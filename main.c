#include <MDR32FxQI_port.h>
#include <MDR32FxQI_rst_clk.h>
#include <MDR32FxQI_adc.h>

#include "tusb.h"
#include "scope.h"

/*
 * Осциллограф в браузере: плата определяется как USB-диск, на котором лежит страница осциллографа
 * (index.htm). Страница открывается с диска в браузере на базе Chromium и по WebUSB получает отсчёты
 * АЦП этой же платы. Драйверы и установка программ не нужны. Диск только для чтения, образ во
 * флеш-памяти (src/disk_image.c, создаётся tools/make_disk.py из каталога web/).
 *
 * Вход: канал 7 АЦП (вывод PD7). На отладочной плате он идёт на разъём внешнего сигнала или на
 * подстроечный резистор, выбор делает перемычка. Диапазон входного напряжения 0...3,3 В.
 *
 * Захват: АЦП работает непрерывно, прерывание по каждому отсчёту отдаёт его в scope_sample()
 * (src/scope.c). Скорость выбирается делителем тактовой частоты АЦП, реальную частоту дискретизации
 * плата измеряет по SysTick и передаёт в каждом кадре.
 *
 * Страница -> плата (vendor-запросы на EP0, интерфейс 1, см. usb_descriptors.c):
 *   0x11  wValue = номер скорости (биты 0..3) | число отсчётов / 16 (биты 4..10)
 *   0x12  wValue = уровень синхронизации (биты 0..11) | спад (бит 12) | ждущий режим (бит 13) | пуск (бит 14)
 *
 * Плата -> страница, EP3 IN: кадры (little-endian)
 *   [0xA5] [0x5A] [флаги: бит 0 = запуск по синхронизации] [номер скорости] [число отсчётов, 2 байта]
 *   [номер кадра, 2 байта] [частота дискретизации, Гц, 4 байта] [отсчёты, по 2 байта, 12 бит]
 * Точка синхронизации стоит на пятой части окна, то есть на отсчёте n / 5.
 */

// Канал АЦП и вывод. Если на вашей плате сигнал заведён на другой канал, замените оба
#define SCOPE_ADC_CHANNEL   ADC_CH_ADC7
#define SCOPE_ADC_PORT      MDR_PORTD
#define SCOPE_ADC_PIN       PORT_Pin_7

#define REQ_CAPTURE         0x11
#define REQ_TRIGGER         0x12

#define FRAME_HEADER_LEN    12
#define TX_STALL_MS         300     // страница не забирает данные дольше этого времени: считаем её закрытой

#define VD3  PORT_Pin_0              // горит, если последний кадр пришёл по синхронизации
#define VD4  PORT_Pin_1              // меняет состояние с каждым кадром: плата работает

enum { RATE_COUNT = 6 };

// Делитель тактовой частоты АЦП: 8, 16, ... 256
static const uint32_t adc_prescaler[RATE_COUNT] = {
    ADC_CLK_div_8, ADC_CLK_div_16, ADC_CLK_div_32, ADC_CLK_div_64, ADC_CLK_div_128, ADC_CLK_div_256
};

extern void     SystemCoreClockUpdate(void);
extern uint32_t SystemCoreClock;

//--------------------------------------------------------------------+
// Время
//--------------------------------------------------------------------+
static volatile uint32_t ms_ticks;       // миллисекунды с момента запуска
static uint32_t          ticks_per_ms;   // тактов ядра в миллисекунде

void SysTick_Handler(void)
{
    ms_ticks++;
}

// Время в тактах ядра (с переполнением): миллисекунды SysTick плюс то, что набежало внутри миллисекунды
static uint32_t ticks_now(void)
{
    uint32_t ms1, ms2, val;
    do {
        ms1 = ms_ticks;
        val = SysTick->VAL;
        ms2 = ms_ticks;
    } while (ms1 != ms2);
    return ms1 * ticks_per_ms + (ticks_per_ms - 1u - val);
}

//--------------------------------------------------------------------+
// АЦП
//--------------------------------------------------------------------+
static uint32_t t_first, t_last;         // время первого и последнего отсчёта захвата

