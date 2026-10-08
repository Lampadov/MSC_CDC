#include <MDR32FxQI_port.h>
#include <MDR32FxQI_rst_clk.h>

#include "tusb.h"

/*
 * USB-мышь на кнопках платы.
 *   SELECT       - левая кнопка мыши
 *   UP/DOWN/LEFT/RIGHT - движение курсора; пока кнопка удержана, курсор едет
 *                  непрерывно и постепенно ускоряется
 *
 * Светодиоды: VD3 горит, когда хост настроил устройство; VD4 горит, пока нажата любая кнопка.
 */

// ---- Настройки поведения ----
#define DEBOUNCE_MS       5     // кнопка считается нажатой/отпущенной после стольких мс стабильного уровня
#define REPORT_PERIOD_MS  10    // период отчётов о движении, пока кнопка удержана
#define SPEED_START       2     // начальная скорость, пикселей за отчёт
#define SPEED_MAX         20    // максимальная скорость, пикселей за отчёт
#define SPEED_ACCEL_MS    100   // каждые столько мс удержания скорость растёт на 1

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
// Кнопки и отчёты HID
//--------------------------------------------------------------------+

static void hid_task(void)
{
    static uint32_t last_ms;          // последняя обработанная миллисекунда
    static uint32_t last_report_ms;   // когда ушёл последний отчёт
    static uint32_t move_start_ms;    // когда началось текущее удержание
    static uint8_t  integrator[BTN_COUNT];
    static uint8_t  state;            // кнопки после подавления дребезга (битовая маска)
    static uint8_t  sent_buttons;     // состояние кнопок мыши, которое уже ушло хосту
    static bool     was_moving;       // шло ли движение в прошлую миллисекунду

    uint32_t now = ms_ticks;
    if (now == last_ms) {
        return;                       // всё ниже выполняется раз в миллисекунду
    }
    last_ms = now;

    // Подавление дребезга: счётчик растёт, пока кнопка нажата (низкий уровень), и падает, пока отпущена
    for (uint8_t i = 0; i < BTN_COUNT; i++) {
        if (PORT_ReadInputDataBit(buttons[i].port, buttons[i].pin) == 0) {
            if (integrator[i] < DEBOUNCE_MS) integrator[i]++;
        } else if (integrator[i] > 0) {
            integrator[i]--;
        }
        if (integrator[i] == DEBOUNCE_MS)  state |= BIT(i);
        else if (integrator[i] == 0)       state &= (uint8_t)~BIT(i);
    }

    // Светодиоды
    if (tud_mounted()) PORT_SetBits(MDR_PORTC, VD3); else PORT_ResetBits(MDR_PORTC, VD3);
    if (state)         PORT_SetBits(MDR_PORTC, VD4); else PORT_ResetBits(MDR_PORTC, VD4);

    // Скорость курсора растёт с длительностью удержания
    uint8_t move_mask = BIT(BTN_UP) | BIT(BTN_DOWN) | BIT(BTN_LEFT) | BIT(BTN_RIGHT);
    bool moving = (state & move_mask) != 0;
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
// Обязательные обратные вызовы HID
//--------------------------------------------------------------------+

// Хост передал отчёт устройству (для мыши не используется)
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
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
