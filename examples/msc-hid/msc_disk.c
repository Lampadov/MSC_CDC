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

#include <string.h>

#include "tusb.h"

/*
 * RAM-диск 8 КБ с файловой системой FAT12. На нём два файла:
 *   README.TXT - описание проекта
 *   TEXT.TXT   - текст, который плата "набирает на клавиатуре" по кнопке SELECT (см. main.c)
 *
 * Раскладка секторов (512 байт, один сектор на кластер):
 *   0 загрузочный сектор | 1 таблица FAT | 2 корневой каталог | 3.. данные файлов
 */

enum {
  DISK_BLOCK_NUM  = 16,    // меньше 8 КБ Windows не монтирует
  DISK_BLOCK_SIZE = 512
};

enum {
  SECTOR_BOOT = 0,
  SECTOR_FAT  = 1,
  SECTOR_ROOT = 2,
  SECTOR_DATA = 3          // здесь лежит кластер 2 (кластеры нумеруются с 2)
};

// "\xEF\xBB\xBF" - метка UTF-8, чтобы Блокнот правильно показал кириллицу в названии.
// К1986ВЕ9х записано байтами UTF-8, чтобы текст не зависел от кодировки этого файла.
#define README_TEXT \
  "\xEF\xBB\xBF" \
  "Milandr \xD0\x9A" "1986" "\xD0\x92\xD0\x95" "9" "\xD1\x85" " - USB flash drive + keyboard (TinyUSB).\r\n" \
  "\r\n" \
  "This device is a flash drive and a keyboard at the same time.\r\n" \
  "\r\n" \
  "Try it: edit TEXT.TXT and save it, click into any text field\r\n" \
  "and press SELECT on the board - the board types the text for you.\r\n" \
  "(English keyboard layout, plain ASCII text, up to 512 characters.)\r\n" \
  "\r\n" \
  "Questions: support@milandr.ru\r\n"

#define TEXT_FILE_NAME  "TEXT    TXT"
#define TEXT_DEFAULT    "Hello from Milandr!"

static uint8_t msc_disk[DISK_BLOCK_NUM][DISK_BLOCK_SIZE];
static bool    ejected;

//--------------------------------------------------------------------+
// Создание образа диска
//--------------------------------------------------------------------+
// Загрузочный сектор: параметры FAT12 (16 секторов по 512 байт, один сектор на кластер,
// 1 копия FAT на 1 сектор, 16 записей в корневом каталоге), метка тома "Milandr MSC"
static const uint8_t boot_sector[62] = {
  0xEB, 0x3C, 0x90, 'M', 'S', 'D', 'O', 'S', '5', '.', '0',  // переход, имя изготовителя
  0x00, 0x02,        // байт в секторе: 512
  0x01,              // секторов в кластере
  0x01, 0x00,        // зарезервировано секторов (загрузочный)
  0x01,              // копий FAT
  0x10, 0x00,        // записей в корневом каталоге
  0x10, 0x00,        // всего секторов: 16
  0xF8,              // тип носителя
  0x01, 0x00,        // секторов в FAT
  0x01, 0x00,        // секторов на дорожку
  0x01, 0x00,        // головок
  0x00, 0x00, 0x00, 0x00,   // скрытых секторов
  0x00, 0x00, 0x00, 0x00,   // всего секторов (32-битное поле, не используется)
  0x80, 0x00, 0x29,  // номер диска, расширенная сигнатура
  0x34, 0x12, 0x00, 0x00,   // серийный номер тома
  'M', 'i', 'l', 'a', 'n', 'd', 'r', ' ', 'M', 'S', 'C',   // метка тома
  'F', 'A', 'T', '1', '2', ' ', ' ', ' '                   // тип файловой системы
};