void ADC_IRQHandler(void)
{
    uint16_t v = (uint16_t)(ADC1_GetResult() & 0x0FFF);   // чтение результата сбрасывает флаг

    scope_sample(v);
    if (scope_count() == 1) t_first = ticks_now();
    if (scope_state() == SCOPE_DONE) {
        t_last = ticks_now();
        ADC1_ITConfig(ADC1_IT_END_OF_CONVERSION, DISABLE);
    }
}

// АЦП1 в непрерывном режиме на канале осциллографа; rate выбирает делитель тактовой частоты
static void adc_start(uint8_t rate)
{
    ADC_InitTypeDef  adc;
    ADCx_InitTypeDef adc1;

    RST_CLK_PCLKcmd(RST_CLK_PCLK_ADC, ENABLE);
    ADC1_ITConfig(ADC1_IT_END_OF_CONVERSION, DISABLE);
    ADC_DeInit();

    ADC_StructInit(&adc);                              // датчик температуры не нужен
    ADC_Init(&adc);

    ADCx_StructInit(&adc1);
    adc1.ADC_ClockSource    = ADC_CLOCK_SOURCE_CPU;
    adc1.ADC_SamplingMode   = ADC_SAMPLING_MODE_CICLIC_CONV;   // преобразования идут одно за другим
    adc1.ADC_ChannelNumber  = SCOPE_ADC_CHANNEL;
    adc1.ADC_Prescaler      = adc_prescaler[rate];
    adc1.ADC_VRefSource     = ADC_VREF_SOURCE_INTERNAL;        // опорное напряжение - питание
    adc1.ADC_IntVRefSource  = ADC_INT_VREF_SOURCE_INEXACT;
    ADC1_Init(&adc1);
    ADC1_Cmd(ENABLE);
    ADC1_Start();
}

//--------------------------------------------------------------------+
// Команды страницы (вызываются из обработчика запросов на EP0)
//--------------------------------------------------------------------+
static volatile struct {
    uint8_t  rate;
    uint16_t n;
    uint16_t level;
    uint8_t  falling, normal, run;
    uint8_t  dirty;
} cfg = { 0, 352, 2048, 0, 0, 0, 0 };

bool scope_command(uint8_t request, uint16_t value)
{
    if (request == REQ_CAPTURE) {
        uint8_t  rate = value & 0x0F;
        uint16_t n    = (uint16_t)(((value >> 4) & 0x7F) * 16u);
        if (rate >= RATE_COUNT || n < 16 || n > SCOPE_MAX_SAMPLES) return false;
        cfg.rate = rate;
        cfg.n    = n;
    } else if (request == REQ_TRIGGER) {
        cfg.level   = value & 0x0FFF;
        cfg.falling = (value >> 12) & 1;
        cfg.normal  = (value >> 13) & 1;
        cfg.run     = (value >> 14) & 1;
    } else {
        return false;
    }
    cfg.dirty = 1;
    return true;
}

//--------------------------------------------------------------------+
// Передача кадров
//--------------------------------------------------------------------+
static uint8_t  frame[FRAME_HEADER_LEN + 2 * SCOPE_MAX_SAMPLES] __attribute__((aligned(4)));
static uint16_t frame_len, frame_off;
static uint8_t  tx_active;
static uint16_t frame_seq;
static uint32_t tx_progress_ms;

static void capture_start(void)
{
    (void)ADC1_GetResult();                            // выбрасываем устаревший отсчёт
    scope_arm(cfg.n, cfg.level, cfg.falling, cfg.normal);
    ADC1_ITConfig(ADC1_IT_END_OF_CONVERSION, ENABLE);
}

static void leds_update(uint8_t triggered)
{
    if (triggered) PORT_SetBits(MDR_PORTC, VD3); else PORT_ResetBits(MDR_PORTC, VD3);
    if (frame_seq & 1) PORT_SetBits(MDR_PORTC, VD4); else PORT_ResetBits(MDR_PORTC, VD4);
}

