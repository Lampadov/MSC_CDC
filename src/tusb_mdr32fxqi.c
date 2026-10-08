#include "tusb.h"
#include <MDR32FxQI_rst_clk.h>
#include "MDR32FxQI_usb.h"
#include "device/dcd.h"

/*
 * Порт TinyUSB для USB-контроллера MDR32F9Q2I.
 *
 * Особенности контроллера:
 *  - у точки один бит EPRDY на оба направления (IN и OUT). Пока EPRDY = 0, хост получает NAK;
 *  - после транзакции контроллер сам сбрасывает EPRDY и вызывает прерывание SCTDONE;
 *  - регистры TS/STS не очищаются, в них всегда результат последней транзакции.
 *    Поэтому транзакция считается прошедшей, только если мы взводили EPRDY (ep_armed),
 *    а теперь он сброшен;
 *  - если EPRDY = 1, а TX FIFO пуст, контроллер сам отвечает на IN пустым пакетом (ZLP).
 *
 * Главное правило порта: EPRDY взводится, только когда стек поставил передачу (dcd_edpt_xfer).
 * Поэтому после сброса шины, dcd_edpt_open и clear_stall точки EP1..EP3 остаются в NAK.
 * STALL держится (повторным взведением с флагом STALL), пока стек не вызовет clear_stall.
 * Если OUT-пакет пришёл раньше, чем стек дал буфер, он остаётся в RX FIFO (rx_parked)
 * и отдаётся стеку при следующем dcd_edpt_xfer.
 */

#define EP_COUNT 4    // EP0..EP3
#define EP_SIZE  64   // размер FIFO

typedef struct {
    uint8_t*      buffer;
    uint16_t      total_len;
    volatile bool pending;    // стек поставил передачу, она ещё не завершена
} ep_state_t;

static ep_state_t    ep_state[EP_COUNT][2]; // [точка][TUSB_DIR_OUT / TUSB_DIR_IN]
static volatile bool ep_armed[EP_COUNT];    // мы взвели EPRDY и ждём транзакцию
#if USB_SETUP_LOG
// Отладка: журнал принятых SETUP-пакетов (смотреть в окне Watch отладчика)
volatile uint8_t  usb_setup_log[32][8];
volatile uint32_t usb_setup_cnt;
volatile uint32_t usb_reset_cnt;
volatile uint32_t usb_stall_cnt;
#endif
static volatile bool ep0_stalled;               // стек ответил на SETUP STALL-ом
static volatile bool rx_parked[EP_COUNT];   // в RX FIFO лежит пакет, которому ещё не дали буфер
static volatile bool ep_halted[EP_COUNT];   // точка в состоянии STALL до clear_stall
static uint32_t      set_addr = 0;
static uint32_t      sis;

static void handle_ep0(uint8_t rhport);
static void handle_ep(uint8_t rhport, uint8_t ep);

//--------------------------------------------------------------------+
// Вспомогательные функции
//--------------------------------------------------------------------+

// Взвести EPRDY - точка готова к обмену
static void ep_set_ready(uint8_t ep)
{
    USB_SetSEPxCTRL(ep, USB_SEPx_CTRL_EPRDY_Ready);
    ep_armed[ep] = true;
}

// Прошла ли транзакция с момента взведения EPRDY
static bool ep_transaction_done(uint8_t ep)
{
    if (ep_armed[ep] && !(USB_GetSEPxCTRL(ep) & USB_SEPx_CTRL_EPRDY_Ready)) {
        ep_armed[ep] = false;
        return true;
    }
    return false;
}

// Снять все поставленные передачи точки
static void ep_clear_state(uint8_t ep)
{
    ep_state[ep][TUSB_DIR_IN].pending  = false;
    ep_state[ep][TUSB_DIR_OUT].pending = false;
    rx_parked[ep]                      = false;
}

//--------------------------------------------------------------------+
// Передача и приём данных
//--------------------------------------------------------------------+

