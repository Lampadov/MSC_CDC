/*
 * Файл проекта Keil: подключает usb_descriptors.c выбранного примера.
 * Пример выбирается целью (Options for Target -> C/C++ -> Define: EXAMPLE_...),
 * исходники примеров лежат в examples/<имя>/. Правьте их там, а не здесь.
 */

#if defined(EXAMPLE_CDC)
  #include "cdc-only/usb_descriptors.c"
#elif defined(EXAMPLE_HID)
  #include "hid-only/usb_descriptors.c"
#elif defined(EXAMPLE_MSC)
  #include "msc-only/usb_descriptors.c"
#elif defined(EXAMPLE_MSC_HID)
  #include "msc-hid/usb_descriptors.c"
#elif defined(EXAMPLE_WEBUSB_ONLY)
  #include "webusb-only/usb_descriptors.c"
#elif defined(EXAMPLE_MSC_WEBUSB)
  #include "msc-webusb/usb_descriptors.c"
#endif

typedef int example_unit_is_not_empty_t;   /* чтобы файл не был пустым при выключенном примере */
