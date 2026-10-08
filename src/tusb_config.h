#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

// Минимальная конфигурация для MDR32FxQI с 32KB RAM
#define CFG_TUSB_DEBUG          0
#define CFG_TUSB_OS             OPT_OS_NONE
#define CFG_TUSB_MEM_ALIGN      __attribute__ ((aligned(4)))

#define CFG_TUD_ENABLED         1
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE
#define CFG_TUD_ENDPOINT0_SIZE  64
#define CFG_TUSB_SPEED          OPT_MODE_FULL_SPEED

// Только vendor-класс (WebUSB, драйвер WinUSB ставится Windows сам)
#define USB_SETUP_LOG           1   // отладка: журнал SETUP в порту (usb_setup_log)
#define CFG_TUD_CDC             0
#define CFG_TUD_HID             0
#define CFG_TUD_MIDI            0
#define CFG_TUD_VENDOR          1

// Размер буферов vendor-класса (WebUSB)
#define CFG_TUD_VENDOR_RX_BUFSIZE  64
#define CFG_TUD_VENDOR_TX_BUFSIZE  64


#endif
