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

#define VD3     PORT_Pin_0
#define VD4     PORT_Pin_1
#define LED_ALL (VD3 | VD4)

#define DEBOUNCE_MS   5
#define SPEED_BYTES   65536u     // сколько байт отправляет команда speed
#define TXQ_SIZE      1024       // очередь вывода консоли
#define LINE_MAX      32         // длина вводимой команды

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
// Время в миллисекундах (SysTick)
//--------------------------------------------------------------------+
static volatile uint32_t ms_ticks;

void SysTick_Handler(void)
{
    ms_ticks++;
}

//--------------------------------------------------------------------+
// Кнопки (опрос раз в миллисекунду, подавление дребезга)
//--------------------------------------------------------------------+
static uint8_t buttons_state;      // маска нажатых кнопок (бит = BTN_xxx)

static void buttons_scan(void)
{
    static uint8_t integrator[BTN_COUNT];

    for (int i = 0; i < BTN_COUNT; i++) {
        if (PORT_ReadInputDataBit(buttons[i].port, buttons[i].pin) == 0) {
            if (integrator[i] < DEBOUNCE_MS) integrator[i]++;
        } else if (integrator[i] > 0) {
            integrator[i]--;
        }
        if (integrator[i] == DEBOUNCE_MS)  buttons_state |= (1u << i);
        else if (integrator[i] == 0)       buttons_state &= ~(1u << i);
    }
}

//--------------------------------------------------------------------+
// Вывод: очередь байт, которая по мере возможности уходит в USB.
// Благодаря ей длинный текст не теряется, даже если буфер CDC (64 байта) мал.
//--------------------------------------------------------------------+
static uint8_t  txq[TXQ_SIZE];
static uint16_t txq_head, txq_tail;

static uint16_t txq_free(void)
{
    return (uint16_t)((txq_tail - txq_head - 1 + TXQ_SIZE) % TXQ_SIZE);
}

static void put_char(char c)
{
    if (txq_free() == 0) return;                  // очередь полна - символ пропускаем
    txq[txq_head] = (uint8_t)c;
    txq_head = (uint16_t)((txq_head + 1) % TXQ_SIZE);
}

static void put(const char* s)
{
    while (*s) put_char(*s++);
}

static void put_fmt(const char* fmt, uint32_t a, uint32_t b, uint32_t c)
{
    char tmp[64];
    snprintf(tmp, sizeof(tmp), fmt, (unsigned long)a, (unsigned long)b, (unsigned long)c);
    put(tmp);
}

static void tx_pump(void)
{
    uint32_t room = tud_cdc_write_available();
    bool     wrote = false;
    while (room && txq_tail != txq_head) {
        tud_cdc_write_char((char)txq[txq_tail]);
        txq_tail = (uint16_t)((txq_tail + 1) % TXQ_SIZE);
        room--;
        wrote = true;
    }
    if (wrote) tud_cdc_write_flush();
}

//--------------------------------------------------------------------+
// Консоль
//--------------------------------------------------------------------+
static char     line[LINE_MAX];
static uint8_t  line_len;
static bool     last_was_cr;
static bool     echo_mode;           // прозрачное эхо вместо консоли
static uint32_t speed_left;          // сколько байт ещё надо отправить для speed
static uint32_t speed_start_ms;
static bool     speed_running;

#define PROMPT  "> "

static void cmd_help(void)
{
    put("help             this list\r\n"
        "led N on|off     LED VD3 or VD4 (N = 3 or 4)\r\n"
        "btn              which buttons are pressed now\r\n"
        "uptime           time since reset\r\n"
        "speed            send 64 KB to the PC and measure speed\r\n"
        "echo             raw echo mode (until the port is closed)\r\n");
}

static void cmd_led(char* n, char* state)
{
    uint16_t pin = (n && n[0] == '3' && !n[1]) ? VD3 : (n && n[0] == '4' && !n[1]) ? VD4 : 0;
    bool     on  = state && strcmp(state, "on") == 0;
    if (!pin || !(on || (state && strcmp(state, "off") == 0))) {
        put("usage: led 3|4 on|off\r\n");
        return;
    }
    if (on) PORT_SetBits(MDR_PORTC, pin); else PORT_ResetBits(MDR_PORTC, pin);
    put("LED"); put_char(n[0]); put(on ? " on\r\n" : " off\r\n");
}

static void cmd_btn(void)
{
    bool any = false;
    for (int i = 0; i < BTN_COUNT; i++) {
        if (buttons_state & (1u << i)) {
            put(buttons[i].name); put_char(' ');
            any = true;
        }
    }
    put(any ? "pressed\r\n" : "none pressed\r\n");
}

static void cmd_uptime(void)
{
    uint32_t t = ms_ticks;
    put_fmt("up %02lu:%02lu:%02lu", t / 3600000u, t / 60000u % 60u, t / 1000u % 60u);
    put_fmt(".%03lu\r\n", t % 1000u, 0, 0);
}

static void cmd_speed(void)
{
    speed_left     = SPEED_BYTES;
    speed_start_ms = ms_ticks;
    speed_running  = true;
}