static void handle_ep0_in(void)
{
    ep_state_t* state = &ep_state[0][TUSB_DIR_IN];

    USB_SetSEPxTXFDC(USB_EP0, 1);

    for (uint16_t i = 0; i < state->total_len; i++) {   // total_len = 0 -> уйдёт ZLP
        USB_SetSEPxTXFD(USB_EP0, state->buffer[i]);
    }

    ep_set_ready(USB_EP0);
}

static void handle_ep_in(uint8_t ep)
{
    ep_state_t* state = &ep_state[ep][TUSB_DIR_IN];

    // Если точка уже взведена (ждёт OUT), TX FIFO пуст - сбрасывать нечего
    if (!ep_armed[ep]) {
        USB_SetSEPxTXFDC(ep, 1);
    }

    for (uint16_t i = 0; i < state->total_len; i++) {
        USB_SetSEPxTXFD(ep, state->buffer[i]);
    }

    // Пока в RX FIFO лежит отложенный пакет, точку не взводим - её взведёт dcd_edpt_xfer(OUT)
    if (!ep_armed[ep] && !rx_parked[ep]) {
        ep_set_ready(ep);
    }
}

// Прочитать OUT-пакет из FIFO в буфер стека (для любой точки, включая EP0)
static void handle_ep_out(uint8_t rhport, uint8_t ep)
{
    ep_state_t* state = &ep_state[ep][TUSB_DIR_OUT];

    uint32_t count = USB_GetSEPxRXFDC(ep);
    if (count > state->total_len) {
        count = state->total_len;   // не пишем за пределы буфера стека
    }

    for (uint32_t i = 0; i < count; i++) {
        state->buffer[i] = USB_GetSEPxRXFD(ep);
    }
    USB_SetSEPxRXFC(ep, 1);

    state->pending = false;
    dcd_event_xfer_complete(rhport, ep, count, XFER_RESULT_SUCCESS, true);
}

//--------------------------------------------------------------------+
// Сброс шины
//--------------------------------------------------------------------+

static void handle_usb_device_reset(uint8_t rhport)
{
    set_addr = 0;
    USB_SetSA(0);
#if USB_SETUP_LOG
    usb_reset_cnt++;
#endif

    for (int ep = 0; ep < EP_COUNT; ep++) {
        ep_clear_state(ep);

        USB_SetSEPxTXFDC(ep, 1);
        USB_SetSEPxRXFC(ep, 1);

        USB_SetSEPxCTRL(ep,
                        USB_SEPx_CTRL_EPEN_Disable |
                            USB_SEPx_CTRL_EPRDY_NotReady |
                            USB_SEPx_CTRL_EPDATASEQ_Data0 |
                            USB_SEPx_CTRL_EPSSTALL_NotReply |
                            USB_SEPx_CTRL_EPISOEN_Reset);

        // Готова только EP0 (ждёт SETUP). Остальные отвечают NAK, пока стек не поставит передачу:
        // точка с EPRDY=1 и пустым FIFO отвечает на IN пустым пакетом (ZLP), а для MSC это ошибка
        ep_halted[ep] = false;
        ep_armed[ep]  = (ep == USB_EP0);
        USB_SetSEPxCTRL(ep,
                        USB_SEPx_CTRL_EPEN_Enable |
                            (ep == USB_EP0 ? USB_SEPx_CTRL_EPRDY_Ready : USB_SEPx_CTRL_EPRDY_NotReady) |
                            USB_SEPx_CTRL_EPDATASEQ_Data0 |
                            USB_SEPx_CTRL_EPSSTALL_NotReply |
                            USB_SEPx_CTRL_EPISOEN_Reset);
    }

    dcd_event_bus_reset(rhport, TUSB_SPEED_FULL, true);
}

//--------------------------------------------------------------------+
// Инициализация
//--------------------------------------------------------------------+

