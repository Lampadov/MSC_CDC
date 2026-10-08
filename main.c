#include <MDR32FxQI_port.h>
#include <MDR32FxQI_rst_clk.h>

#include "tusb.h"

/*
 * Составное устройство: USB-флешка + клавиатура.
 *
 * 1. Откройте диск, отредактируйте TEXT.TXT и сохраните.
 * 2. Поставьте курсор в любое текстовое поле на ПК.
 * 3. Нажмите SELECT на плате - плата сама "наберёт" текст файла.
 *
 * Светодиоды: VD3 горит, когда хост настроил устройство; VD4 - пока плата печатает.
 */

#define VD3  PORT_Pin_0
#define VD4  PORT_Pin_1

#define SELECT_PORT   MDR_PORTC
#define SELECT_PIN    PORT_Pin_2

#define DEBOUNCE_MS   5      // сколько мс уровень кнопки должен быть стабильным
#define KEY_PERIOD_MS 10     // пауза между нажатием и отпусканием клавиши

#define TEXT_MAX      512

int msc_disk_read_text(char* out, int max);   // см. msc_disk.c
void msc_disk_init(void);

extern void     SystemCoreClockUpdate(void);
extern uint32_t SystemCoreClock;

//--------------------------------------------------------------------+
// Время и кнопка
//--------------------------------------------------------------------+
static volatile uint32_t ms_ticks;     // миллисекунды с момента запуска

void SysTick_Handler(void)
{
    ms_ticks++;
}

// Пауза с обслуживанием USB: диск продолжает работать, пока плата печатает
static void wait_ms(uint32_t ms)
{
    uint32_t start = ms_ticks;
    while (ms_ticks - start < ms) tud_task();
}

// Возвращает true один раз за каждое нажатие SELECT (с подавлением дребезга)
static bool select_clicked(void)
{
    static uint32_t last_ms;
    static int      integrator;
    static bool     pressed;

    if (ms_ticks == last_ms) return false;       // опрос раз в миллисекунду
    last_ms = ms_ticks;

    if (PORT_ReadInputDataBit(SELECT_PORT, SELECT_PIN) == 0) {
        if (integrator < DEBOUNCE_MS) integrator++;
    } else if (integrator > 0) {
        integrator--;
    }

    if (integrator == DEBOUNCE_MS && !pressed) { pressed = true;  return true; }
    if (integrator == 0)                       { pressed = false; }
    return false;
}

//--------------------------------------------------------------------+
// Печать текста клавиатурой
//--------------------------------------------------------------------+
// Каждый символ - отчёт "клавиша нажата" и отчёт "отпущена"
static void type_text(const char* text, int len)
{
    static const uint8_t ascii_to_key[128][2] = { HID_ASCII_TO_KEYCODE };   // {нужен Shift, код клавиши}

    for (int i = 0; i < len; i++) {
        uint8_t c = (uint8_t)text[i];
        if (c >= 128 || c == '\r' || ascii_to_key[c][1] == 0) continue;     // пропускаем всё, чего нет на клавиатуре; CR не нужен, Enter даёт LF

        uint8_t keycodes[6] = { ascii_to_key[c][1] };
        uint8_t shift       = ascii_to_key[c][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;

        for (int press = 1; press >= 0; press--) {
            while (!tud_hid_ready()) tud_task();
            tud_hid_keyboard_report(0, press ? shift : 0, press ? keycodes : NULL);
            wait_ms(KEY_PERIOD_MS);
        }
    }
}

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

    RST_CLK_PCLKcmd(RST_CLK_PCLK_PORTC, ENABLE);
    PORT_StructInit(&gpio);
    gpio.PORT_SPEED = PORT_SPEED_SLOW;
    gpio.PORT_MODE  = PORT_MODE_DIGITAL;

    gpio.PORT_Pin = VD3 | VD4;           // светодиоды - выходы
    gpio.PORT_OE  = PORT_OE_OUT;
    PORT_Init(MDR_PORTC, &gpio);

    gpio.PORT_Pin = SELECT_PIN;          // кнопка - вход
    gpio.PORT_OE  = PORT_OE_IN;
    PORT_Init(SELECT_PORT, &gpio);
}

int main(void)
{
    gpio_init();
    msc_disk_init();                     // создаём образ диска в RAM

    tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    tusb_init(0, &dev_init);             // здесь же настраивается PLL

    SystemCoreClockUpdate();             // тактовая частота известна только после tusb_init
    SysTick_Config(SystemCoreClock / 1000);

    while (1)
    {
        tud_task();                      // обработка USB-стека

        if (tud_mounted()) PORT_SetBits(MDR_PORTC, VD3); else PORT_ResetBits(MDR_PORTC, VD3);

        if (select_clicked() && tud_mounted()) {
            char text[TEXT_MAX];
            int  len = msc_disk_read_text(text, sizeof(text));

            PORT_SetBits(MDR_PORTC, VD4);
            type_text(text, len);
            PORT_ResetBits(MDR_PORTC, VD4);
        }
    }
}
