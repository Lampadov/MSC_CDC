/*
 * Порт TinyUSB (device) для USB-контроллера MDR32F9Q2I (К1986ВЕ92QI).
 *
 * Как устроен контроллер (то, что важно для понимания порта):
 *  - 4 конечные точки EP0..EP3, у каждой FIFO по 64 байта на приём и на передачу;
 *  - у точки ОДИН бит готовности EPRDY на оба направления. Пока EPRDY = 0, хост получает NAK;
 *  - после транзакции железо само сбрасывает EPRDY и выставляет флаг прерывания SCTDONE;
 *  - регистры TS/STS хранят результат ПОСЛЕДНЕЙ транзакции и сами не очищаются,
 *    поэтому "транзакция была" определяем так: мы взвели EPRDY (ep_armed), а теперь он сброшен;
 *  - если EPRDY = 1, а TX FIFO пуст, на IN-токен контроллер сам отвечает пустым пакетом (ZLP).
 *
 * Правила порта (одинаковы для любого класса: CDC, MSC, HID, vendor...):
 *  1. EPRDY взводится, только если стек поставил передачу (dcd_edpt_xfer) хотя бы в одну сторону.
 *     Иначе точка отвечает NAK - так работает управление потоком.
 *  2. Стеку сообщаем только о завершении тех передач, которые он сам поставил (pending).
 *  3. OUT-пакет, пришедший раньше, чем стек дал буфер, остаётся в RX FIFO ("припаркован")
 *     и отдаётся при следующем dcd_edpt_xfer(OUT). FIFO при этом не сбрасываем - данные не теряются.
 *  4. Если транзакция завершилась, а прерывание ещё не успело её обработать, её обрабатывает
 *     сам dcd_edpt_xfer - иначе повторное взведение EPRDY "спрятало" бы завершение.
 */

#include "tusb.h"
#include "device/dcd.h"
#include "MDR32FxQI_rst_clk.h"
#include "MDR32FxQI_usb.h"

#define EP_COUNT   4    // EP0..EP3
#define EP_SIZE    64   // размер FIFO точки, байт

#define HW(ep)     ((USB_EP_TypeDef)(ep))

//--------------------------------------------------------------------+
// Состояние
//--------------------------------------------------------------------+

// Передача, поставленная стеком в одном направлении
typedef struct {
    uint8_t*      buf;
    uint16_t      len;
    volatile bool pending;   // поставлена и ещё не завершена
} xfer_t;

static xfer_t        xfer[EP_COUNT][2];   // [номер точки][TUSB_DIR_OUT / TUSB_DIR_IN]
static volatile bool ep_armed[EP_COUNT];  // мы взвели EPRDY, транзакции ещё не было
static volatile bool rx_parked[EP_COUNT]; // в RX FIFO лежит OUT-пакет, ожидающий буфера
static uint8_t       new_address;         // адрес, применяемый после статуса SET_ADDRESS

// Счётчики для отладки - удобно смотреть в окне Watch в Keil
volatile struct {
    uint32_t zlp_ack;       // хост принял ZLP, который стек не ставил
    uint32_t rx_parked;     // OUT-пакет пришёл раньше буфера
    uint32_t rx_delivered;  // припаркованный пакет отдан стеку
    uint32_t rx_trunc;      // пакет длиннее буфера, хвост отброшен
    uint32_t svc_in_xfer;   // завершение транзакции обработано внутри dcd_edpt_xfer
    uint32_t stall_sent;    // отправлен STALL
} dcd_stat;

//--------------------------------------------------------------------+
// Вспомогательные функции
//--------------------------------------------------------------------+

// Запрет прерываний с сохранением прежнего состояния (можно вызывать и из прерывания)
static inline uint32_t irq_lock(void)        { uint32_t m = __get_PRIMASK(); __disable_irq(); return m; }
static inline void     irq_unlock(uint32_t m) { __set_PRIMASK(m); }

static inline bool ep_ready(uint8_t ep)  { return USB_GetSEPxCTRL(HW(ep)) & USB_SEPx_CTRL_EPRDY_Ready; }
static inline void tx_flush(uint8_t ep)  { USB_SetSEPxTXFDC(HW(ep), 1); }
static inline void rx_flush(uint8_t ep)  { USB_SetSEPxRXFC(HW(ep), 1); }
static inline void toggle_seq(uint8_t ep){ USB_SEPxToggleEPDATASEQ(HW(ep)); }

