#include <MDR32FxQI_port.h>
#include <MDR32FxQI_rst_clk.h>

#include "tusb.h"

/*
 * WebUSB-демо: плата общается с веб-страницей (web/index.html) прямо из браузера Chrome/Edge,
 * без драйверов и без установки программ.
 *
 *   плата -> страница  кадр состояния раз в 20 мс: кнопки, светодиоды, значение АЦП, время
 *   страница -> плата  команда "включить/выключить светодиоды"
 *
 * Формат кадров (все числа little-endian):
 *   плата -> страница  [0x01] [кнопки] [светодиоды] [АЦП, 2 байта] [мс с запуска, 4 байта]
 *   страница -> плата  [0x01] [светодиоды]
 * Биты кнопок: 0 SELECT, 1 UP, 2 DOWN, 3 LEFT, 4 RIGHT. Биты светодиодов: 0 VD3, 1 VD4.
 */

// 1 - измерять встроенный датчик температуры АЦП (нужен компонент Drivers -> ADC в Keil RTE),
// 0 - вместо АЦП выдавать "треугольник" (для проверки USB без АЦП)
#define USE_ADC       1

#define FRAME_PERIOD_MS  20
#define DEBOUNCE_MS      5

#define MSG_STATE   0x01
#define MSG_LEDS    0x01

#define VD3  PORT_Pin_0
#define VD4  PORT_Pin_1

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

extern void     SystemCoreClockUpdate(void);
extern uint32_t SystemCoreClock;

//--------------------------------------------------------------------+
// Время, кнопки, светодиоды
//--------------------------------------------------------------------+
static volatile uint32_t ms_ticks;       // миллисекунды с момента запуска

void SysTick_Handler(void)
{
    ms_ticks++;
}

static uint8_t buttons_state;            // маска нажатых кнопок (бит = номер BTN_xxx)
static uint8_t leds_state;               // маска включённых светодиодов

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

static void leds_set(uint8_t mask)
{
    leds_state = mask & 0x03;
    if (leds_state & 1) PORT_SetBits(MDR_PORTC, VD3); else PORT_ResetBits(MDR_PORTC, VD3);
    if (leds_state & 2) PORT_SetBits(MDR_PORTC, VD4); else PORT_ResetBits(MDR_PORTC, VD4);
}

//--------------------------------------------------------------------+
// АЦП
//--------------------------------------------------------------------+
#if USE_ADC
#include <MDR32FxQI_adc.h>

static void adc_init(void)
{
    ADC_InitTypeDef  adc;
    ADCx_InitTypeDef adc1;

    RST_CLK_PCLKcmd(RST_CLK_PCLK_ADC, ENABLE);
    ADC_DeInit();

    ADC_StructInit(&adc);                              // общие настройки: включаем датчик температуры
    adc.ADC_TempSensor           = ADC_TEMP_SENSOR_Enable;
    adc.ADC_TempSensorAmplifier  = ADC_TEMP_SENSOR_AMPLIFIER_Enable;
    adc.ADC_TempSensorConversion = ADC_TEMP_SENSOR_CONVERSION_Enable;
    ADC_Init(&adc);

    ADCx_StructInit(&adc1);                            // АЦП1: одиночное преобразование
    adc1.ADC_ClockSource    = ADC_CLOCK_SOURCE_CPU;
    adc1.ADC_SamplingMode   = ADC_SAMPLING_MODE_SINGLE_CONV;
    adc1.ADC_ChannelNumber  = ADC_CH_TEMP_SENSOR;      // чтобы измерять вход, например, ADC_CH_ADC7
    adc1.ADC_Prescaler      = ADC_CLK_div_16;
    adc1.ADC_VRefSource     = ADC_VREF_SOURCE_INTERNAL;
    adc1.ADC_IntVRefSource  = ADC_INT_VREF_SOURCE_INEXACT;
    ADC1_Init(&adc1);
    ADC1_Cmd(ENABLE);
}

static uint16_t adc_read(void)
{
    ADC1_Start();
    for (int i = 0; i < 10000 && ADC1_GetFlagStatus(ADC1_FLAG_END_OF_CONVERSION) == RESET; i++) {}
    return (uint16_t)(ADC1_GetResult() & 0x0FFF);
}

#else

static void adc_init(void) {}

// Вместо АЦП: "треугольник" 0..4095 с периодом около 8 секунд
static uint16_t adc_read(void)
{
    uint32_t t = (ms_ticks / 2) % 8192;
    return (uint16_t)(t < 4096 ? t : 8191 - t);
}
#endif

//--------------------------------------------------------------------+
// Обмен со страницей
//--------------------------------------------------------------------+
static void send_state(void)
{
    uint16_t adc = adc_read();
    uint32_t ms  = ms_ticks;
    uint8_t  frame[9] = {
        MSG_STATE, buttons_state, leds_state,
        (uint8_t)adc, (uint8_t)(adc >> 8),
        (uint8_t)ms, (uint8_t)(ms >> 8), (uint8_t)(ms >> 16), (uint8_t)(ms >> 24)
    };

    if (tud_vendor_write_available() >= sizeof(frame)) {   // если страница не читает, кадр пропускаем
        tud_vendor_write(frame, sizeof(frame));
        tud_vendor_write_flush();
    }
}

static void receive_commands(void)
{
    uint8_t cmd[2];

    while (tud_vendor_available() >= sizeof(cmd)) {
        tud_vendor_read(cmd, sizeof(cmd));
        if (cmd[0] == MSG_LEDS) leds_set(cmd[1]);
    }
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
    adc_init();                          // АЦП тактируется от ядра: настраиваем после PLL

    uint32_t last_ms = 0, last_frame_ms = 0;
    uint8_t  sent_buttons = 0;

    while (1)
    {
        tud_task();                      // обработка USB-стека

        receive_commands();

        if (ms_ticks == last_ms) continue;     // дальше - раз в миллисекунду
        last_ms = ms_ticks;
        buttons_scan();

        // Кадр уходит по таймеру и сразу при нажатии/отпускании кнопки
        if (ms_ticks - last_frame_ms >= FRAME_PERIOD_MS || buttons_state != sent_buttons) {
            last_frame_ms = ms_ticks;
            sent_buttons  = buttons_state;
            send_state();
        }
    }
}
