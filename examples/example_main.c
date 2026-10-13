/*
 * Файл проекта Keil: подключает main.c выбранного примера.
 * Пример выбирается макросом (Options for Target -> C/C++ -> Define: EXAMPLE_...
 * - определенный макрос активируется при выборе требуемого примера из "Select Target"),
 * исходники примеров лежат в examples/<имя>/.
 */

#if defined(EXAMPLE_CDC)
  #include "cdc-only/main.c"
#elif defined(EXAMPLE_HID)
  #include "hid-only/main.c"
#elif defined(EXAMPLE_MSC)
  #include "msc-only/main.c"
#elif defined(EXAMPLE_MSC_HID)
  #include "msc-hid/main.c"
#elif defined(EXAMPLE_WEBUSB_ONLY)
  #include "webusb-only/main.c"
#elif defined(EXAMPLE_MSC_WEBUSB)
  #include "msc-webusb/main.c"
#endif

typedef int example_unit_is_not_empty_t;   /* чтобы файл не был пустым при отсутствии выбора какого-либо из примеров */
