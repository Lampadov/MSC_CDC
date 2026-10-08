#include "tusb.h"
#include <MDR32FxQI_rst_clk.h>
#include "MDR32FxQI_usb.h"
#include "device/dcd.h"

/*
 * Порт TinyUSB (device) для USB-контроллера К1986ВЕ92QI / MDR32F9Q2I.
 *
 * Модель работы контроллера, на которой построен порт:
 *  - у каждой точки EP0..EP3 один бит EPRDY на оба направления. Пока EPRDY=0, хост получает NAK.
 *    Когда транзакция завершена, железо само сбрасывает EPRDY и поднимает SCTDONE;
 *  - регистры STS/TS хранят результат ПОСЛЕДНЕЙ транзакции и сами не очищаются, поэтому
 *    по ним нельзя определить, что транзакция только что произошла. Признаком служит пара
 *    "программа записала EPRDY=1" (ep_armed) + "теперь EPRDY=0";
 *  - если EPRDY=1, а в TX FIFO пусто, на IN-токен контроллер отвечает ZLP.
 *
 * Правила порта (не зависят от класса):
 *  1. EPRDY взводится только когда есть что обслуживать: стек поставил передачу (dcd_edpt_xfer)
 *     в любом из направлений. Иначе точка остаётся в NAK - это и есть управление потоком.
 *  2. Завершение IN сообщается стеку только для передачи, которую он поставил (pending).
 *     ACK на "сам пошедший" ZLP только переключает DATASEQ.
 *  3. Данные OUT читаются в буфер стека только если он поставил приём. Пришедший без
 *     буфера пакет остаётся в RX FIFO (rx_parked) и отдаётся стеку в dcd_edpt_xfer(OUT).
 *  4. Транзакция, завершившаяся до того, как xfer перевзвёл точку, обрабатывается внутри xfer
 *     (иначе прерывание увидело бы EPRDY=1 и пропустило её).
 */

#define DCD_EP_NUM    4   // EP0..EP3
#define DCD_FIFO_SIZE 64  // размер FIFO точки

// Состояние одного направления одной точки
typedef struct {
    uint8_t*      buffer;
    uint16_t      total_len;
    volatile bool pending; // передача поставлена стеком (dcd_edpt_xfer) и ещё не завершена
} ep_state_t;

static ep_state_t         ep_state[DCD_EP_NUM][2]; // [точка][TUSB_DIR_OUT / TUSB_DIR_IN]
static volatile bool      ep_armed[DCD_EP_NUM];    // EPRDY=1 записан программой, железо его ещё не сбросило
static volatile bool      rx_parked[DCD_EP_NUM];   // в RX FIFO лежит OUT-пакет, буфера для него пока нет
static uint32_t           set_addr = 0;
static uint32_t           sis;

// Счётчики для отладки (смотреть в Watch-окне Keil)
typedef struct {
    uint32_t zlp_ack;       // ACK на ZLP, которого стек не ставил
    uint32_t rx_parked;     // OUT-пакет пришёл без поставленного приёма
    uint32_t rx_delivered;  // отложенный OUT-пакет отдан стеку в dcd_edpt_xfer
    uint32_t rx_trunc;      // пакет длиннее буфера стека (хвост отброшен)
    uint32_t svc_in_xfer;   // завершённая транзакция обработана внутри dcd_edpt_xfer
    uint32_t stall_sent;    // отправлен STALL
} dcd_stat_t;
volatile dcd_stat_t dcd_stat;

uint32_t tusb_time_millis_api(void);
void     board_get_unique_id(uint8_t* id, uint8_t max_len);

//--------------------------------------------------------------------+
// Вспомогательные функции
//--------------------------------------------------------------------+

// Взвести EPRDY (в точности та же запись, что и раньше по всему порту)
static void ep_arm(uint8_t ep)
{
    USB_SetSEPxCTRL((USB_EP_TypeDef)ep, USB_SEPx_CTRL_EPRDY_Ready);
    ep_armed[ep] = true;
}

