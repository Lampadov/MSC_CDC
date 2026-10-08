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
 * Дескрипторы WebUSB-устройства: один vendor-интерфейс с двумя bulk-точками.
 *
 * Чтобы браузер (Chrome/Edge) увидел плату без драйвера и без установки программ,
 * нужны две вещи:
 *   1. BOS-дескриптор с метками WebUSB и Microsoft OS 2.0 (bcdUSB не ниже 2.10,
 *      иначе Windows не запросит BOS);
 *   2. набор Microsoft OS 2.0, который просит Windows подключить драйвер WinUSB.
 */

#define USB_VID   0xCAFE
#define USB_PID   0x4011
#define USB_BCD   0x0210     // 2.10: хост запрашивает BOS-дескриптор

// Адрес страницы, которую Chrome предложит открыть при подключении платы.
// Для http допустим только localhost; для боевой страницы нужен https (scheme = 1).
#define WEBUSB_URL     "localhost:8000"
#define WEBUSB_SCHEME  0     // 0 = http, 1 = https

// Коды vendor-запросов (bRequest), которыми хост достаёт дескрипторы WebUSB и MS OS 2.0
enum {
  VENDOR_REQUEST_WEBUSB    = 1,
  VENDOR_REQUEST_MICROSOFT = 2
};

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
static tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,

    .bDeviceClass       = 0x00,     // класс задан на уровне интерфейса
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0101,   // Windows запоминает отсутствие MS OS 2.0 для VID/PID/bcdDevice: при отладке увеличивайте

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x00,

    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
enum {
  ITF_NUM_VENDOR = 0,
  ITF_NUM_TOTAL
};

// Каждая точка работает в одном направлении (у контроллера один EPRDY на точку)
#define EPNUM_VENDOR_OUT  0x01   // EP1 OUT - команды от страницы
#define EPNUM_VENDOR_IN   0x82   // EP2 IN  - данные к странице

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN)

static uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 0, EPNUM_VENDOR_OUT, EPNUM_VENDOR_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;
  return desc_configuration;
}

//--------------------------------------------------------------------+
// BOS Descriptor: сообщает хосту, что устройство поддерживает WebUSB и MS OS 2.0
//--------------------------------------------------------------------+
#define MS_OS_20_DESC_LEN  0xB2

#define BOS_TOTAL_LEN  (TUD_BOS_DESC_LEN + TUD_BOS_WEBUSB_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

static uint8_t const desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 2),
    TUD_BOS_WEBUSB_DESCRIPTOR(VENDOR_REQUEST_WEBUSB, 1),            // 1 - номер URL страницы
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT)
};

// Отладка энумерации: биты, которые main.c показывает светодиодами, пока устройство не настроено
// бит 0 - хост прочитал BOS, бит 1 - хост запросил MS OS 2.0, бит 2 - хост запросил URL WebUSB
volatile uint8_t usb_trace;

uint8_t const *tud_descriptor_bos_cb(void) {
  usb_trace |= 1;
  return desc_bos;
}

//--------------------------------------------------------------------+
// Microsoft OS 2.0: "подключи WinUSB к этому интерфейсу"
//--------------------------------------------------------------------+
static uint8_t const desc_ms_os_20[] = {
    // Заголовок набора: длина, тип, версия Windows (8.1), общая длина
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000),
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

    // Подмножество конфигурации: длина, тип, номер конфигурации, резерв, длина подмножества
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),

    // Подмножество функции: длина, тип, первый интерфейс, резерв, длина подмножества
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_NUM_VENDOR, 0,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),

    // Compatible ID: драйвер WinUSB
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // Свойство реестра DeviceInterfaceGUIDs (по нему программы находят устройство)
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A),                  // REG_MULTI_SZ, длина имени 42 байта
    'D', 0, 'e', 0, 'v', 0, 'i', 0, 'c', 0, 'e', 0, 'I', 0, 'n', 0, 't', 0, 'e', 0,
    'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0, 'G', 0, 'U', 0, 'I', 0, 'D', 0, 's', 0, 0, 0,
    U16_TO_U8S_LE(0x0050),                                         // длина значения 80 байт
    '{', 0, '9', 0, '7', 0, '5', 0, 'F', 0, '4', 0, '4', 0, 'D', 0, '9', 0, '-', 0,
    '0', 0, 'D', 0, '0', 0, '8', 0, '-', 0, '4', 0, '3', 0, 'F', 0, 'D', 0, '-', 0,
    '8', 0, 'B', 0, '3', 0, 'E', 0, '-', 0, '1', 0, '2', 0, '7', 0, 'C', 0, 'A', 0,
    '8', 0, 'A', 0, 'F', 0, 'F', 0, 'F', 0, '9', 0, 'D', 0, '}', 0, 0, 0, 0, 0
};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect size");

//--------------------------------------------------------------------+
// Адрес страницы WebUSB (Landing Page URL)
//--------------------------------------------------------------------+
static struct {
  uint8_t len;                           // длина дескриптора
  uint8_t type;                          // 3 = WebUSB URL
  uint8_t scheme;                        // 0 = http://, 1 = https://
  char    url[sizeof(WEBUSB_URL) - 1];   // адрес без схемы
} const desc_url = {
    sizeof(desc_url), 3, WEBUSB_SCHEME, WEBUSB_URL
};

//--------------------------------------------------------------------+
// Vendor-запросы: хост просит URL страницы и набор MS OS 2.0
//--------------------------------------------------------------------+
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
  if (stage != CONTROL_STAGE_SETUP) return true;     // нужен только этап SETUP

  if (request->bmRequestType_bit.type != TUSB_REQ_TYPE_VENDOR) return false;

  switch (request->bRequest) {
    case VENDOR_REQUEST_WEBUSB:
      usb_trace |= 4;
      return tud_control_xfer(rhport, request, (void *) (uintptr_t) &desc_url, desc_url.len);

    case VENDOR_REQUEST_MICROSOFT:
      if (request->wIndex == 7) {                    // 7 = запрос дескриптора MS OS 2.0
        usb_trace |= 2;
        return tud_control_xfer(rhport, request, (void *) (uintptr_t) desc_ms_os_20, MS_OS_20_DESC_LEN);
      }
      return false;

    default:
      return false;
  }
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
static char const *string_desc_arr[] = {
    (const char[]) { 0x09, 0x04 }, // 0: язык - английский (0x0409)
    "Milandr",                     // 1: производитель
    "Milandr WebUSB Demo",         // 2: изделие
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  size_t chr_count;

  if (index == 0) {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;

    const char *str = string_desc_arr[index];
    chr_count = strlen(str);
    if (chr_count > 32) chr_count = 32;

    for (size_t i = 0; i < chr_count; i++) {       // ASCII -> UTF-16
      _desc_str[1 + i] = str[i];
    }
  }

  // первое слово: длина в байтах (с заголовком) и тип дескриптора
  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return _desc_str;
}
