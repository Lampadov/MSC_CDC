/*
 * Файл проекта Keil: подключает msc_disk.c выбранного примера.
 * Пример выбирается целью (Options for Target -> C/C++ -> Define: EXAMPLE_...),
 * исходники примеров лежат в examples/<имя>/. Правьте их там, а не здесь.
 */

#if defined(EXAMPLE_MSC)
  #include "msc-only/msc_disk.c"
#elif defined(EXAMPLE_MSC_HID)
  #include "msc-hid/msc_disk.c"
#elif defined(EXAMPLE_MSC_WEBUSB)
  #include "msc-webusb/msc_disk.c"
#endif

typedef int example_unit_is_not_empty_t;   /* чтобы файл не был пустым при выключенном примере */