bool dcd_init(uint8_t rhport, const tusb_rhport_init_t* rh_init)
{
    (void)rhport;
    (void)rh_init;

    for (int ep = 0; ep < EP_COUNT; ep++) {
        ep_clear_state(ep);
        ep_armed[ep]  = false;
        ep_halted[ep] = false;
    }

    // Ядро: HSE x10, USB: HSE x6 = 48 МГц (для кварца 8 МГц)
    RST_CLK_HSEconfig(RST_CLK_HSE_ON);
    while (RST_CLK_HSEstatus() == ERROR) {}
    RST_CLK_CPUclkSelectionC1(RST_CLK_CPU_C1srcHSEdiv1);

    RST_CLK_CPU_PLLconfig(RST_CLK_CPU_PLLsrcHSEdiv1, RST_CLK_CPU_PLLmul10);
    RST_CLK_CPU_PLLcmd(ENABLE);
    while (RST_CLK_CPU_PLLstatus() == ERROR) {}
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

// Адрес применяется после того, как хост подтвердит статус SET_ADDRESS (в handle_ep0)
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

    uint8_t epnum = tu_edpt_number(ep_desc->bEndpointAddress);
    uint8_t dir   = tu_edpt_dir(ep_desc->bEndpointAddress);

    if (epnum >= EP_COUNT) {
        return false;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    // Точка включена, но в NAK: EPRDY взведёт dcd_edpt_xfer, когда у стека будут данные или буфер
    USB_SetSEPxCTRL(epnum,
                    USB_SEPx_CTRL_EPEN_Enable |
                        USB_SEPx_CTRL_EPRDY_NotReady |
                        USB_SEPx_CTRL_EPDATASEQ_Data0 |
                        USB_SEPx_CTRL_EPSSTALL_NotReply |
                        USB_SEPx_CTRL_EPISOEN_Reset);
    USB_SetSEPxRXFC(epnum, 1);
    USB_SetSEPxTXFDC(epnum, 1);

    ep_armed[epnum]              = false;
    ep_halted[epnum]             = false;
    rx_parked[epnum]             = false;
    ep_state[epnum][dir].pending = false;

    __set_PRIMASK(primask);
    return true;
}

void dcd_edpt_close(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;

    uint8_t epnum = tu_edpt_number(ep_addr);
    if (epnum < EP_COUNT) {
        ep_state[epnum][tu_edpt_dir(ep_addr)].pending = false;
    }
}

void dcd_edpt_close_all(uint8_t rhport)
{
    (void)rhport;

    // Железо не трогаем - dcd_edpt_open() перенастроит точки
    for (int ep = 1; ep < EP_COUNT; ep++) {
        ep_clear_state(ep);
    }
}

bool dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t* buffer, uint16_t total_bytes, bool is_isr)
{
    (void)is_isr;

    uint8_t epnum = tu_edpt_number(ep_addr);
    uint8_t dir   = tu_edpt_dir(ep_addr);

    if (epnum >= EP_COUNT) {
        return false;
    }
    if (dir == TUSB_DIR_IN && total_bytes > EP_SIZE) {
        return false;   // в TX FIFO помещается только один пакет
    }

    // Запрещаем прерывания, чтобы обработчик USB не вмешался посередине
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    // Транзакция могла уже пройти, а прерывание ещё не успело её обработать.
    // Обрабатываем её сейчас, иначе после повторного взведения EPRDY она потеряется.
    if (epnum != USB_EP0 && ep_transaction_done(epnum)) {
        handle_ep(rhport, epnum);
    }

    ep_state_t* state = &ep_state[epnum][dir];
    state->buffer     = buffer;
    state->total_len  = total_bytes;
    state->pending    = true;

    if (epnum == USB_EP0) {
        if (dir == TUSB_DIR_IN) {
            handle_ep0_in();
        } else {
            USB_SetSEPxRXFC(USB_EP0, 1);
            ep_set_ready(USB_EP0);
        }
    } else if (dir == TUSB_DIR_IN) {
        handle_ep_in(epnum);
    } else if (rx_parked[epnum]) {
        // Пакет уже лежит в RX FIFO - отдаём его сразу
        rx_parked[epnum] = false;
        handle_ep_out(rhport, epnum);

        // FIFO освободился - теперь может уйти отложенный IN
        if (ep_state[epnum][TUSB_DIR_IN].pending && !ep_armed[epnum]) {
            ep_set_ready(epnum);
        }
    } else if (!ep_armed[epnum]) {
        USB_SetSEPxRXFC(epnum, 1);
        ep_set_ready(epnum);
    }

    __set_PRIMASK(primask);
    return true;
}

void dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;

    uint8_t epnum = tu_edpt_number(ep_addr);
    if (epnum >= EP_COUNT) {
        return;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    ep_clear_state(epnum);   // TinyUSB требует снять поставленные передачи

    USB_SetSEPxTXFDC(epnum, 1);
    USB_SetSEPxRXFC(epnum, 1);

    USB_SetSEPxCTRL(epnum,
                    USB_SEPx_CTRL_EPSSTALL_Reply |
                        USB_SEPx_CTRL_EPDATASEQ_Data0 |
                        USB_SEPx_CTRL_EPRDY_Ready);
    ep_armed[epnum]  = true;
    ep0_stalled      = (epnum == USB_EP0);
    ep_halted[epnum] = (epnum != USB_EP0);   // EP0 выходит из STALL сама по новому SETUP

    __set_PRIMASK(primask);
}

void dcd_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;

    uint8_t epnum = tu_edpt_number(ep_addr);
    if (epnum >= EP_COUNT) {
        return;
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    rx_parked[epnum]  = false;
    ep_halted[epnum]  = false;

    USB_SetSEPxTXFDC(epnum, 1);
    USB_SetSEPxRXFC(epnum, 1);

    // После снятия STALL точка в NAK, пока стек не поставит передачу (обычно сразу следом)
    USB_SetSEPxCTRL(epnum,
                    USB_SEPx_CTRL_EPSSTALL_NotReply |
                        USB_SEPx_CTRL_EPDATASEQ_Data0 |
                        USB_SEPx_CTRL_EPRDY_NotReady);
    ep_armed[epnum] = false;

    __set_PRIMASK(primask);
}

//--------------------------------------------------------------------+
// Обработка завершённых транзакций
//--------------------------------------------------------------------+

static void handle_ep0(uint8_t rhport)
{
    uint32_t ts  = USB_GetSEPxTS(USB_EP0);
    uint32_t sts = USB_GetSEPxSTS(USB_EP0);

    // Отправлен STALL
    if (sts & USB_SEPx_STS_SCSTALLSENT_Set) {
        ep0_stalled = false;
#if USB_SETUP_LOG
        usb_stall_cnt++;
#endif
        USB_SEPxToggleEPDATASEQ(USB_EP0);
    }

    // Обработка SETUP
    else if ((ts & USB_SEPx_TS_SCTTYPE_Msk) == USB_SEPx_TS_SCTTYPE_Setup) {

        USB_SetSEPxCTRL(USB_EP0, USB_SEPx_CTRL_EPDATASEQ_Data0); // Явная установка DATA0

        uint8_t setup[8];
        for (int i = 0; i < 8; i++) {
            setup[i] = USB_GetSEPxRXFD(USB_EP0);
        }

        USB_SetSEPxRXFC(USB_EP0, 1);
        USB_SEPxToggleEPDATASEQ(USB_EP0);
        ep0_stalled = false;
#if USB_SETUP_LOG
        for (int i = 0; i < 8; i++) {
            usb_setup_log[usb_setup_cnt & 31][i] = setup[i];
        }
        usb_setup_cnt++;
#endif
        dcd_event_setup_received(rhport, setup, true);

        // Ответ на SETUP должен попасть в FIFO до взведения EP0 ниже,
        // иначе на первый IN контроллер отправит пустой пакет
        tud_task();
    }

    // Обработка IN
    else if ((ts & USB_SEPx_TS_SCTTYPE_Msk) == USB_SEPx_TS_SCTTYPE_In) {

        if (sts & USB_SEPx_STS_SCACKRXED_Set) {
            USB_SetSEPxTXFDC(USB_EP0, 1);

            if (set_addr) {
                USB_SetSA(set_addr);
                set_addr = 0;
            }
            USB_SEPxToggleEPDATASEQ(USB_EP0);

            // Следующий пакет ответа должен попасть в FIFO до взведения EP0 ниже,
            // иначе хост получит пустой пакет и оборвёт длинный дескриптор (>64 байт)
            dcd_event_xfer_complete(rhport, 0x80, ep_state[0][TUSB_DIR_IN].total_len, XFER_RESULT_SUCCESS, true);
            tud_task();
        }
    }

    // Обработка OUT
    else if ((ts & USB_SEPx_TS_SCTTYPE_Msk) == USB_SEPx_TS_SCTTYPE_Outdata) {
        handle_ep_out(rhport, USB_EP0);
    }

    // EP0 всегда взведена - она должна принять следующий SETUP.
    // Исключение: стек только что ответил на SETUP STALL-ом - не затираем его
    if (!ep0_stalled) {
        ep_set_ready(USB_EP0);
    }
}

