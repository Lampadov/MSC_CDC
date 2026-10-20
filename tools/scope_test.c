/*
 * Проверка логики захвата на ПК: gcc -O1 -Wall -I src tools/scope_test.c src/scope.c -o scope_test && ./scope_test
 * Подаём в scope_sample() синтетические сигналы и проверяем положение точки синхронизации.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "scope.h"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t sine(unsigned i, unsigned period, uint16_t mid, uint16_t amp)
{
    return (uint16_t)lround(mid + amp * sin(6.283185307 * (double)i / period));
}

// Подаёт отсчёты, пока захват не закончится; возвращает число поданных
static unsigned run(uint16_t (*gen)(unsigned), unsigned limit)
{
    unsigned i = 0;
    while (scope_state() != SCOPE_DONE && i < limit) { scope_sample(gen(i)); i++; }
    return i;
}

static uint16_t gen_sine(unsigned i)   { return sine(i, 100, 2048, 1500); }
static uint16_t gen_flat(unsigned i)   { (void)i; return 2048; }
static uint16_t gen_above(unsigned i)  { (void)i; return 3000; }
static uint16_t gen_noise(unsigned i)  { return (uint16_t)(2048 + ((i * 7919u) % 21) - 10); }   // +-10 кодов у уровня

int main(void)
{
    static uint16_t out[SCOPE_MAX_SAMPLES];
    uint16_t n;

    // 1. Фронт синусоиды: точка синхронизации стоит на 1/5 окна, значение около уровня
    scope_arm(400, 2048, 0, 1);
    run(gen_sine, 100000);
    CHECK(scope_state() == SCOPE_DONE, "rise: capture did not finish");
    CHECK(scope_triggered(), "rise: not triggered");
    n = scope_copy(out);
    CHECK(n == 400, "rise: n=%u", n);
    CHECK(out[80] >= 2048 && out[79] < 2048 + 200 && out[79] <= out[80], "rise: trigger sample out[79]=%u out[80]=%u", out[79], out[80]);
    CHECK(out[80] - 2048 < 200, "rise: trigger far from level: %u", out[80]);
    CHECK(out[90] > out[80], "rise: signal must go up after trigger");

    // 2. Спад
    scope_arm(400, 2048, 1, 1);
    run(gen_sine, 100000);
    n = scope_copy(out);
    CHECK(scope_triggered() && n == 400, "fall: triggered=%d n=%u", scope_triggered(), n);
    CHECK(out[80] <= 2048 && out[81] < out[80], "fall: out[80]=%u out[81]=%u", out[80], out[81]);

    // 3. Без сигнала: "Авто" запускается без синхронизации, "Ждущий" ждёт
    scope_arm(400, 3000, 0, 0);
    run(gen_flat, 100000);
    CHECK(scope_state() == SCOPE_DONE && !scope_triggered(), "auto: state=%d trig=%d", scope_state(), scope_triggered());
    CHECK(scope_count() >= 800, "auto: waited only %u samples", (unsigned)scope_count());

    scope_arm(400, 3000, 0, 1);
    run(gen_flat, 20000);
    CHECK(scope_state() == SCOPE_WAIT, "normal: state=%d, must keep waiting", scope_state());

    // 4. Сигнал уже выше уровня и никогда не опускается: фронта нет
    scope_arm(400, 2048, 0, 1);
    run(gen_above, 20000);
    CHECK(scope_state() == SCOPE_WAIT, "above: must not trigger without a crossing");

    // 5. Шум около уровня: гистерезис не даёт ложных запусков
    scope_arm(400, 2048, 0, 1);
    run(gen_noise, 20000);
    CHECK(scope_state() == SCOPE_WAIT, "noise: false trigger inside hysteresis band");

    // 6. Размеры: минимум и максимум, после арм выходной порядок отсчётов не нарушается
    scope_arm(16, 2048, 0, 1);
    run(gen_sine, 100000);
    CHECK(scope_copy(out) == 16 && scope_state() == SCOPE_DONE, "min n");
    scope_arm(SCOPE_MAX_SAMPLES, 2048, 0, 1);
    run(gen_sine, 100000);
    CHECK(scope_copy(out) == SCOPE_MAX_SAMPLES, "max n");
    scope_arm(5000, 2048, 0, 1);                        // слишком много: обрезается до максимума
    run(gen_sine, 100000);
    CHECK(scope_copy(out) == SCOPE_MAX_SAMPLES, "clamp n");

    // 7. Повторный захват после остановки
    scope_arm(400, 2048, 0, 1);
    run(gen_sine, 100000);
    scope_stop();
    CHECK(scope_state() == SCOPE_IDLE, "stop");
    scope_sample(1234);
    CHECK(scope_state() == SCOPE_IDLE, "samples in idle must be ignored");

    printf(failures ? "scope_test: %d failure(s)\n" : "scope_test: all passed\n", failures);
    return failures ? 1 : 0;
}