// Взвести EPRDY: точка готова к следующей транзакции
static void ep_arm(uint8_t ep)
{
    USB_SetSEPxCTRL(HW(ep), USB_SEPx_CTRL_EPRDY_Ready);
    ep_armed[ep] = true;
}

// Была ли транзакция с момента взведения? Если да - снимаем отметку ep_armed
static bool ep_take_done(uint8_t ep)
{
    if (!ep_armed[ep] || ep_ready(ep)) {
        return false;
    }
    ep_armed[ep] = false;
    return true;
}

static void tx_fill(uint8_t ep, const uint8_t* data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        USB_SetSEPxTXFD(HW(ep), data[i]);
    }
}

// Забрать OUT-пакет из RX FIFO в буфер стека и сообщить о завершении.
// Копируем не больше, чем просил стек; остаток уходит вместе со сбросом FIFO.
static void rx_deliver(uint8_t rhport, uint8_t ep)
{
    xfer_t*  x     = &xfer[ep][TUSB_DIR_OUT];
    uint32_t count = USB_GetSEPxRXFDC(HW(ep));
    uint32_t n     = (count < x->len) ? count : x->len;

    if (count > n) {
        dcd_stat.rx_trunc++;
    }
    for (uint32_t i = 0; i < n; i++) {
        x->buf[i] = USB_GetSEPxRXFD(HW(ep));
    }
    rx_flush(ep);

    x->pending = false;
    dcd_event_xfer_complete(rhport, ep, n, XFER_RESULT_SUCCESS, true);
}

// Сбросить программное состояние точки (все поставленные передачи отменяются)
static void ep_reset_state(uint8_t ep)
{
    xfer[ep][TUSB_DIR_IN].pending  = false;
    xfer[ep][TUSB_DIR_OUT].pending = false;
    rx_parked[ep]                  = false;
}

//--------------------------------------------------------------------+
// Инициализация
//--------------------------------------------------------------------+

// Тактирование: ядро 80 МГц от HSE 8 МГц (x10), USB 48 МГц (HSE x6)
static void clock_init(void)
{
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

    USB_Clock_TypeDef usb_clk = {
        .USB_USBC1_Source = USB_C1HSEdiv1,
        .USB_PLLUSBMUL    = USB_PLLUSBMUL6,
    };
    USB_BRGInit(&usb_clk);
}

bool dcd_init(uint8_t rhport, const tusb_rhport_init_t* rh_init)
{
    (void)rhport;
    (void)rh_init;

    for (uint8_t ep = 0; ep < EP_COUNT; ep++) {
        ep_reset_state(ep);
        ep_armed[ep] = false;
    }

    clock_init();
    USB_Reset();

    // Режим device, Full Speed 12 Мбит/с, подтяжка D+ (хост видит подключение)
    USB_SetHSCR(USB_HSCR_HOST_MODE_Device | USB_HSCR_EN_RX_Set |
                USB_HSCR_EN_TX_Set | USB_HSCR_DP_PULLUP_Set);
    USB_SetSC(USB_SC_SCGEN_Set | USB_SC_SCFSP_Full | USB_SC_SCFSR_12Mb);

    // EP0 включена и ждёт SETUP
    USB_SetSEPxCTRL(USB_EP0, USB_SEPx_CTRL_EPEN_Enable | USB_SEPx_CTRL_EPRDY_Ready);
    ep_armed[0] = true;

    USB_SetSIM(USB_SIM_SCTDONEIE_Set | USB_SIM_SCRESETEVIE_Set);
    return true;
}

void dcd_int_enable(uint8_t rhport)  { (void)rhport; NVIC_EnableIRQ(USB_IRQn); }
void dcd_int_disable(uint8_t rhport) { (void)rhport; NVIC_DisableIRQ(USB_IRQn); }

// Адрес нельзя применять сразу: статус SET_ADDRESS идёт ещё на старом адресе.
// Применяется в ep0_service() после ACK статусного пакета.
void dcd_set_address(uint8_t rhport, uint8_t dev_addr)
{
    (void)rhport;
    new_address = dev_addr;
}

