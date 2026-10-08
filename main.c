#include <string.h>

#include <MDR32FxQI_port.h>
#include <MDR32FxQI_rst_clk.h>

#include "tusb.h"

/*
 * HID-устройство на кнопках платы. Режим выбирается макросом HID_MODE в tusb_config.h
 * (или в Keil: Options for Target -> C/C++ -> Define: HID_MODE=2).
 *
 *   HID_MODE_MOUSE    SELECT - левая кнопка мыши; UP/DOWN/LEFT/RIGHT двигают курсор, пока кнопка
 *                     удержана (непрерывно и с ускорением)
 *   HID_MODE_KEYBOARD SELECT - Enter; UP/DOWN/LEFT/RIGHT - стрелки
 *   HID_MODE_ECHO     тест: всё, что хост отправил в отчёте (64 байта), возвращается ему назад
 *                     (стресс-тест tools/hid_stress.py)
 *
 * Светодиоды: VD3 горит, когда хост настроил устройство; VD4 горит, пока нажата любая кнопка.
 */

// ---- Настройки поведения ----
#define DEBOUNCE_MS       5     // кнопка считается нажатой/отпущенной после стольких мс стабильного уровня
#define REPORT_PERIOD_MS  10    // (мышь) период отчётов о движении, пока кнопка удержана
#define SPEED_START       2     // (мышь) начальная скорость, пикселей за отчёт
#define SPEED_MAX         20    // (мышь) максимальная скорость, пикселей за отчёт
#define SPEED_ACCEL_MS    100   // (мышь) каждые столько мс удержания скорость растёт на 1

#define VD3     PORT_Pin_0
#define VD4     PORT_Pin_1
#define LED_ALL (VD3 | VD4)

typedef struct {
    MDR_PORT_TypeDef* port;
    uint16_t          pin;
} button_t;

enum { BTN_SELECT, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_COUNT };

static const button_t buttons[BTN_COUNT] = {
    {MDR_PORTC, PORT_Pin_2},   // SELECT
    {MDR_PORTB, PORT_Pin_5},   // UP
    {MDR_PORTE, PORT_Pin_1},   // DOWN
    {MDR_PORTE, PORT_Pin_3},   // LEFT
    {MDR_PORTB, PORT_Pin_6}    // RIGHT
};

#define BIT(b)  (1u << (b))

extern void     SystemCoreClockUpdate(void);
extern uint32_t SystemCoreClock;

static void hid_task(void);

//--------------------------------------------------------------------+
// Время в миллисекундах (SysTick)
//--------------------------------------------------------------------+

static volatile uint32_t ms_ticks;

void SysTick_Handler(void)
{
    ms_ticks++;
}

//--------------------------------------------------------------------+
// main
//--------------------------------------------------------------------+

static void gpio_init(void)
{
    PORT_InitTypeDef gpio;

    RST_CLK_PCLKcmd(RST_CLK_PCLK_PORTC | RST_CLK_PCLK_PORTB | RST_CLK_PCLK_PORTE, ENABLE);
    PORT_StructInit(&gpio);
    gpio.PORT_SPEED = PORT_SPEED_SLOW;
    gpio.PORT_MODE  = PORT_MODE_DIGITAL;

    // Светодиоды - выходы
    gpio.PORT_Pin = LED_ALL;
    gpio.PORT_OE  = PORT_OE_OUT;
    PORT_Init(MDR_PORTC, &gpio);

    // Кнопки - входы
    gpio.PORT_OE = PORT_OE_IN;
    gpio.PORT_Pin = PORT_Pin_2;
    PORT_Init(MDR_PORTC, &gpio);
    gpio.PORT_Pin = PORT_Pin_5 | PORT_Pin_6;
    PORT_Init(MDR_PORTB, &gpio);
    gpio.PORT_Pin = PORT_Pin_1 | PORT_Pin_3;
    PORT_Init(MDR_PORTE, &gpio);
}

int main(void)
{
    gpio_init();

    tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    tusb_init(0, &dev_init);   // здесь же настраивается PLL

    // Тактовая частота известна только после tusb_init: считаем по регистрам и запускаем SysTick на 1 кГц
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);

    while (1)
    {
        tud_task();   // обработка USB-стека
        hid_task();
    }
}

//--------------------------------------------------------------------+
// Кнопки: опрос с подавлением дребезга, светодиоды
//--------------------------------------------------------------------+

// Возвращает маску нажатых кнопок (бит BTN_xxx). Вызывать не чаще раза в миллисекунду.
static uint8_t buttons_scan(void)
{
    static uint8_t integrator[BTN_COUNT];
    static uint8_t state;

    // Счётчик растёт, пока кнопка нажата (низкий уровень), и падает, пока отпущена
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        if (PORT_ReadInputDataBit(buttons[i].port, buttons[i].pin) == 0) {
            if (integrator[i] < DEBOUNCE_MS) integrator[i]++;
        } else if (integrator[i] > 0) {
            integrator[i]--;
        }
        if (integrator[i] == DEBOUNCE_MS)  state |= BIT(i);
        else if (integrator[i] == 0)       state &= (uint8_t)~BIT(i);
    }

    if (tud_mounted()) PORT_SetBits(MDR_PORTC, VD3); else PORT_ResetBits(MDR_PORTC, VD3);
    if (state)         PORT_SetBits(MDR_PORTC, VD4); else PORT_ResetBits(MDR_PORTC, VD4);

    return state;
}

//--------------------------------------------------------------------+
// Режим MOUSE
//--------------------------------------------------------------------+
#if HID_MODE == HID_MODE_MOUSE

