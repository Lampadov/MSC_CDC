#include <MDR32FxQI_port.h>
#include <MDR32FxQI_rst_clk.h>

#include "tusb.h"

/*
 * HID-устройство на кнопках платы. Что это будет - мышь или клавиатура - задаёт HID_MODE
 * в tusb_config.h (или в Keil: Options for Target -> C/C++ -> Define: HID_MODE=2).
 *
 *             HID_MODE_MOUSE                         HID_MODE_KEYBOARD
 *   SELECT    левая кнопка мыши                      Enter
 *   стрелки   курсор движется, пока кнопка           стрелки
 *             удержана (скорость растёт)
 *   UP+DOWN   демо: курсор рисует окружность         демо: плата сама набирает текст
 *             (в Paint с зажатой левой кнопкой)
 *
 * Светодиоды: VD3 горит, когда хост настроил устройство; VD4 - пока нажата любая кнопка.
 */

#define VD3  PORT_Pin_0
#define VD4  PORT_Pin_1

#define DEBOUNCE_MS      5     // сколько мс уровень кнопки должен быть стабильным
#define REPORT_PERIOD_MS 10    // как часто отправляем отчёты (движение, демо)

// Мышь: скорость курсора растёт с длительностью удержания
#define SPEED_START      2     // пикселей за отчёт в начале
#define SPEED_MAX        20    // пикселей за отчёт максимум
#define SPEED_ACCEL_MS   100   // каждые столько мс удержания скорость растёт на 1

// Клавиатура: этот текст набирается в демо (раскладка на ПК должна быть английская)
#define DEMO_TEXT        "Hello from Milandr!"

enum { BTN_SELECT, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_COUNT };

static const struct {
    MDR_PORT_TypeDef* port;
    uint16_t          pin;
} buttons[BTN_COUNT] = {
    {MDR_PORTC, PORT_Pin_2},   // SELECT
    {MDR_PORTB, PORT_Pin_5},   // UP
    {MDR_PORTE, PORT_Pin_1},   // DOWN
    {MDR_PORTE, PORT_Pin_3},   // LEFT
    {MDR_PORTB, PORT_Pin_6}    // RIGHT
};

#define BIT(b)  (1u << (b))

extern void     SystemCoreClockUpdate(void);
extern uint32_t SystemCoreClock;

//--------------------------------------------------------------------+
// Время и кнопки
//--------------------------------------------------------------------+
static volatile uint32_t ms_ticks;       // миллисекунды с момента запуска

void SysTick_Handler(void)
{
    ms_ticks++;
}

static uint8_t buttons_state;            // маска нажатых кнопок (бит = номер BTN_xxx)

// Опрос кнопок раз в миллисекунду с подавлением дребезга + светодиоды
static void buttons_scan(void)
{
    static uint8_t integrator[BTN_COUNT];

    for (int i = 0; i < BTN_COUNT; i++) {
        if (PORT_ReadInputDataBit(buttons[i].port, buttons[i].pin) == 0) {
            if (integrator[i] < DEBOUNCE_MS) integrator[i]++;
        } else if (integrator[i] > 0) {
            integrator[i]--;
        }
        if (integrator[i] == DEBOUNCE_MS) buttons_state |= BIT(i);
        if (integrator[i] == 0)           buttons_state &= ~BIT(i);
    }

    if (tud_mounted())  PORT_SetBits(MDR_PORTC, VD3); else PORT_ResetBits(MDR_PORTC, VD3);
    if (buttons_state)  PORT_SetBits(MDR_PORTC, VD4); else PORT_ResetBits(MDR_PORTC, VD4);
}

// Пауза с обслуживанием USB (для демо)
static void wait_ms(uint32_t ms)
{
    uint32_t start = ms_ticks;
    while (ms_ticks - start < ms) tud_task();
}

// Ждём готовности конечной точки (для демо)
static void wait_ready(void)
{
    while (!tud_hid_ready()) tud_task();
}

// Демо запускается одновременным нажатием UP и DOWN (по фронту)
static bool demo_requested(void)
{
    static bool was_pressed;
    bool pressed = (buttons_state & (BIT(BTN_UP) | BIT(BTN_DOWN))) == (BIT(BTN_UP) | BIT(BTN_DOWN));
    bool start   = pressed && !was_pressed;

    was_pressed = pressed;
    return start;
}

#if HID_MODE == HID_MODE_MOUSE
//--------------------------------------------------------------------+
// Мышь
//--------------------------------------------------------------------+
// Окружность радиусом 60 пикселей из 36 шагов (dx, dy); сумма шагов равна нулю,
// поэтому курсор возвращается в исходную точку
static const int8_t circle[36][2] = {
    {-1,10},  {-3,11},  {-4,9},   {-6,9},   {-7,7},   {-9,6},
    {-9,4},   {-11,3},  {-10,1},  {-10,-1}, {-11,-3}, {-9,-4},
    {-9,-6},  {-7,-7},  {-6,-9},  {-4,-9},  {-3,-11}, {-1,-10},
    {1,-10},  {3,-11},  {4,-9},   {6,-9},   {7,-7},   {9,-6},
    {9,-4},   {11,-3},  {10,-1},  {10,1},   {11,3},   {9,4},
    {9,6},    {7,7},    {6,9},    {4,9},    {3,11},   {1,10}
};

