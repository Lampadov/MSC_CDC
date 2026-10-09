/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
 
#include "tusb.h"

/*
 * Дескрипторы составного устройства: флешка (MSC, интерфейс 0) + WebUSB (vendor, интерфейс 1).
 *
 * Флешка содержит страницу управления платой. Страница открывается из браузера прямо с диска
 * платы и по WebUSB (интерфейс 1) общается с этой же платой.
 *
 * Как Windows сама, без драйвера и без прав администратора, подключает WinUSB (Microsoft OS 1.0):
 *   1. Хост читает строку 0xEE. В ней "MSFT100" и код vendor-запроса (MS_VENDOR_CODE).
 *   2. По этому коду хост запрашивает Compatible ID. Мы отвечаем "WINUSB", и Windows подключает winusb.sys.
 *   3. Вторым запросом хост получает GUID интерфейса: по нему браузер находит устройство.
 * Linux и macOS драйвер не требуют.
 *
 * Если Windows уже видела устройство без ответа на эти запросы, она запоминает результат
 * для связки VID/PID/bcdDevice. При отладке дескрипторов меняйте USB_PID или bcdDevice.
 */

#define USB_VID   0xCAFE         // должен совпадать с USB_VID в web/app.js
#define USB_PID   0x4016

// Коды MS OS 1.0
#define MS_VENDOR_CODE     0x03  // bRequest vendor-запросов; сообщается хосту в строке 0xEE
#define MS_REQ_COMPAT_ID   4     // wIndex: запрос Compatible ID
#define MS_REQ_PROPERTIES  5     // wIndex: запрос свойств (GUID интерфейса)

// Команда страницы: установить светодиоды (wValue - маска). Адресована интерфейсу 1
#define REQ_SET_LEDS       0x10

// Реализация в main.c
void leds_set(uint8_t mask);

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    .bDeviceClass       = 0x00,     // класс задан на уровне интерфейса
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0101,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

// Справочный дескриптор для high-speed; у full-speed устройства хост его всё равно запрашивает
static uint8_t const desc_device_qualifier[] = {
    0x0A, TUSB_DESC_DEVICE_QUALIFIER,
    U16_TO_U8S_LE(0x0200),              // bcdUSB
    0x00, 0x00, 0x00,                   // класс, подкласс, протокол
    CFG_TUD_ENDPOINT0_SIZE,
    0x01,                               // число конфигураций
    0x00                                // резерв
};

uint8_t const *tud_descriptor_device_qualifier_cb(void) {
  return desc_device_qualifier;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
enum {
  ITF_NUM_MSC = 0,       // флешка
  ITF_NUM_VENDOR,        // WebUSB
  ITF_NUM_TOTAL
};

// Каждая точка работает в одном направлении (у контроллера один EPRDY на точку)
#define EPNUM_MSC_OUT     0x01   // EP1 OUT - данные от хоста на флешку
#define EPNUM_MSC_IN      0x82   // EP2 IN  - данные с флешки
#define EPNUM_VENDOR_IN   0x83   // EP3 IN  - кадры состояния для страницы (команды идут через EP0)

// Интерфейс WebUSB с одной точкой IN. Класс 0xFF - vendor, поэтому браузер разрешает его занять
#define VENDOR_IN_ONLY_DESC_LEN  (9 + 7)
#define VENDOR_IN_ONLY_DESCRIPTOR(_itfnum, _epin, _epsize) \
  9, TUSB_DESC_INTERFACE, _itfnum, 0, 1, TUSB_CLASS_VENDOR_SPECIFIC, 0x00, 0x00, 0, \
  7, TUSB_DESC_ENDPOINT, _epin, TUSB_XFER_BULK, U16_TO_U8S_LE(_epsize), 0

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN + VENDOR_IN_ONLY_DESC_LEN)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),

    VENDOR_IN_ONLY_DESCRIPTOR(ITF_NUM_VENDOR, EPNUM_VENDOR_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_configuration;
}

//--------------------------------------------------------------------+
// Microsoft OS 1.0: "подключи WinUSB к этому интерфейсу"
//--------------------------------------------------------------------+

