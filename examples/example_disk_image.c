/*
 * Файл проекта Keil: подключает disk_image.c выбранного примера.
 * Пример выбирается целью (Options for Target -> C/C++ -> Define: EXAMPLE_...),
 * исходники примеров лежат в examples/<имя>/. Правьте их там, а не здесь.
 */

#if defined(EXAMPLE_MSC_WEBUSB)
  #include "msc-webusb/disk_image.c"
#endif

typedef int example_unit_is_not_empty_t;   /* чтобы файл не был пустым при выключенном примере */
