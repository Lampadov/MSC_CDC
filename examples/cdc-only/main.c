#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <MDR32FxQI_port.h>
#include <MDR32FxQI_rst_clk.h>

#include "tusb.h"

/*
 * Консоль в виртуальном COM-порте. Откройте порт в PuTTY / Tera Term (скорость любая):
 * плата пришлёт приветствие и будет выполнять команды (help - список).
 * Нажатия кнопок платы тоже печатаются в терминале.
 *
 * Команда echo включает прозрачное эхо (нужно для стресс-теста tools/cdc_stress.py);
 * оно действует до закрытия порта.
 */

#define VD3  PORT_Pin_0
#define VD4  PORT_Pin_1

#define DEBOUNCE_MS  5
#define LINE_MAX     32
#define PROMPT       "> "

enum { BTN_SELECT, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_COUNT };

static const struct {
    MDR_PORT_TypeDef* port;
    uint16_t          pin;
    const char*       name;
} buttons[BTN_COUNT] = {
    {MDR_PORTC, PORT_Pin_2, "SELECT"},
    {MDR_PORTB, PORT_Pin_5, "UP"},
    {MDR_PORTE, PORT_Pin_1, "DOWN"},
    {MDR_PORTE, PORT_Pin_3, "LEFT"},
    {MDR_PORTB, PORT_Pin_6, "RIGHT"}
};

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

// Опрос кнопок раз в миллисекунду с подавлением дребезга
static void buttons_scan(void)
{
    static uint8_t integrator[BTN_COUNT];

    for (int i = 0; i < BTN_COUNT; i++) {
        if (PORT_ReadInputDataBit(buttons[i].port, buttons[i].pin) == 0) {
            if (integrator[i] < DEBOUNCE_MS) integrator[i]++;
        } else if (integrator[i] > 0) {
            integrator[i]--;
        }
        if (integrator[i] == DEBOUNCE_MS) buttons_state |= (1u << i);
        if (integrator[i] == 0)           buttons_state &= ~(1u << i);
    }
}

//--------------------------------------------------------------------+
// Вывод в терминал
//--------------------------------------------------------------------+
// Если буфер CDC (64 байта) заполнен, ждём, обслуживая USB, пока хост его заберёт.
static void put(const char* s)
{
    while (*s && tud_cdc_connected()) {
        if (tud_cdc_write_char(*s)) {
            s++;
        } else {
            tud_cdc_write_flush();
            tud_task();
        }
    }
    tud_cdc_write_flush();
}

static void print(const char* fmt, ...)
{
    char    buf[96];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    put(buf);
}

//--------------------------------------------------------------------+
// Команды
//--------------------------------------------------------------------+
static char line[LINE_MAX];              // вводимая строка
static int  line_len;
static bool echo_mode;                   // прозрачное эхо вместо консоли

static void cmd_led(const char* n, const char* state)
{
    uint16_t pin = !n ? 0 : !strcmp(n, "3") ? VD3 : !strcmp(n, "4") ? VD4 : 0;

    if (pin && state && !strcmp(state, "on")) {
        PORT_SetBits(MDR_PORTC, pin);
    } else if (pin && state && !strcmp(state, "off")) {
        PORT_ResetBits(MDR_PORTC, pin);
    } else {
        put("usage: led 3|4 on|off\r\n");
        return;
    }
    print("LED%s %s\r\n", n, state);
}

static void cmd_btn(void)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        if (buttons_state & (1u << i)) print("%s ", buttons[i].name);
    }
    put(buttons_state ? "pressed\r\n" : "none pressed\r\n");
}

static void cmd_uptime(void)
{
    uint32_t t = ms_ticks;

    print("up %02lu:%02lu:%02lu.%03lu\r\n", (unsigned long)(t / 3600000u), (unsigned long)(t / 60000u % 60u),
          (unsigned long)(t / 1000u % 60u), (unsigned long)(t % 1000u));
}

// Отправляет 64 КБ точек и показывает скорость
static void cmd_speed(void)
{
    uint32_t start = ms_ticks;

    for (int i = 0; i < 1024; i++) {
        put("................................................................");
    }
    uint32_t ms = ms_ticks - start;
    print("\r\n64 KB in %lu ms = %lu KB/s\r\n", (unsigned long)ms, (unsigned long)(64000u / (ms ? ms : 1)));
    tud_cdc_read_flush();                // то, что набрали за это время, отбрасываем
}