// Compatible ID: драйвер WinUSB только для интерфейса 1 (WebUSB). Флешкой занимается штатный драйвер Windows
static uint8_t const desc_ms_compat_id[] = {
    U32_TO_U8S_LE(40), U16_TO_U8S_LE(0x0100), U16_TO_U8S_LE(MS_REQ_COMPAT_ID),   // длина, версия, тип
    1, 0, 0, 0, 0, 0, 0, 0,                                                      // одна функция
    ITF_NUM_VENDOR, 1,                                                           // интерфейс, резерв
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,                                    // Compatible ID
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                              // Sub-compatible ID
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Свойство реестра DeviceInterfaceGUID (относится к интерфейсу 1) = {975F44D9-0D08-43FD-8B3E-127CA8AFFF9D}
static uint8_t const desc_ms_properties[] = {
    U32_TO_U8S_LE(142), U16_TO_U8S_LE(0x0100), U16_TO_U8S_LE(MS_REQ_PROPERTIES), // длина, версия, тип
    U16_TO_U8S_LE(1),                                                            // одно свойство
    U32_TO_U8S_LE(132), U32_TO_U8S_LE(1),                                        // размер свойства, REG_SZ
    U16_TO_U8S_LE(40),                                                           // длина имени, байт
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0, 't', 0, 'e', 0,
    'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0, 'G', 0, 'U', 0, 'I', 0, 'D', 0, 0, 0,
    U32_TO_U8S_LE(78),                                                           // длина значения, байт
    '{', 0, '9', 0, '7', 0, '5', 0, 'F', 0, '4', 0, '4', 0, 'D', 0, '9', 0, '-', 0,
    '0', 0, 'D', 0, '0', 0, '8', 0, '-', 0, '4', 0, '3', 0, 'F', 0, 'D', 0, '-', 0,
    '8', 0, 'B', 0, '3', 0, 'E', 0, '-', 0, '1', 0, '2', 0, '7', 0, 'C', 0, 'A', 0,
    '8', 0, 'A', 0, 'F', 0, 'F', 0, 'F', 0, '9', 0, 'D', 0, '}', 0, 0, 0
};

TU_VERIFY_STATIC(sizeof(desc_ms_compat_id) == 40, "Incorrect size");
TU_VERIFY_STATIC(sizeof(desc_ms_properties) == 142, "Incorrect size");

// Vendor-запросы хоста: Compatible ID, свойства и команда страницы
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
  if (stage != CONTROL_STAGE_SETUP) return true;     // нужен только этап SETUP

  if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR) return false;

  if (request->bRequest == REQ_SET_LEDS) {           // страница управляет светодиодами
    leds_set((uint8_t) request->wValue);
    return tud_control_status(rhport, request);
  }

  if (request->bRequest != MS_VENDOR_CODE) {
    return false;                                    // неизвестный запрос: STALL
  }

  if (request->wIndex == MS_REQ_COMPAT_ID) {
    return tud_control_xfer(rhport, request, (void *) (uintptr_t) desc_ms_compat_id, sizeof(desc_ms_compat_id));
  }
  if (request->wIndex == MS_REQ_PROPERTIES) {
    return tud_control_xfer(rhport, request, (void *) (uintptr_t) desc_ms_properties, sizeof(desc_ms_properties));
  }
  return false;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
#define STRID_MS_OS  0xEE        // строка, по которой хост узнаёт про MS OS 1.0

static char const *string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 }, // 0: язык - английский (0x0409)
    "Milandr",                     // 1: производитель
    "Milandr WebUSB Drive",         // 2: изделие
    "MDR32-MSCWEB-0001",           // 3: серийный номер
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  size_t chr_count;

  if (index == 0) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else if (index == STRID_MS_OS) {                 // "MSFT100" и код vendor-запроса
    static const char signature[] = "MSFT100";
    for (size_t i = 0; i < 7; i++) _desc_str[1 + i] = signature[i];
    _desc_str[8] = MS_VENDOR_CODE;
    chr_count    = 8;
  } else {
    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;

    const char *str = string_desc_arr[index];
    chr_count = strlen(str);
    if (chr_count > 32) chr_count = 32;

    for (size_t i = 0; i < chr_count; i++) {         // ASCII -> UTF-16
      _desc_str[1 + i] = str[i];
    }
  }

  // первое слово: длина в байтах (с заголовком) и тип дескриптора
  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