// Не поддерживается контроллером / не требуется
void dcd_remote_wakeup(uint8_t rhport)          { (void)rhport; }
void dcd_connect(uint8_t rhport)                { (void)rhport; }
void dcd_disconnect(uint8_t rhport)             { (void)rhport; }
void dcd_sof_enable(uint8_t rhport, bool enable){ (void)rhport; (void)enable; }

//--------------------------------------------------------------------+
// Открытие / закрытие / STALL
//--------------------------------------------------------------------+

bool dcd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const* desc)
{
    (void)rhport;
    uint8_t ep  = tu_edpt_number(desc->bEndpointAddress);
    uint8_t dir = tu_edpt_dir(desc->bEndpointAddress);

    if (ep >= EP_COUNT) {
        return false;
    }

    uint32_t m = irq_lock();

    USB_SetSEPxCTRL(HW(ep), USB_SEPx_CTRL_EPEN_Enable | USB_SEPx_CTRL_EPDATASEQ_Data0 |
                            USB_SEPx_CTRL_EPSSTALL_NotReply | USB_SEPx_CTRL_EPISOEN_Reset |
                            USB_SEPx_CTRL_EPRDY_Ready);
    rx_flush(ep);
    tx_flush(ep);

    ep_armed[ep]          = true;
    rx_parked[ep]         = false;
    xfer[ep][dir].pending = false;

    irq_unlock(m);
    return true;
}

void dcd_edpt_close(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;
    uint8_t ep = tu_edpt_number(ep_addr);

    if (ep < EP_COUNT) {
        xfer[ep][tu_edpt_dir(ep_addr)].pending = false;
    }
}

// Железо не трогаем - dcd_edpt_open() всё равно перенастроит точки
void dcd_edpt_close_all(uint8_t rhport)
{
    (void)rhport;
    for (uint8_t ep = 1; ep < EP_COUNT; ep++) {
        ep_reset_state(ep);
    }
}

void dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;
    uint8_t ep = tu_edpt_number(ep_addr);
    if (ep >= EP_COUNT) {
        return;
    }

    uint32_t m = irq_lock();

    ep_reset_state(ep);   // TinyUSB требует снять все передачи точки
    tx_flush(ep);
    rx_flush(ep);
    USB_SetSEPxCTRL(HW(ep), USB_SEPx_CTRL_EPSSTALL_Reply | USB_SEPx_CTRL_EPDATASEQ_Data0 |
                            USB_SEPx_CTRL_EPRDY_Ready);
    ep_armed[ep] = true;

    irq_unlock(m);
}

void dcd_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;
    uint8_t ep = tu_edpt_number(ep_addr);
    if (ep >= EP_COUNT) {
        return;
    }

    uint32_t m = irq_lock();

    rx_parked[ep] = false;
    tx_flush(ep);
    rx_flush(ep);
    USB_SetSEPxCTRL(HW(ep), USB_SEPx_CTRL_EPSSTALL_NotReply | USB_SEPx_CTRL_EPDATASEQ_Data0 |
                            USB_SEPx_CTRL_EPRDY_Ready);
    ep_armed[ep] = true;

    irq_unlock(m);
}

//--------------------------------------------------------------------+
// Обработка завершённых транзакций
//--------------------------------------------------------------------+

