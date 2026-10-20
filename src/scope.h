#ifndef SCOPE_H
#define SCOPE_H

/*
 * Логика захвата осциллографа без обращения к железу: её можно проверить на ПК
 * (tools/scope_test.c). Отсчёты АЦП по одному поступают в scope_sample() из прерывания АЦП.
 *
 * Работа:  SCOPE_PRE  - набираем отсчёты до точки синхронизации (1/5 экрана, 2 деления из 10)
 *          SCOPE_WAIT - ищем фронт или спад через уровень; в режиме "Авто" через 2 экрана без
 *                       события запускаемся без синхронизации
 *          SCOPE_POST - добираем отсчёты после точки синхронизации
 *          SCOPE_DONE - в кольце лежит ровно n отсчётов: scope_copy() раскладывает их по порядку
 */

#include <stdint.h>

#define SCOPE_MAX_SAMPLES  1120   // 70 * 16: хватает на 10 делений при любой развёртке
#define SCOPE_HYSTERESIS   24     // зона нечувствительности у уровня, в кодах АЦП (около 20 мВ)

enum { SCOPE_IDLE, SCOPE_PRE, SCOPE_WAIT, SCOPE_POST, SCOPE_DONE };

// Начать захват n отсчётов (n кратно 16). level - 0..4095, falling - запуск по спаду,
// normal - ждать синхронизацию бесконечно (иначе "Авто")
void     scope_arm(uint16_t n, uint16_t level, uint8_t falling, uint8_t normal);
void     scope_stop(void);

// Вызывается на каждый отсчёт из прерывания АЦП
void     scope_sample(uint16_t value);

uint8_t  scope_state(void);
uint8_t  scope_triggered(void);      // 1, если захват запущен по синхронизации
uint32_t scope_count(void);          // сколько отсчётов принято с момента scope_arm()

// Копирует захваченные отсчёты по порядку (самый старый первым), возвращает их число
uint16_t scope_copy(uint16_t* dst);

#endif
