/*
 * Файл проекта Keil: подключает main.c выбранного примера.
 * Пример выбирается целью (Options for Target -> C/C++ -> Define: EXAMPLE_...),
 * исходники примеров лежат в examples/<имя>/. Правьте их там, а не здесь.
 */

#if defined(EXAMPLE_CDC)
  #include "cdc-only/main.c"
#elif defined(EXAMPLE_HID)
  #include "hid-only/main.c"
#elif defined(EXAMPLE_MSC)
  #include "msc-only/main.c"
#elif defined(EXAMPLE_MSC_HID)
  #include "msc-hid/main.c"
#elif defined(EXAMPLE_WEBUSB)
  #include "webusb/main.c"
#elif defined(EXAMPLE_MSC_WEBUSB)
  #include "msc-webusb/main.c"
#endif

typedef int example_unit_is_not_empty_t;   /* чтобы файл не был пустым при выключенном примере */