static void handle_ep(uint8_t rhport, uint8_t ep)
{
    ep_state_t* in  = &ep_state[ep][TUSB_DIR_IN];
    ep_state_t* out = &ep_state[ep][TUSB_DIR_OUT];

    uint32_t ts  = USB_GetSEPxTS(ep);
    uint32_t sts = USB_GetSEPxSTS(ep);

    // Отправлен STALL: точка остаётся в STALL до clear_stall - взводим её снова с флагом STALL
    if (sts & USB_SEPx_STS_SCSTALLSENT_Set) {
        in->pending  = false;
        out->pending = false;
        if (ep_halted[ep]) {
            USB_SetSEPxCTRL(ep,
                            USB_SEPx_CTRL_EPSSTALL_Reply |
                                USB_SEPx_CTRL_EPDATASEQ_Data0 |
                                USB_SEPx_CTRL_EPRDY_Ready);
            ep_armed[ep] = true;
        }
        return;
    }

    // Обработка IN
    if ((ts & USB_SEPx_TS_SCTTYPE_Msk) == USB_SEPx_TS_SCTTYPE_In) {

        if (sts & USB_SEPx_STS_SCACKRXED_Set) {
            USB_SetSEPxTXFDC(ep, 1);
            USB_SEPxToggleEPDATASEQ(ep);

            if (in->pending) {
                in->pending = false;
                dcd_event_xfer_complete(rhport, ep | 0x80, in->total_len, XFER_RESULT_SUCCESS, true);
            }
            // Иначе это был ZLP, который контроллер отправил сам - стеку не сообщаем
        }
    }

    // Обработка OUT
    else if ((ts & USB_SEPx_TS_SCTTYPE_Msk) == USB_SEPx_TS_SCTTYPE_Outdata) {

        if (out->pending) {
            handle_ep_out(rhport, ep);
        } else {
            rx_parked[ep] = true;   // буфера ещё нет - пакет ждёт в FIFO до dcd_edpt_xfer
        }
    }

    // Снова взводим точку, только если у стека остались поставленные передачи
    if (!rx_parked[ep] && (in->pending || out->pending)) {
        ep_set_ready(ep);
    }
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
        // SCTDONE общий для всех точек - проверяем каждую
        if (ep_transaction_done(USB_EP0)) {
            handle_ep0(rhport);
        }

        for (int ep = 1; ep < EP_COUNT; ep++) {
            if (ep_transaction_done(ep)) {
                handle_ep(rhport, ep);
            }
        }
    }
}

void USB_IRQHandler(void)
{
    sis = USB_GetSIS();
    USB_SetSIS(sis);   // сбрасываем флаги до обработки, чтобы не пропустить новые
    dcd_int_handler(0);
}

// Без RTOS TinyUSB требует эту функцию от приложения
uint32_t tusb_time_millis_api(void)
{
    return 0;
}