// Запись в корневом каталоге (32 байта): имя 8+3, атрибут, дата, первый кластер, размер
static void add_file(int index, const char* name83, int cluster, const char* text, int size)
{
  uint8_t* e = &msc_disk[SECTOR_ROOT][index * 32];

  memcpy(e, name83, 11);
  e[11] = 0x20;                              // атрибут "архивный" (обычный файл)
  e[24] = 0x21; e[25] = 0x5A;                // дата 01.01.2025
  e[26] = (uint8_t)cluster;                  // первый кластер
  e[28] = (uint8_t)size;                     // размер файла в байтах
  e[29] = (uint8_t)(size >> 8);
  memcpy(msc_disk[SECTOR_DATA + cluster - 2], text, size);
}

void msc_disk_init(void)
{
  memcpy(msc_disk[SECTOR_BOOT], boot_sector, sizeof(boot_sector));
  msc_disk[SECTOR_BOOT][510] = 0x55;         // сигнатура загрузочного сектора
  msc_disk[SECTOR_BOOT][511] = 0xAA;

  // FAT12: элементы 0 и 1 служебные, файлы занимают по одному кластеру (2 и 3) = конец цепочки
  static const uint8_t fat[] = { 0xF8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  memcpy(msc_disk[SECTOR_FAT], fat, sizeof(fat));

  memcpy(msc_disk[SECTOR_ROOT], "Milandr MSC\x08", 12);   // запись 0 - метка тома
  add_file(1, "README  TXT", 2, README_TEXT, sizeof(README_TEXT) - 1);
  add_file(2, TEXT_FILE_NAME, 3, TEXT_DEFAULT, sizeof(TEXT_DEFAULT) - 1);
}

//--------------------------------------------------------------------+
// Чтение TEXT.TXT для main.c. Файл ищем в каталоге заново при каждом вызове: Windows может
// перенести его на другой кластер при сохранении.
// Возвращает число скопированных в out байт (не больше max и не больше одного сектора).
//--------------------------------------------------------------------+
int msc_disk_read_text(char* out, int max)
{
  for (int i = 0; i < 16; i++) {
    const uint8_t* e = &msc_disk[SECTOR_ROOT][i * 32];
    if (memcmp(e, TEXT_FILE_NAME, 11) != 0) continue;

    int cluster = e[26] | (e[27] << 8);
    int size    = e[28] | (e[29] << 8);
    int sector  = SECTOR_DATA + cluster - 2;
    if (cluster < 2 || sector >= DISK_BLOCK_NUM) return 0;

    if (size > DISK_BLOCK_SIZE) size = DISK_BLOCK_SIZE;
    if (size > max) size = max;
    memcpy(out, msc_disk[sector], size);
    return size;
  }
  return 0;
}

//--------------------------------------------------------------------+
// Обратные вызовы MSC
//--------------------------------------------------------------------+
uint32_t tud_msc_inquiry2_cb(uint8_t lun, scsi_inquiry_resp_t* inquiry_resp, uint32_t bufsize)
{
  (void)lun; (void)bufsize;
  strncpy((char*)inquiry_resp->vendor_id, "Milandr", 8);
  strncpy((char*)inquiry_resp->product_id, "Mass Storage", 16);
  strncpy((char*)inquiry_resp->product_rev, "1.0", 4);
  return sizeof(scsi_inquiry_resp_t);
}

// Диск готов, пока его не извлекли
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
  if (ejected) {
    return tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);   // носитель не найден
  }
  return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)
{
  (void)lun;
  *block_count = DISK_BLOCK_NUM;
  *block_size  = DISK_BLOCK_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
  (void)lun; (void)power_condition;
  if (load_eject && !start) ejected = true;          // "Безопасное извлечение"
  return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
  (void)lun;
  if (lba * DISK_BLOCK_SIZE + offset + bufsize > sizeof(msc_disk)) return -1;

  memcpy(buffer, msc_disk[lba] + offset, bufsize);
  return (int32_t)bufsize;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
  (void)lun;
  return true;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
  (void)lun;
  if (lba * DISK_BLOCK_SIZE + offset + bufsize > sizeof(msc_disk)) return -1;

  memcpy(msc_disk[lba] + offset, buffer, bufsize);
  return (int32_t)bufsize;
}

// Остальные SCSI-команды не поддерживаются
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize)
{
  (void)scsi_cmd; (void)buffer; (void)bufsize;
  tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);   // неверная команда
  return -1;
}