// Прочитать принятый OUT-пакет в буфер стека и сообщить о завершении.
// Копируется не больше, чем поставил стек; остаток сбрасывается вместе с FIFO.
static void rx_complete(uint8_t rhport, uint8_t ep, ep_state_t* st)
{
    uint32_t count = USB_GetSEPxRXFDC((USB_EP_TypeDef)ep);
    uint32_t n     = (count < st->total_len) ? count : st->total_len;

    if (count > n) {
        dcd_stat.rx_trunc++;
    }

    for (uint32_t i = 0; i < n; i++) {
        st->buffer[i] = USB_GetSEPxRXFD((USB_EP_TypeDef)ep);
    }

    USB_SetSEPxRXFC((USB_EP_TypeDef)ep, 1);

    dcd_event_xfer_complete(rhport, ep, n, XFER_RESULT_SUCCESS, true);
}

//--------------------------------------------------------------------+
// Сброс шины
//--------------------------------------------------------------------+

void handle_usb_device_reset(uint8_t rhport)
{
    (void)rhport;
    set_addr = 0;
    USB_SetSA(0);

    for (int ep = 0; ep < DCD_EP_NUM; ep++) {
        ep_state[ep][TUSB_DIR_IN].buffer     = NULL;
        ep_state[ep][TUSB_DIR_IN].total_len  = 0;
        ep_state[ep][TUSB_DIR_IN].pending    = false;
        ep_state[ep][TUSB_DIR_OUT].buffer    = NULL;
        ep_state[ep][TUSB_DIR_OUT].total_len = 0;
        ep_state[ep][TUSB_DIR_OUT].pending   = false;
        rx_parked[ep]                        = false;
    }

    for (int ep = 0; ep < DCD_EP_NUM; ep++) {
        USB_SetSEPxTXFDC(ep, 1);
        USB_SetSEPxRXFC(ep, 1);

        USB_SetSEPxCTRL(ep,
                        USB_SEPx_CTRL_EPEN_Disable |
                            USB_SEPx_CTRL_EPRDY_NotReady |
                            USB_SEPx_CTRL_EPDATASEQ_Data0 |
                            USB_SEPx_CTRL_EPSSTALL_NotReply |
                            USB_SEPx_CTRL_EPISOEN_Reset);

        USB_SetSEPxCTRL(ep,
                        USB_SEPx_CTRL_EPEN_Enable |
                            USB_SEPx_CTRL_EPRDY_Ready |
                            USB_SEPx_CTRL_EPDATASEQ_Data0 |
                            USB_SEPx_CTRL_EPSSTALL_NotReply |
                            USB_SEPx_CTRL_EPISOEN_Reset);

        ep_armed[ep] = true;
    }

    dcd_event_bus_reset(rhport, TUSB_SPEED_FULL, true);
}

//--------------------------------------------------------------------+
// Передача IN на EP0
//--------------------------------------------------------------------+

static void handle_ep0_in(void)
{
    ep_state_t* state = &ep_state[0][TUSB_DIR_IN];

    USB_SetSEPxTXFDC(USB_EP0, 1);

    // Обработка ZLP (Zero Length Packet)
    if (state->total_len == 0) {
        ep_arm(USB_EP0);
        return;
    }

    for (uint16_t i = 0; i < state->total_len; i++) {
        USB_SetSEPxTXFD(USB_EP0, state->buffer[i]);
    }

    ep_arm(USB_EP0);
}

//--------------------------------------------------------------------+
// Инициализация и управление
//--------------------------------------------------------------------+

