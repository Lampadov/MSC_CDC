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

//--------------------------------------------------------------------
// Тип HID-устройства: выбирается ОДНИМ макросом HID_MODE
//   HID_MODE_MOUSE    - мышь: кнопки платы двигают курсор
//   HID_MODE_KEYBOARD - клавиатура: Enter и стрелки
// Менять здесь или в Keil: Options for Target -> C/C++ -> Define: HID_MODE=2
//--------------------------------------------------------------------
#define HID_MODE_MOUSE     1
#define HID_MODE_KEYBOARD  2

#ifndef HID_MODE
#define HID_MODE           HID_MODE_MOUSE
#endif

#if (HID_MODE != HID_MODE_MOUSE) && (HID_MODE != HID_MODE_KEYBOARD)
#error "HID_MODE: допустимы HID_MODE_MOUSE (1) и HID_MODE_KEYBOARD (2)"
#endif

// Размер буфера конечной точки HID-отчёта (мышь/клавиатура: 8 байт)
#define CFG_TUD_HID_EP_BUFSIZE  8

#ifdef __cplusplus
}
#endif

#endif