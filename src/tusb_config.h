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
//   HID_MODE_ECHO     - тест: устройство возвращает назад 64-байтные отчёты
//                       (для стресс-теста tools/hid_stress.py)
// Менять здесь или в Keil: Options for Target -> C/C++ -> Define: HID_MODE=2
//--------------------------------------------------------------------
#define HID_MODE_MOUSE     1
#define HID_MODE_KEYBOARD  2
#define HID_MODE_ECHO      3

#ifndef HID_MODE
#define HID_MODE           HID_MODE_MOUSE
#endif

// Размер буфера конечной точки HID-отчёта: 8 байт для мыши/клавиатуры, 64 для эха
#if HID_MODE == HID_MODE_ECHO
#define CFG_TUD_HID_EP_BUFSIZE  64
#else
#define CFG_TUD_HID_EP_BUFSIZE  8
#endif

#ifdef __cplusplus
}
#endif

#endif