bool dcd_init(uint8_t rhport, const tusb_rhport_init_t* rh_init)
{
    (void)rhport;
    (void)rh_init;

    for (int ep = 0; ep < DCD_EP_NUM; ep++) {
        ep_state[ep][TUSB_DIR_IN].pending  = false;
        ep_state[ep][TUSB_DIR_OUT].pending = false;
        ep_armed[ep]                       = false;
        rx_parked[ep]                      = false;
    }

    RST_CLK_HSEconfig(RST_CLK_HSE_ON);
    while (RST_CLK_HSEstatus() == ERROR) {}
    RST_CLK_CPUclkSelectionC1(RST_CLK_CPU_C1srcHSEdiv1);

    RST_CLK_CPU_PLLconfig(RST_CLK_CPU_PLLsrcHSEdiv1, RST_CLK_CPU_PLLmul10);

    RST_CLK_CPU_PLLcmd(ENABLE);
    while (RST_CLK_CPU_PLLstatus() == ERROR);
    RST_CLK_CPU_PLLuse(ENABLE);

    RST_CLK_CPUclkPrescaler(RST_CLK_CPUclkDIV1);
    RST_CLK_CPUclkSelection(RST_CLK_CPUclkCPU_C3);

    RST_CLK_PCLKcmd(RST_CLK_PCLK_USB, ENABLE);

    USB_Clock_TypeDef clock_cfg = {
        .USB_USBC1_Source = USB_C1HSEdiv1,
        .USB_PLLUSBMUL    = USB_PLLUSBMUL6
    };
    USB_BRGInit(&clock_cfg);

    USB_Reset();

    USB_SetHSCR(USB_HSCR_HOST_MODE_Device |
                USB_HSCR_EN_RX_Set |
                USB_HSCR_EN_TX_Set |
                USB_HSCR_DP_PULLUP_Set);

    USB_SetSC(USB_SC_SCGEN_Set |
              USB_SC_SCFSP_Full |
              USB_SC_SCFSR_12Mb);

    USB_SetSEPxCTRL(USB_EP0, USB_SEPx_CTRL_EPEN_Enable |
                                 USB_SEPx_CTRL_EPRDY_Ready);
    ep_armed[0] = true;

    USB_SetSIM(USB_SIM_SCTDONEIE_Set |
               USB_SIM_SCRESETEVIE_Set);

    return true;
}

void dcd_int_enable(uint8_t rhport)
{
    (void)rhport;

    NVIC_EnableIRQ(USB_IRQn);
}

void dcd_int_disable(uint8_t rhport)
{
    (void)rhport;

    NVIC_DisableIRQ(USB_IRQn);
}

void dcd_set_address(uint8_t rhport, uint8_t dev_addr)
{
    (void)rhport;

    set_addr = dev_addr;
}

void dcd_remote_wakeup(uint8_t rhport)
{
    (void)rhport;
}

void dcd_connect(uint8_t rhport)
{
    (void)rhport;
}

void dcd_disconnect(uint8_t rhport)
{
    (void)rhport;
}

void dcd_sof_enable(uint8_t rhport, bool enable)
{
    (void)rhport;
    (void)enable;
}

//--------------------------------------------------------------------+
// Конечные точки
//--------------------------------------------------------------------+

bool dcd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const* ep_desc)
{
    (void)rhport;

    uint8_t const epnum = tu_edpt_number(ep_desc->bEndpointAddress);
    uint8_t const dir   = tu_edpt_dir(ep_desc->bEndpointAddress);

    if (epnum >= DCD_EP_NUM) {
        return false;
    }

    uint32_t ctrl = USB_SEPx_CTRL_EPEN_Enable |
                    USB_SEPx_CTRL_EPDATASEQ_Data0 |
                    USB_SEPx_CTRL_EPSSTALL_NotReply |
                    USB_SEPx_CTRL_EPISOEN_Reset |
                    USB_SEPx_CTRL_EPRDY_Ready;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    USB_SetSEPxCTRL((USB_EP_TypeDef)epnum, ctrl);
    USB_SetSEPxRXFC((USB_EP_TypeDef)epnum, 1);
    USB_SetSEPxTXFDC((USB_EP_TypeDef)epnum, 1);

    ep_armed[epnum]                = true;
    rx_parked[epnum]               = false;
    ep_state[epnum][dir].pending   = false;

    __set_PRIMASK(primask);

    return true;
}

void dcd_edpt_close(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;

    uint8_t const epnum = tu_edpt_number(ep_addr);
    uint8_t const dir   = tu_edpt_dir(ep_addr);

    if (epnum < DCD_EP_NUM) {
        ep_state[epnum][dir].pending = false;
    }
}

