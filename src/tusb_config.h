#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUSB_DEBUG          0
#define CFG_TUSB_OS             OPT_OS_NONE
#define CFG_TUSB_MEM_ALIGN      __attribute__ ((aligned(4)))

#define CFG_TUD_ENABLED         1
#define CFG_TUSB_RHPORT0_MODE   OPT_MODE_DEVICE
#define CFG_TUD_ENDPOINT0_SIZE  64
#define CFG_TUSB_SPEED          OPT_MODE_FULL_SPEED

//--------------------------------------------------------------------
// Включённые классы устройства
//--------------------------------------------------------------------
// HID (мышь/клавиатура)
#define CFG_TUD_HID            1
// Остальные классы отключены для экономии памяти
#define CFG_TUD_MIDI           0
#define CFG_TUD_VENDOR         0

// Размер буфера конечной точки HID-отчёта (для клавиатуры 8 байт)
#define CFG_TUD_HID_EP_BUFSIZE  8

#ifdef __cplusplus
}
#endif

#endif