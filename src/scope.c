#include "scope.h"

static uint16_t ring[SCOPE_MAX_SAMPLES];

static volatile uint8_t state = SCOPE_IDLE;
static uint8_t  trig_hit;
static uint8_t  falling_edge, normal_mode, edge_armed;
static uint16_t n_samples, pre_samples, level_code;
static uint16_t wr, pre_cnt, post_cnt;
static uint32_t wait_cnt, wait_limit, total;

void scope_arm(uint16_t n, uint16_t level, uint8_t falling, uint8_t normal)
{
    state = SCOPE_IDLE;                      // пока настраиваем, прерывание ничего не делает
    if (n < 16) n = 16;
    if (n > SCOPE_MAX_SAMPLES) n = SCOPE_MAX_SAMPLES;

    n_samples    = n;
    pre_samples  = (uint16_t)(n / 5);
    level_code   = level;
    falling_edge = falling;
    normal_mode  = normal;
    edge_armed   = 0;
    trig_hit     = 0;
    wr = pre_cnt = post_cnt = 0;
    wait_cnt = total = 0;
    wait_limit = 2u * n;
    state = SCOPE_PRE;
}

void scope_stop(void)
{
    state = SCOPE_IDLE;
}

uint8_t  scope_state(void)     { return state; }
uint8_t  scope_triggered(void) { return trig_hit; }
uint32_t scope_count(void)     { return total; }

static void put(uint16_t v)
{
    ring[wr] = v;
    if (++wr >= n_samples) wr = 0;
    total++;
}

static void start_post(void)
{
    post_cnt = (uint16_t)(n_samples - pre_samples - 1);   // сам отсчёт запуска уже в кольце
    state = post_cnt ? SCOPE_POST : SCOPE_DONE;
}

void scope_sample(uint16_t v)
{
    switch (state) {
    case SCOPE_PRE:
        put(v);
        if (++pre_cnt >= pre_samples) state = SCOPE_WAIT;
        break;

    case SCOPE_WAIT: {
        uint8_t hit = 0;
        put(v);
        if (!falling_edge) {
            if (v + SCOPE_HYSTERESIS < level_code) edge_armed = 1;
            else if (edge_armed && v >= level_code) hit = 1;
        } else {
            if (v > level_code + SCOPE_HYSTERESIS) edge_armed = 1;
            else if (edge_armed && v <= level_code) hit = 1;
        }
        if (hit) {
            trig_hit = 1;
            start_post();
        } else if (!normal_mode && ++wait_cnt >= wait_limit) {
            start_post();                    // "Авто": сигнал без фронта, показываем как есть
        }
        break;
    }

    case SCOPE_POST:
        put(v);
        if (--post_cnt == 0) state = SCOPE_DONE;
        break;

    default:
        break;
    }
}

uint16_t scope_copy(uint16_t* dst)
{
    uint16_t i = wr;                          // wr указывает на самый старый отсчёт
    for (uint16_t k = 0; k < n_samples; k++) {
        dst[k] = ring[i];
        if (++i >= n_samples) i = 0;
    }
    return n_samples;
}