// Захват готов: собираем кадр и сразу запускаем следующий захват, пока кадр уходит по USB
static void frame_build(void)
{
    uint16_t n     = scope_copy((uint16_t*)(void*)(frame + FRAME_HEADER_LEN));
    uint8_t  trig  = scope_triggered();
    uint32_t cnt   = scope_count();
    uint32_t dt    = t_last - t_first;
    uint32_t fs    = dt ? (uint32_t)(((uint64_t)(cnt - 1u) * SystemCoreClock) / dt) : 0;

    frame[0] = 0xA5;  frame[1] = 0x5A;
    frame[2] = trig;  frame[3] = cfg.rate;
    frame[4] = (uint8_t)n;           frame[5] = (uint8_t)(n >> 8);
    frame[6] = (uint8_t)frame_seq;   frame[7] = (uint8_t)(frame_seq >> 8);
    frame[8] = (uint8_t)fs;          frame[9] = (uint8_t)(fs >> 8);
    frame[10] = (uint8_t)(fs >> 16); frame[11] = (uint8_t)(fs >> 24);

    frame_len = (uint16_t)(FRAME_HEADER_LEN + 2u * n);
    frame_off = 0;
    tx_active = 1;
    tx_progress_ms = ms_ticks;
    leds_update(trig);
    frame_seq++;
}

static void frame_pump(void)
{
    uint32_t room = tud_vendor_write_available();
    if (room) {
        uint32_t left = (uint32_t)(frame_len - frame_off);
        uint32_t n    = room < left ? room : left;
        n = tud_vendor_write(frame + frame_off, n);
        frame_off = (uint16_t)(frame_off + n);
        tud_vendor_write_flush();
        if (n) tx_progress_ms = ms_ticks;
        if (frame_off >= frame_len) tx_active = 0;
    }
    if (ms_ticks - tx_progress_ms > TX_STALL_MS) {     // страницу закрыли: останавливаемся до новой команды
        tx_active = 0;
        cfg.run   = 0;
        cfg.dirty = 1;
    }
}

//--------------------------------------------------------------------+
// main
//--------------------------------------------------------------------+
static void gpio_init(void)
{
    PORT_InitTypeDef gpio;

    RST_CLK_PCLKcmd(RST_CLK_PCLK_PORTC | RST_CLK_PCLK_PORTD, ENABLE);
    PORT_StructInit(&gpio);
    gpio.PORT_SPEED = PORT_SPEED_SLOW;

    gpio.PORT_MODE = PORT_MODE_DIGITAL;      // светодиоды - выходы
    gpio.PORT_Pin  = VD3 | VD4;
    gpio.PORT_OE   = PORT_OE_OUT;
    PORT_Init(MDR_PORTC, &gpio);

    gpio.PORT_MODE = PORT_MODE_ANALOG;       // вход АЦП: аналоговый режим вывода
    gpio.PORT_Pin  = SCOPE_ADC_PIN;
    gpio.PORT_OE   = PORT_OE_IN;
    PORT_Init(SCOPE_ADC_PORT, &gpio);
}

int main(void)
{
    uint8_t cur_rate = 0xFF, running = 0;

    gpio_init();

    tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    tusb_init(0, &dev_init);             // здесь же настраивается PLL

    SystemCoreClockUpdate();             // тактовая частота известна только после tusb_init
    ticks_per_ms = SystemCoreClock / 1000;
    SysTick_Config(ticks_per_ms);

    NVIC_SetPriority(ADC_IRQn, 1);       // отсчёты важнее USB: прерывание АЦП может прервать обработку USB
    NVIC_SetPriority(USB_IRQn, 2);
    NVIC_EnableIRQ(ADC_IRQn);

    while (1)
    {
        tud_task();                      // обработка USB-стека

        if (cfg.dirty) {                 // страница прислала новые настройки
            cfg.dirty = 0;
            running = cfg.run;
            if (!running) {
                scope_stop();
                ADC1_ITConfig(ADC1_IT_END_OF_CONVERSION, DISABLE);
            } else {
                if (cfg.rate != cur_rate) {      // новая скорость: перенастраиваем АЦП
                    cur_rate = cfg.rate;
                    adc_start(cur_rate);
                }
                capture_start();
            }
        }

        if (running && !tx_active && scope_state() == SCOPE_DONE) {
            frame_build();
            capture_start();             // следующий захват идёт, пока уходит этот кадр
        }
        if (tx_active) frame_pump();
    }
}
