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
 * Диск только для чтения. Образ FAT12 с файлами страницы (index.htm, app.js, README.TXT)
 * лежит во флеш-памяти (src/disk_image.c). Создаёт его tools/make_disk.py из каталога web/,
 * оперативная память под диск не нужна.
 */

enum { DISK_BLOCK_SIZE = 512 };

extern const uint8_t  disk_image[];
extern const uint32_t disk_image_blocks;

static bool ejected;

//--------------------------------------------------------------------+
// Обратные вызовы MSC
//--------------------------------------------------------------------+
uint32_t tud_msc_inquiry2_cb(uint8_t lun, scsi_inquiry_resp_t* inquiry_resp, uint32_t bufsize)
{
  (void)lun; (void)bufsize;
  strncpy((char*)inquiry_resp->vendor_id, "Milandr", 8);
  strncpy((char*)inquiry_resp->product_id, "WebUSB Drive", 16);
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
  *block_count = disk_image_blocks;
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
  if (lba * DISK_BLOCK_SIZE + offset + bufsize > disk_image_blocks * DISK_BLOCK_SIZE) return -1;

  memcpy(buffer, disk_image + lba * DISK_BLOCK_SIZE + offset, bufsize);
  return (int32_t)bufsize;
}

// Диск защищён от записи: Windows показывает его как заблокированный
bool tud_msc_is_writable_cb(uint8_t lun)
{
  (void)lun;
  return false;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
  (void)lun; (void)lba; (void)offset; (void)buffer; (void)bufsize;
  tud_msc_set_sense(lun, SCSI_SENSE_DATA_PROTECT, 0x27, 0x00);   // запись запрещена
  return -1;
}

// Остальные SCSI-команды не поддерживаются
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize)
{
  (void)scsi_cmd; (void)buffer; (void)bufsize;
  tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);   // неверная команда
  return -1;
}