static void mouse_send(uint8_t state, uint32_t now)
{
    static uint32_t last_report_ms;   // когда ушёл последний отчёт
    static uint32_t move_start_ms;    // когда началось текущее удержание
    static uint8_t  sent_buttons;     // состояние кнопок мыши, которое уже ушло хосту
    static bool     was_moving;       // шло ли движение в прошлую миллисекунду

    // Скорость курсора растёт с длительностью удержания
    bool moving = (state & (BIT(BTN_UP) | BIT(BTN_DOWN) | BIT(BTN_LEFT) | BIT(BTN_RIGHT))) != 0;
    if (moving && !was_moving) {
        move_start_ms = now;
    }
    was_moving = moving;

    int speed = SPEED_START + (int)((now - move_start_ms) / SPEED_ACCEL_MS);
    if (speed > SPEED_MAX) speed = SPEED_MAX;

    int8_t dx = 0, dy = 0;
    if (state & BIT(BTN_RIGHT)) dx += speed;
    if (state & BIT(BTN_LEFT))  dx -= speed;
    if (state & BIT(BTN_DOWN))  dy += speed;
    if (state & BIT(BTN_UP))    dy -= speed;

    uint8_t mouse_buttons  = (state & BIT(BTN_SELECT)) ? MOUSE_BUTTON_LEFT : 0;
    bool    buttons_change = (mouse_buttons != sent_buttons);
    bool    time_to_move   = (dx || dy) && (now - last_report_ms >= REPORT_PERIOD_MS);

    // Отчёт уходит при смене кнопок мыши или по таймеру, пока курсор должен двигаться
    if ((buttons_change || time_to_move) && tud_hid_ready()) {
        if (tud_hid_mouse_report(0, mouse_buttons, dx, dy, 0, 0)) {
            sent_buttons   = mouse_buttons;
            last_report_ms = now;
        }
    }
}

//--------------------------------------------------------------------+
// Режим KEYBOARD
//--------------------------------------------------------------------+
#elif HID_MODE == HID_MODE_KEYBOARD

static const uint8_t key_codes[BTN_COUNT] = {
    HID_KEY_ENTER,        // SELECT
    HID_KEY_ARROW_UP,     // UP
    HID_KEY_ARROW_DOWN,   // DOWN
    HID_KEY_ARROW_LEFT,   // LEFT
    HID_KEY_ARROW_RIGHT   // RIGHT
};

static void keyboard_send(uint8_t state)
{
    static uint8_t sent_state;   // набор клавиш, который уже ушёл хосту

    // Отчёт уходит только при изменении набора нажатых клавиш; автоповтор делает сам хост
    if (state == sent_state || !tud_hid_ready()) {
        return;
    }

    uint8_t keycodes[6] = {0};
    uint8_t n = 0;
    for (uint8_t i = 0; i < BTN_COUNT && n < 6; i++) {
        if (state & BIT(i)) {
            keycodes[n++] = key_codes[i];
        }
    }

    if (tud_hid_keyboard_report(0, 0, keycodes)) {   // все нули = "ничего не нажато"
        sent_state = state;
    }
}

//--------------------------------------------------------------------+
// Режим ECHO (тест)
//--------------------------------------------------------------------+
#elif HID_MODE == HID_MODE_ECHO

#define ECHO_QUEUE  8                         // сколько принятых отчётов можно держать в очереди

static uint8_t           echo_buf[ECHO_QUEUE][CFG_TUD_HID_EP_BUFSIZE];
static uint16_t          echo_len[ECHO_QUEUE];
static volatile uint8_t  echo_head, echo_tail;
volatile uint32_t        echo_dropped;        // отчёты, не поместившиеся в очередь (для отладки в Keil)
volatile uint32_t        echo_count;          // сколько отчётов возвращено хосту

static void echo_task(void)
{
    if (echo_tail != echo_head && tud_hid_ready()) {
        if (tud_hid_report(0, echo_buf[echo_tail], echo_len[echo_tail])) {
            echo_tail = (uint8_t)((echo_tail + 1) % ECHO_QUEUE);
            echo_count++;
        }
    }
}

#endif

//--------------------------------------------------------------------+
// Задача HID
//--------------------------------------------------------------------+

static void hid_task(void)
{
#if HID_MODE == HID_MODE_ECHO
    echo_task();
#else
    static uint32_t last_ms;
    uint32_t now = ms_ticks;
    if (now == last_ms) {
        return;                       // всё ниже выполняется раз в миллисекунду
    }
    last_ms = now;

    uint8_t state = buttons_scan();
  #if HID_MODE == HID_MODE_KEYBOARD
    keyboard_send(state);
  #else
    mouse_send(state, now);
  #endif
#endif
}

//--------------------------------------------------------------------+
// Обязательные обратные вызовы HID
//--------------------------------------------------------------------+

// Хост передал отчёт устройству
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;

#if HID_MODE == HID_MODE_ECHO
    uint8_t next = (uint8_t)((echo_head + 1) % ECHO_QUEUE);
    if (next == echo_tail) {
        echo_dropped++;               // очередь полна
        return;
    }
    if (bufsize > CFG_TUD_HID_EP_BUFSIZE) bufsize = CFG_TUD_HID_EP_BUFSIZE;
    memcpy(echo_buf[echo_head], buffer, bufsize);
    echo_len[echo_head] = bufsize;
    echo_head = next;
#else
    (void)buffer;                     // мыши/клавиатуре отчёты от хоста не нужны (кроме индикаторов Num/Caps Lock)
    (void)bufsize;
#endif
}

// Хост запросил отчёт через управляющую точку; 0 - нечего отдавать
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t* buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}