void dcd_edpt_close_all(uint8_t rhport)
{
    (void)rhport;

    // Все поставленные передачи отменяются; железо не трогаем - dcd_edpt_open() перенастроит точки
    for (int ep = 1; ep < DCD_EP_NUM; ep++) {
        ep_state[ep][TUSB_DIR_IN].pending  = false;
        ep_state[ep][TUSB_DIR_OUT].pending = false;
        rx_parked[ep]                      = false;
    }
}

// Обработать завершённую транзакцию точки EP1..EP3, если она есть.
// Вызывается из прерывания и из dcd_edpt_xfer (при запрещённых прерываниях).
static void ep_service(uint8_t rhport, uint8_t ep)
{
    // Взведённой точки не было - STS/TS устаревшие, транзакции не было
    if (!ep_armed[ep]) {
        return;
    }
    // EPRDY ещё стоит - хост к точке не обращался
    if (USB_GetSEPxCTRL((USB_EP_TypeDef)ep) & USB_SEPx_CTRL_EPRDY_Ready) {
        return;
    }
    ep_armed[ep] = false;

    uint32_t ts  = USB_GetSEPxTS((USB_EP_TypeDef)ep);
    uint32_t sts = USB_GetSEPxSTS((USB_EP_TypeDef)ep);

    ep_state_t* in  = &ep_state[ep][TUSB_DIR_IN];
    ep_state_t* out = &ep_state[ep][TUSB_DIR_OUT];

    if (sts & USB_SEPx_STS_SCSTALLSENT_Set) {
        // Отправлен STALL: поставленные передачи сняты, точка возвращается в работу
        dcd_stat.stall_sent++;
        in->pending  = false;
        out->pending = false;
        USB_SEPxToggleEPDATASEQ((USB_EP_TypeDef)ep);
        ep_arm(ep);
        return;
    }

    uint32_t type = ts & USB_SEPx_TS_SCTTYPE_Msk;

    if (type == USB_SEPx_TS_SCTTYPE_In) {
        if (sts & USB_SEPx_STS_SCACKRXED_Set) {
            USB_SetSEPxTXFDC((USB_EP_TypeDef)ep, 1);
            USB_SEPxToggleEPDATASEQ((USB_EP_TypeDef)ep);

            if (in->pending) {
                in->pending = false;
                dcd_event_xfer_complete(rhport, (uint8_t)(ep | 0x80), in->total_len, XFER_RESULT_SUCCESS, true);
            } else {
                // Хост принял ZLP, который контроллер отправил сам (EPRDY=1 при пустом FIFO)
                dcd_stat.zlp_ack++;
            }
        }
    } else if (type == USB_SEPx_TS_SCTTYPE_Outdata) {
        if (out->pending) {
            out->pending = false;
            rx_complete(rhport, ep, out);
        } else {
            // Приёма никто не ставил: пакет остаётся в RX FIFO до dcd_edpt_xfer(OUT)
            rx_parked[ep] = true;
            dcd_stat.rx_parked++;
        }
    }

    // Пока у стека есть что-то поставленное - точку надо держать взведённой (EPRDY общий на оба
    // направления). Если ничего нет - NAK. Пока в RX FIFO лежит отложенный пакет, не взводим:
    // иначе следующий OUT пришёл бы в занятый FIFO.
    if (!rx_parked[ep] && (in->pending || out->pending)) {
        ep_arm(ep);
    }
}