static void run_command(char* s)
{
    char* arg[3] = {0, 0, 0};
    int   n = 0;

    while (*s && n < 3) {                // делим строку на слова
        while (*s == ' ') *s++ = 0;
        if (*s) {
            arg[n++] = s;
            while (*s && *s != ' ') s++;
        }
    }
    if (n == 0) return;

    if (!strcmp(arg[0], "help")) {
        put("help             this list\r\n"
            "led N on|off     LED VD3 or VD4 (N = 3 or 4)\r\n"
            "btn              which buttons are pressed now\r\n"
            "uptime           time since reset\r\n"
            "speed            send 64 KB to the PC and measure speed\r\n"
            "echo             raw echo mode (until the port is closed)\r\n");
    }
    else if (!strcmp(arg[0], "led"))    cmd_led(arg[1], arg[2]);
    else if (!strcmp(arg[0], "btn"))    cmd_btn();
    else if (!strcmp(arg[0], "uptime")) cmd_uptime();
    else if (!strcmp(arg[0], "speed"))  cmd_speed();
    else if (!strcmp(arg[0], "echo"))   { put("ECHO MODE\r\n"); echo_mode = true; }
    else                                put("unknown command, type help\r\n");
}

// Один принятый символ: набор строки, Backspace, Enter
static void console_char(char c)
{
    static bool last_was_cr;

    if (c == '\n' && last_was_cr) {      // CR LF считаем одним переводом строки
        last_was_cr = false;
        return;
    }
    last_was_cr = (c == '\r');

    if (c == '\r' || c == '\n') {
        put("\r\n");
        run_command(line);
        line_len = 0;
        line[0] = 0;
        if (!echo_mode) put(PROMPT);
    } else if ((c == 8 || c == 127) && line_len > 0) {   // Backspace
        line[--line_len] = 0;
        put("\b \b");
    } else if (c >= 32 && c < 127 && line_len < LINE_MAX - 1) {
        line[line_len++] = c;
        line[line_len] = 0;
        print("%c", c);                 // эхо вводимого символа
    }
}

//--------------------------------------------------------------------+
// Задачи
//--------------------------------------------------------------------+
// Печатает нажатия кнопок, вернув приглашение и недописанную команду
static void buttons_task(void)
{
    static uint8_t  prev;
    static uint32_t last_ms;

    if (ms_ticks == last_ms) return;     // опрос раз в миллисекунду
    last_ms = ms_ticks;

    buttons_scan();
    uint8_t pressed = buttons_state & (uint8_t)~prev;
    prev = buttons_state;

    for (int i = 0; i < BTN_COUNT; i++) {
        if (pressed & (1u << i)) {
            print("\r\n%s pressed\r\n" PROMPT "%s", buttons[i].name, line);
        }
    }
}

// Эхо: читаем не больше, чем влезет в TX-буфер, иначе при медленном хосте байты теряются
static void echo_task(void)
{
    uint32_t room = tud_cdc_write_available();

    if (room && tud_cdc_available()) {
        uint8_t  buf[64];
        uint32_t n = tud_cdc_read(buf, room < sizeof(buf) ? room : sizeof(buf));
        tud_cdc_write(buf, n);
        tud_cdc_write_flush();
    }
}

static void cdc_task(void)
{
    static bool was_connected;
    bool        connected = tud_cdc_connected();   // терминал открыл порт (DTR)

    if (connected != was_connected) {              // порт открыли или закрыли
        was_connected = connected;
        echo_mode = false;
        line_len = 0;
        line[0] = 0;
        tud_cdc_read_flush();
        if (connected) {
            // К1986ВЕ9х в UTF-8 (байтами, чтобы не зависеть от кодировки файла)
            put("\r\nMilandr \xD0\x9A" "1986" "\xD0\x92\xD0\x95" "9" "\xD1\x85"
                ", TinyUSB CDC\r\nType 'help' for commands.\r\n" PROMPT);
        }
    }
    if (!connected) return;

    if (echo_mode) {
        echo_task();
        return;
    }
    while (tud_cdc_available()) {
        console_char((char)tud_cdc_read_char());
    }
    buttons_task();
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

    while (1)
    {
        tud_task();                      // обработка USB-стека
        cdc_task();
    }
}