// Демо: нажать левую кнопку, обойти окружность, отпустить кнопку
static void mouse_demo(void)
{
    if (!tud_mounted()) return;

    for (int step = -1; step <= 36; step++) {      // -1: нажатие, 0..35: окружность, 36: отпускание
        bool    drawing = (step < 36);
        uint8_t buttons = drawing ? MOUSE_BUTTON_LEFT : 0;
        int8_t  dx = (step >= 0 && drawing) ? circle[step][0] : 0;
        int8_t  dy = (step >= 0 && drawing) ? circle[step][1] : 0;

        wait_ready();
        tud_hid_mouse_report(0, buttons, dx, dy, 0, 0);
        wait_ms(REPORT_PERIOD_MS);
    }
}

static void hid_task(void)
{
    static uint32_t last_report_ms;      // когда ушёл последний отчёт
    static uint32_t move_start_ms;       // когда началось текущее удержание стрелок
    static uint8_t  sent_buttons;        // состояние кнопок мыши, уже отправленное хосту
    static bool     was_moving;

    uint32_t now = ms_ticks;

    if (demo_requested()) {
        mouse_demo();
        return;
    }

    // Курсор: скорость растёт, пока удерживается любая стрелка
    bool moving = buttons_state & (BIT(BTN_UP) | BIT(BTN_DOWN) | BIT(BTN_LEFT) | BIT(BTN_RIGHT));
    if (moving && !was_moving) move_start_ms = now;
    was_moving = moving;

    int speed = SPEED_START + (int)((now - move_start_ms) / SPEED_ACCEL_MS);
    if (speed > SPEED_MAX) speed = SPEED_MAX;

    int8_t dx = 0, dy = 0;
    if (buttons_state & BIT(BTN_RIGHT)) dx += speed;
    if (buttons_state & BIT(BTN_LEFT))  dx -= speed;
    if (buttons_state & BIT(BTN_DOWN))  dy += speed;
    if (buttons_state & BIT(BTN_UP))    dy -= speed;

    uint8_t buttons = (buttons_state & BIT(BTN_SELECT)) ? MOUSE_BUTTON_LEFT : 0;

    // Отчёт уходит при смене кнопок мыши или по таймеру, пока курсор должен двигаться
    bool changed = (buttons != sent_buttons);
    bool timer   = (dx || dy) && (now - last_report_ms >= REPORT_PERIOD_MS);

    if ((changed || timer) && tud_hid_ready()) {
        if (tud_hid_mouse_report(0, buttons, dx, dy, 0, 0)) {
            sent_buttons   = buttons;
            last_report_ms = now;
        }
    }
}

#else
//--------------------------------------------------------------------+
// Клавиатура
//--------------------------------------------------------------------+
static const uint8_t key_codes[BTN_COUNT] = {
    HID_KEY_ENTER,         // SELECT
    HID_KEY_ARROW_UP,      // UP
    HID_KEY_ARROW_DOWN,    // DOWN
    HID_KEY_ARROW_LEFT,    // LEFT
    HID_KEY_ARROW_RIGHT    // RIGHT
};

// Демо: набрать текст. На каждый символ - отчёт "клавиша нажата" и отчёт "отпущена".
static void keyboard_demo(void)
{
    static const uint8_t ascii_to_key[128][2] = { HID_ASCII_TO_KEYCODE };   // {нужен Shift, код клавиши}

    if (!tud_mounted()) return;

    for (const char* c = DEMO_TEXT; *c; c++) {
        uint8_t keycodes[6] = { ascii_to_key[(uint8_t)*c][1] };
        uint8_t shift       = ascii_to_key[(uint8_t)*c][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;

        for (int press = 1; press >= 0; press--) {      // нажать, затем отпустить
            wait_ready();
            tud_hid_keyboard_report(0, press ? shift : 0, press ? keycodes : NULL);
            wait_ms(REPORT_PERIOD_MS);
        }
    }
}

static void hid_task(void)
{
    static uint8_t sent_state;           // набор кнопок, который уже ушёл хосту

    if (demo_requested()) {
        keyboard_demo();
        sent_state = buttons_state;      // хост видит "всё отпущено"; удержание UP+DOWN после демо ничего не шлёт
        return;
    }

    // Отчёт уходит только при изменении набора нажатых клавиш; автоповтор делает сам хост
    if (buttons_state == sent_state || !tud_hid_ready()) return;

    uint8_t keycodes[6] = {0};
    int     n = 0;
    for (int i = 0; i < BTN_COUNT; i++) {
        if (buttons_state & BIT(i)) keycodes[n++] = key_codes[i];
    }
    if (tud_hid_keyboard_report(0, 0, keycodes)) {
        sent_state = buttons_state;
    }
}
#endif

//--------------------------------------------------------------------+
// Обязательные обратные вызовы HID (нам от хоста ничего не нужно)
//--------------------------------------------------------------------+
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t* buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
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

    gpio.PORT_Pin = VD3 | VD4;           // светодиоды - выходы
    gpio.PORT_OE  = PORT_OE_OUT;
    PORT_Init(MDR_PORTC, &gpio);

    gpio.PORT_OE  = PORT_OE_IN;          // кнопки - входы
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
    tusb_init(0, &dev_init);             // здесь же настраивается PLL

    SystemCoreClockUpdate();             // тактовая частота известна только после tusb_init
    SysTick_Config(SystemCoreClock / 1000);

    uint32_t last_ms = 0;
    while (1)
    {
        tud_task();                      // обработка USB-стека

        if (ms_ticks != last_ms) {       // кнопки и отчёты - раз в миллисекунду
            last_ms = ms_ticks;
            buttons_scan();
            hid_task();
        }
    }
}