bool dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t* buffer, uint16_t total_bytes, bool is_isr)
{
    (void)is_isr; // защита от гонки с прерыванием - через PRIMASK, оно работает в любом контексте

    uint8_t const epnum = tu_edpt_number(ep_addr);
    uint8_t const dir   = tu_edpt_dir(ep_addr);

    if (epnum >= DCD_EP_NUM) {
        return false;
    }
    if (dir == TUSB_DIR_IN && total_bytes > DCD_FIFO_SIZE) {
        return false; // не поместится в TX FIFO
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    ep_state_t* state = &ep_state[epnum][dir];

    if (epnum == USB_EP0) {
        state->buffer    = buffer;
        state->total_len = total_bytes;

        if (dir == TUSB_DIR_IN) {
            // Начать передачу IN
            handle_ep0_in();
        } else {
            // Готов к приему OUT
            USB_SetSEPxRXFC(USB_EP0, 1);
            ep_arm(USB_EP0);
        }
    } else {
        // Транзакция могла завершиться, а прерывание ещё не выполнилось: сначала обработать её,
        // иначе взведение EPRDY ниже скроет факт завершения.
        if (ep_armed[epnum] && !(USB_GetSEPxCTRL((USB_EP_TypeDef)epnum) & USB_SEPx_CTRL_EPRDY_Ready)) {
            dcd_stat.svc_in_xfer++;
            ep_service(rhport, epnum);
        }

        state->buffer    = buffer;
        state->total_len = total_bytes;
        state->pending   = true;

        if (dir == TUSB_DIR_IN) {
            // Начать передачу IN. Если точка уже взведена (ждёт OUT), FIFO пуст - сбрасывать не нужно.
            if (!ep_armed[epnum]) {
                USB_SetSEPxTXFDC((USB_EP_TypeDef)epnum, 1);
            }

            for (uint16_t i = 0; i < total_bytes; i++) {
                USB_SetSEPxTXFD((USB_EP_TypeDef)epnum, buffer[i]);
            }

            // Если в RX FIFO лежит отложенный пакет, точку взведёт приём (dcd_edpt_xfer OUT)
            if (!ep_armed[epnum] && !rx_parked[epnum]) {
                ep_arm(epnum);
            }
        } else {
            if (rx_parked[epnum]) {
                // Пакет уже в RX FIFO: отдаём его сразу, ничего не сбрасывая
                rx_parked[epnum] = false;
                state->pending   = false;
                dcd_stat.rx_delivered++;
                rx_complete(rhport, epnum, state);

                // Передача IN, которая ждала освобождения FIFO, может стартовать
                if (ep_state[epnum][TUSB_DIR_IN].pending && !ep_armed[epnum]) {
                    ep_arm(epnum);
                }
            } else if (!ep_armed[epnum]) {
                // Готов к приему OUT
                USB_SetSEPxRXFC((USB_EP_TypeDef)epnum, 1);
                ep_arm(epnum);
            }
        }
    }

    __set_PRIMASK(primask);

    return true;
}

void dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;
    uint8_t epnum = tu_edpt_number(ep_addr);

    if (epnum >= DCD_EP_NUM) {
        return;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    // Все поставленные на точку передачи снимаются (требование TinyUSB к dcd_edpt_stall)
    ep_state[epnum][TUSB_DIR_IN].pending  = false;
    ep_state[epnum][TUSB_DIR_OUT].pending = false;
    rx_parked[epnum]                      = false;

    USB_SetSEPxTXFDC(epnum, 1);
    USB_SetSEPxRXFC(epnum, 1);

    USB_SetSEPxCTRL((USB_EP_TypeDef)epnum,
                    USB_SEPx_CTRL_EPSSTALL_Reply |
                        USB_SEPx_CTRL_EPDATASEQ_Data0 |
                        USB_SEPx_CTRL_EPRDY_Ready);
    ep_armed[epnum] = true;

    __set_PRIMASK(primask);
}

void dcd_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;
    uint8_t epnum = tu_edpt_number(ep_addr);

    if (epnum >= DCD_EP_NUM) {
        return;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    rx_parked[epnum] = false;

    USB_SetSEPxTXFDC(epnum, 1);
    USB_SetSEPxRXFC(epnum, 1);

    USB_SetSEPxCTRL((USB_EP_TypeDef)epnum,
                    USB_SEPx_CTRL_EPSSTALL_NotReply |
                        USB_SEPx_CTRL_EPDATASEQ_Data0 |
                        USB_SEPx_CTRL_EPRDY_Ready);
    ep_armed[epnum] = true;

    __set_PRIMASK(primask);
}

//--------------------------------------------------------------------+
// Прерывание
//--------------------------------------------------------------------+

// Обработка завершённой транзакции EP0
static void ep0_service(uint8_t rhport)
{
    if (!ep_armed[0]) {
        return;
    }
    if (USB_GetSEPxCTRL(USB_EP0) & USB_SEPx_CTRL_EPRDY_Ready) {
        return;
    }
    ep_armed[0] = false;

    uint32_t ts   = USB_GetSEPxTS(USB_EP0);
    uint32_t sts  = USB_GetSEPxSTS(USB_EP0);
    uint32_t type = ts & USB_SEPx_TS_SCTTYPE_Msk;

    if (sts & USB_SEPx_STS_SCSTALLSENT_Set) {
        // Отправлен STALL: переключаем DATASEQ и снова готовим EP0 (как и для EP1-EP3)
        dcd_stat.stall_sent++;
        USB_SEPxToggleEPDATASEQ(USB_EP0);
    }
    // Обработка SETUP
    else if (type == USB_SEPx_TS_SCTTYPE_Setup) {

        USB_SetSEPxCTRL(USB_EP0, USB_SEPx_CTRL_EPDATASEQ_Data0); // Явная установка DATA0

        uint8_t setup[8];
        for (int i = 0; i < 8; i++) {
            setup[i] = USB_GetSEPxRXFD(USB_EP0);
        }

        USB_SetSEPxRXFC(USB_EP0, 1);
        USB_SEPxToggleEPDATASEQ(USB_EP0);
        dcd_event_setup_received(rhport, setup, true); // Передача пакета SETUP в стек TinyUSB

        // Ответ на SETUP должен быть поставлен до взведения EP0, иначе на IN-токен уйдёт пустой пакет
        tud_task();
    }
    // Обработка IN
    else if (type == USB_SEPx_TS_SCTTYPE_In) {

        if (sts & USB_SEPx_STS_SCACKRXED_Set) {
            ep_state_t* state = &ep_state[0][TUSB_DIR_IN];
            dcd_event_xfer_complete(rhport, 0x80, state->total_len, XFER_RESULT_SUCCESS, true);
            USB_SetSEPxTXFDC(USB_EP0, 1);

            if (set_addr) {
                USB_SetSA(set_addr);
                set_addr = 0;
            }

            USB_SEPxToggleEPDATASEQ(USB_EP0);
        }
    }
    // Обработка OUT
    else if (type == USB_SEPx_TS_SCTTYPE_Outdata) {
        rx_complete(rhport, 0, &ep_state[0][TUSB_DIR_OUT]);
    }

    // EP0 всегда остаётся взведённой: она должна принять следующий SETUP
    ep_arm(USB_EP0);
}

void dcd_int_handler(uint8_t rhport)
{
    if (sis & USB_SIS_SCRESETEV_Set) {
        handle_usb_device_reset(rhport);
        return;
    }

    if (sis & USB_SIS_SCRESUME_Set) {
        dcd_event_bus_signal(rhport, DCD_EVENT_RESUME, true);
    }

    if (sis & USB_SIS_SCTDONE_Set) {
        ep0_service(rhport);

        // EP0 не прерывает обработку EP1-EP3: их транзакции могли завершиться в этом же проходе,
        // а флаг SCTDONE уже сброшен в USB_IRQHandler
        for (uint8_t ep = 1; ep < DCD_EP_NUM; ep++) {
            ep_service(rhport, ep);
        }
    }
}

uint32_t tusb_time_millis_api(void)
{
    return 0;
}

void board_get_unique_id(uint8_t* id, uint8_t max_len)
{
    const uint8_t default_id[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE };

    for (uint8_t i = 0; i < max_len; i++) {
        id[i] = (i < sizeof(default_id)) ? default_id[i] : 0;
    }
}

void USB_IRQHandler(void)
{
    sis = USB_GetSIS();
    // Сбрасываем ровно те флаги, что прочитали, ДО обработки: событие, пришедшее во время
    // обработки, поднимет флаг снова и не будет потеряно
    USB_SetSIS(sis);
    dcd_int_handler(0);
}