// EP1..EP3. Вызывается из прерывания и из dcd_edpt_xfer (прерывания запрещены)
static void ep_service(uint8_t rhport, uint8_t ep)
{
    if (!ep_take_done(ep)) {
        return;   // транзакции не было
    }

    uint32_t sts  = USB_GetSEPxSTS(HW(ep));
    uint32_t type = USB_GetSEPxTS(HW(ep)) & USB_SEPx_TS_SCTTYPE_Msk;
    xfer_t*  in   = &xfer[ep][TUSB_DIR_IN];
    xfer_t*  out  = &xfer[ep][TUSB_DIR_OUT];

    if (sts & USB_SEPx_STS_SCSTALLSENT_Set) {
        // Хосту ушёл STALL: снимаем передачи и возвращаем точку в работу
        dcd_stat.stall_sent++;
        in->pending  = false;
        out->pending = false;
        toggle_seq(ep);
        ep_arm(ep);
        return;
    }

    if (type == USB_SEPx_TS_SCTTYPE_In && (sts & USB_SEPx_STS_SCACKRXED_Set)) {
        // Хост подтвердил IN-пакет
        tx_flush(ep);
        toggle_seq(ep);
        if (in->pending) {
            in->pending = false;
            dcd_event_xfer_complete(rhport, ep | TUSB_DIR_IN_MASK, in->len, XFER_RESULT_SUCCESS, true);
        } else {
            dcd_stat.zlp_ack++;   // это был ZLP, отправленный контроллером самостоятельно
        }
    } else if (type == USB_SEPx_TS_SCTTYPE_Outdata) {
        // Пришёл OUT-пакет
        if (out->pending) {
            rx_deliver(rhport, ep);
        } else {
            rx_parked[ep] = true;   // буфера нет - оставляем пакет в FIFO
            dcd_stat.rx_parked++;
        }
    }

    // Взводим снова, только если стеку ещё есть что делать и RX FIFO свободен
    if (!rx_parked[ep] && (in->pending || out->pending)) {
        ep_arm(ep);
    }
}

// EP0 (control)
static void ep0_service(uint8_t rhport)
{
    if (!ep_take_done(0)) {
        return;
    }

    uint32_t sts  = USB_GetSEPxSTS(USB_EP0);
    uint32_t type = USB_GetSEPxTS(USB_EP0) & USB_SEPx_TS_SCTTYPE_Msk;

    if (sts & USB_SEPx_STS_SCSTALLSENT_Set) {
        dcd_stat.stall_sent++;
        toggle_seq(0);
    } else if (type == USB_SEPx_TS_SCTTYPE_Setup) {
        // SETUP всегда DATA0, ответ на него начинается с DATA1
        USB_SetSEPxCTRL(USB_EP0, USB_SEPx_CTRL_EPDATASEQ_Data0);

        uint8_t setup[8];
        for (int i = 0; i < 8; i++) {
            setup[i] = USB_GetSEPxRXFD(USB_EP0);
        }
        rx_flush(0);
        toggle_seq(0);
        dcd_event_setup_received(rhport, setup, true);

        // Ответ на SETUP должен лечь в FIFO до взведения EP0 ниже,
        // иначе на первый IN-токен контроллер отправит пустой пакет
        tud_task();
    } else if (type == USB_SEPx_TS_SCTTYPE_In && (sts & USB_SEPx_STS_SCACKRXED_Set)) {
        dcd_event_xfer_complete(rhport, TUSB_DIR_IN_MASK, xfer[0][TUSB_DIR_IN].len, XFER_RESULT_SUCCESS, true);
        tx_flush(0);
        if (new_address) {
            USB_SetSA(new_address);   // статус SET_ADDRESS отправлен - можно менять адрес
            new_address = 0;
        }
        toggle_seq(0);
    } else if (type == USB_SEPx_TS_SCTTYPE_Outdata) {
        rx_deliver(rhport, 0);
    }

    // EP0 всегда взведена: она должна принять следующий SETUP
    ep_arm(0);
}

//--------------------------------------------------------------------+
// Постановка передачи
//--------------------------------------------------------------------+

static void ep0_xfer(uint8_t dir, uint8_t* buf, uint16_t len)
{
    xfer[0][dir].buf = buf;
    xfer[0][dir].len = len;

    if (dir == TUSB_DIR_IN) {
        tx_flush(0);
        tx_fill(0, buf, len);   // len = 0 -> уйдёт ZLP
    } else {
        rx_flush(0);
    }
    ep_arm(0);
}

