/*
 * Файл проекта Keil: подключает disk_image.c выбранного примера.
 * Пример выбирается макросом (Options for Target -> C/C++ -> Define: EXAMPLE_...
 * - определенный макрос активируется при выборе требуемого примера из "Select Target"),
 * исходники примеров лежат в examples/<имя>/.
 */

#if defined(EXAMPLE_MSC_WEBUSB)
  #include "msc-webusb/disk_image.c"
#endif

typedef int example_unit_is_not_empty_t;   /* чтобы файл не был пустым при отсутствии выбора какого-либо из примеров */