static void run_command(char* s)
{
    char* arg[3] = {0, 0, 0};
    int   n = 0;
    while (*s && n < 3) {                          // делим строку на слова
        while (*s == ' ') *s++ = 0;
        if (*s) { arg[n++] = s; while (*s && *s != ' ') s++; }
    }
    if (n == 0) return;

    if      (!strcmp(arg[0], "help"))   cmd_help();
    else if (!strcmp(arg[0], "led"))    cmd_led(arg[1], arg[2]);
    else if (!strcmp(arg[0], "btn"))    cmd_btn();
    else if (!strcmp(arg[0], "uptime")) cmd_uptime();
    else if (!strcmp(arg[0], "speed"))  cmd_speed();
    else if (!strcmp(arg[0], "echo"))   { echo_mode = true; put("ECHO MODE\r\n"); }
    else                                put("unknown command, type help\r\n");
}

static void console_char(char c)
{
    if (c == '\n' && last_was_cr) {                // CR LF считаем одним переводом строки
        last_was_cr = false;
        return;
    }
    last_was_cr = (c == '\r');

    if (c == '\r' || c == '\n') {
        put("\r\n");
        line[line_len] = 0;
        run_command(line);
        line_len = 0;
        if (!speed_running && !echo_mode) put(PROMPT);   // после speed приглашение выдаст он сам
    } else if (c == 8 || c == 127) {               // Backspace
        if (line_len) { line_len--; put("\b \b"); }
    } else if (c >= 32 && c < 127 && line_len < LINE_MAX - 1) {
        line[line_len++] = c;
        put_char(c);                               // эхо вводимого символа
    }
}

// Сброс состояния при подключении/отключении терминала
static void console_reset(void)
{
    txq_head = txq_tail = 0;
    line_len = 0;
    last_was_cr = false;
    echo_mode = false;
    speed_running = false;
    speed_left = 0;
}

static void speed_task(void)
{
    if (!speed_running) return;

    while (speed_left && txq_free() >= 64) {       // поток точек, как «индикатор прогресса»
        uint32_t n = speed_left < 64 ? speed_left : 64;
        for (uint32_t i = 0; i < n; i++) put_char('.');
        speed_left -= n;
    }
    // Всё ушло, когда очередь и буфер CDC пусты
    if (!speed_left && txq_tail == txq_head && tud_cdc_write_available() == CFG_TUD_CDC_TX_BUFSIZE) {
        uint32_t ms = ms_ticks - speed_start_ms;
        if (ms == 0) ms = 1;
        put_fmt("\r\n%lu KB in %lu ms = %lu KB/s\r\n", SPEED_BYTES / 1024u, ms, (SPEED_BYTES / 1024u) * 1000u / ms);
        put(PROMPT);
        speed_running = false;
    }
}

static void buttons_task(void)
{
    static uint8_t  prev;
    static uint32_t last_ms;

    if (ms_ticks == last_ms) return;               // опрос раз в миллисекунду
    last_ms = ms_ticks;
    buttons_scan();

    uint8_t pressed = buttons_state & (uint8_t)~prev;
    prev = buttons_state;
    if (!pressed || echo_mode || speed_running) return;

    for (int i = 0; i < BTN_COUNT; i++) {
        if (pressed & (1u << i)) {
            put("\r\n"); put(buttons[i].name); put(" pressed\r\n");
        }
    }
    put(PROMPT);                                   // вернуть приглашение и недописанную команду
    for (int i = 0; i < line_len; i++) put_char(line[i]);
}

static void cdc_task(void)
{
    static bool was_connected;
    bool connected = tud_cdc_connected();          // терминал открыл порт (DTR)

    if (connected != was_connected) {
        was_connected = connected;
        console_reset();
        tud_cdc_read_flush();
        if (connected) put("\r\nMilandr MDR32F9Q2I, TinyUSB CDC\r\nType 'help' for commands.\r\n" PROMPT);
    }
    if (!connected) return;

    if (echo_mode) {
        tx_pump();                                 // допечатать "ECHO MODE"
        // Читаем не больше, чем влезет в TX-буфер: иначе при медленном хосте байты теряются
        uint32_t room = tud_cdc_write_available();
        if (txq_tail == txq_head && room > 0 && tud_cdc_available()) {
            uint8_t buf[64];
            uint32_t n = tud_cdc_read(buf, room < sizeof(buf) ? room : sizeof(buf));
            tud_cdc_write(buf, n);
            tud_cdc_write_flush();
        }
        return;
    }

    // Принимаем команды, пока в очереди вывода достаточно места для ответа
    while (tud_cdc_available() && txq_free() >= 400 && !speed_running) {
        console_char((char)tud_cdc_read_char());
    }
    if (speed_running) {
        tud_cdc_read_flush();                      // во время speed ввод игнорируется
    }
    speed_task();
    buttons_task();
    tx_pump();
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

    gpio.PORT_Pin = LED_ALL;                       // светодиоды - выходы
    gpio.PORT_OE  = PORT_OE_OUT;
    PORT_Init(MDR_PORTC, &gpio);

    gpio.PORT_OE  = PORT_OE_IN;                    // кнопки - входы
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
    tusb_init(0, &dev_init);                       // здесь же настраивается PLL

    // Тактовая частота известна только после tusb_init
    SystemCoreClockUpdate();
    SysTick_Config(SystemCoreClock / 1000);

    while (1)
    {
        tud_task();   // обработка USB-стека
        cdc_task();
    }
}