static void epx_xfer(uint8_t rhport, uint8_t ep, uint8_t dir, uint8_t* buf, uint16_t len)
{
    // Транзакция могла завершиться, а прерывание ещё не успело - обрабатываем её здесь (правило 4)
    if (ep_armed[ep] && !ep_ready(ep)) {
        dcd_stat.svc_in_xfer++;
        ep_service(rhport, ep);
    }

    xfer_t* x  = &xfer[ep][dir];
    x->buf     = buf;
    x->len     = len;
    x->pending = true;

    if (dir == TUSB_DIR_IN) {
        // Если точка уже взведена (ждёт OUT), TX FIFO и так пуст
        if (!ep_armed[ep]) {
            tx_flush(ep);
        }
        tx_fill(ep, buf, len);

        // При припаркованном OUT точку взведёт следующий dcd_edpt_xfer(OUT)
        if (!ep_armed[ep] && !rx_parked[ep]) {
            ep_arm(ep);
        }
        return;
    }

    if (rx_parked[ep]) {
        // Пакет уже ждёт в RX FIFO - отдаём сразу (правило 3)
        rx_parked[ep] = false;
        dcd_stat.rx_delivered++;
        rx_deliver(rhport, ep);

        // FIFO освободился - может стартовать отложенный IN
        if (xfer[ep][TUSB_DIR_IN].pending && !ep_armed[ep]) {
            ep_arm(ep);
        }
    } else if (!ep_armed[ep]) {
        rx_flush(ep);
        ep_arm(ep);
    }
}

bool dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t* buffer, uint16_t total_bytes, bool is_isr)
{
    (void)is_isr;   // защита от гонок - запретом прерываний, работает в любом контексте
    uint8_t ep  = tu_edpt_number(ep_addr);
    uint8_t dir = tu_edpt_dir(ep_addr);

    if (ep >= EP_COUNT) {
        return false;
    }
    if (dir == TUSB_DIR_IN && total_bytes > EP_SIZE) {
        return false;   // больше одного пакета в TX FIFO не помещается
    }

    uint32_t m = irq_lock();
    if (ep == 0) {
        ep0_xfer(dir, buffer, total_bytes);
    } else {
        epx_xfer(rhport, ep, dir, buffer, total_bytes);
    }
    irq_unlock(m);

    return true;
}

//--------------------------------------------------------------------+
// Прерывание
//--------------------------------------------------------------------+

static void bus_reset(uint8_t rhport)
{
    new_address = 0;
    USB_SetSA(0);

    for (uint8_t ep = 0; ep < EP_COUNT; ep++) {
        ep_reset_state(ep);
        tx_flush(ep);
        rx_flush(ep);

        // Выключить и снова включить точку: DATA0, без STALL, готова
        USB_SetSEPxCTRL(HW(ep), USB_SEPx_CTRL_EPEN_Disable | USB_SEPx_CTRL_EPRDY_NotReady |
                                USB_SEPx_CTRL_EPDATASEQ_Data0 | USB_SEPx_CTRL_EPSSTALL_NotReply |
                                USB_SEPx_CTRL_EPISOEN_Reset);
        USB_SetSEPxCTRL(HW(ep), USB_SEPx_CTRL_EPEN_Enable | USB_SEPx_CTRL_EPRDY_Ready |
                                USB_SEPx_CTRL_EPDATASEQ_Data0 | USB_SEPx_CTRL_EPSSTALL_NotReply |
                                USB_SEPx_CTRL_EPISOEN_Reset);
        ep_armed[ep] = true;
    }

    dcd_event_bus_reset(rhport, TUSB_SPEED_FULL, true);
}

static uint32_t sis;   // флаги прерывания, прочитанные в USB_IRQHandler

void dcd_int_handler(uint8_t rhport)
{
    if (sis & USB_SIS_SCRESETEV_Set) {
        bus_reset(rhport);
        return;
    }
    if (sis & USB_SIS_SCRESUME_Set) {
        dcd_event_bus_signal(rhport, DCD_EVENT_RESUME, true);
    }
    if (sis & USB_SIS_SCTDONE_Set) {
        // Флаг SCTDONE один на все точки - проверяем каждую
        ep0_service(rhport);
        for (uint8_t ep = 1; ep < EP_COUNT; ep++) {
            ep_service(rhport, ep);
        }
    }
}

void USB_IRQHandler(void)
{
    // Сбрасываем прочитанные флаги ДО обработки: событие, пришедшее во время
    // обработки, снова поднимет флаг и не потеряется
    sis = USB_GetSIS();
    USB_SetSIS(sis);
    dcd_int_handler(0);
}

// Без RTOS TinyUSB требует эту функцию от приложения. Таймауты стеку не нужны - возвращаем 0
uint32_t tusb_time_millis_api(void)
{
    return 0;
}
