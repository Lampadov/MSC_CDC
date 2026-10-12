# TinyUSB на К1986ВЕ9х (MDR32F9Q2I): набор примеров в одном проекте

Один проект Keil `USB_EXAMPLES.uvprojx`, пример выбирается целью (Target). Код TinyUSB и драйвер контроллера `src/tusb_mdr32fxqi.c` общие для всех примеров, у каждого примера в `examples/<имя>/` лежат его файлы: `main.c`, `usb_descriptors.c`, `tusb_config.h` (и `msc_disk.c`, `disk_image.c`, где они нужны).

## Примеры

| Цель Keil | Каталог | Что делает плата | VID:PID | Тест |
|---|---|---|---|---|
| `CDC_ONLY` | `examples/cdc-only` | виртуальный COM-порт | CAFE:4001 | `cdc_stress.py` |
| `HID_MOUSE` | `examples/hid-only` | мышь, кнопки платы двигают курсор; UP+DOWN рисует круг | ABAB:4004 | `hid_check.py mouse` |
| `HID_KEYBOARD` | `examples/hid-only` | клавиатура; UP+DOWN печатает "Hello from Milandr!" | ABAB:4024 | `hid_check.py keyboard` |
| `MSC_ONLY` | `examples/msc-only` | флешка (диск в RAM, метка "Milandr MSC") | CAFE:4002 | `msc_stress.py` |
| `MSC_HID` | `examples/msc-hid` | флешка + клавиатура; SELECT печатает содержимое `TEXT.TXT` | CAFE:4006 | `msc_stress.py`, `hid_check.py` |
| `WEBUSB` | `examples/webusb` | WebUSB без флешки: страница управления платой запускается с локального сервера, светодиоды, кнопки, АЦП, игра | CAFE:4015 | `webusb_stress.py webusb` |
| `MSC_WEBUSB` | `examples/msc-webusb` | флешка только для чтения с веб-страницей управления платой + WebUSB (метка "Milandr WEB") | CAFE:4016 | `msc_stress.py raw`, `webusb_stress.py msc-webusb` |

Мышь и клавиатура собираются из одних исходников двумя целями; отличаются макросом `HID_MODE` (в цели `HID_KEYBOARD` в Define добавлено `HID_MODE=2`). Режим входит в PID, поэтому Windows считает их разными устройствами.

Подробности: `examples/webusb/README.md` и `examples/msc-webusb/README.md`. В `WEBUSB` страницу открывают с локального сервера, в `MSC_WEBUSB` она лежит на самой плате.

## Сборка

1. Откройте `USB_EXAMPLES.uvprojx`, выберите цель в выпадающем списке.
2. Build, Download. Результат лежит в `Objects\<ЦЕЛЬ>\`, листинги в `Listings\<ЦЕЛЬ>\`, поэтому цели не мешают друг другу.
3. В бесплатной версии Keil (32 КБ) код должен укладываться в лимит; размер `MSC_WEBUSB` близок к границе.

Как это устроено. Keil не умеет держать у целей разные списки файлов: файлы групп общие для всего проекта. Поэтому список файлов у всех целей одинаковый и короткий, а пример выбирается макросом в Define цели:

- группа `Example`: четыре файла-переходника `examples/example_main.c`, `example_usb_descriptors.c`, `example_msc_disk.c`, `example_disk_image.c`. Каждый содержит только `#include` файла выбранного примера (`#if defined(EXAMPLE_CDC) #include "cdc-only/main.c" ...`); если примеру файл не нужен, переходник пустой;
- группа `TinyUSB`: ядро и файлы всех четырёх классов. Выключенный в `tusb_config.h` класс компилируется в пустоту.

Макрос примера задан в Options for Target -> C/C++ -> Define (`EXAMPLE_CDC`, `EXAMPLE_HID`, `EXAMPLE_MSC`, `EXAMPLE_MSC_HID`, `EXAMPLE_WEBUSB`, `EXAMPLE_MSC_WEBUSB`). Первым в Include Path стоит `.\examples\<имя>`, поэтому цель находит свой `tusb_config.h`. Исходники примеров правьте в `examples/<имя>/`: Keil открывает их по двойному щелчку на строке `#include` в переходнике.

## Тесты

Все скрипты на Python 3 (Windows; `msc_stress.py` и `cdc_stress.py` работают и на Linux). Каждый тест запускайте на плате с прошивкой соответствующей цели.

**Энумерация** (`tools/enum_test.py`, Windows, права администратора). Скрипт программно отключает и снова включает USB-порт платы (IOCTL_USB_HUB_CYCLE_PORT) и проверяет, что устройство вернулось: узлы запущены без ошибок, число интерфейсов верно, у дисков читается том и содержимое совпадает с эталоном.

    python enum_test.py cdc-only 500 0
    python enum_test.py hid-only 200        # мышь;  hid-keyboard для клавиатуры
    python enum_test.py msc-only
    python enum_test.py msc-hid
    python enum_test.py webusb
    python enum_test.py msc-webusb

Аргументы: пример, число циклов (100), пауза между циклами в секундах (0.5). Для каждого цикла печатается `ok, N мс` или причина отказа.

**Нагрузка**

    python cdc_stress.py COM5 all --minutes 15                  # CDC: поток, пинг-понг, переоткрытие порта
    python msc_stress.py raw \\.\E: --minutes 10                # MSC: чтение случайными кусками (администратор)
    python msc_stress.py files E:\ --minutes 10                 # MSC: файлы (только msc-only и msc-hid)
    python webusb_stress.py webusb --minutes 10                 # WebUSB: кадры, команды LED, дескрипторы (pip install pyusb libusb-package)
    python webusb_stress.py msc-webusb --minutes 10
    python hid_check.py keyboard --runs 20                      # HID: сверка набранного текста
    python hid_check.py mouse --runs 20                         # HID: проверка движения курсора

Для `msc-webusb` запускайте `msc_stress.py raw` и `webusb_stress.py msc-webusb` одновременно: так проверяется составное устройство под нагрузкой.

## Образ диска примера msc-webusb

После правки страницы в `examples/msc-webusb/web/` выполните `python tools/make_disk.py` и пересоберите цель.

## Состав репозитория

    src/                 TinyUSB v0.21 и порт для MDR32F9Q2I (общие)
    examples/<имя>/      файлы конкретного примера; examples/example_*.c: переходники для Keil
    tools/               тесты и make_disk.py
    RTE/                 файлы компонентов Keil (для каждой цели свой RTE_Components.h)
    USB_EXAMPLES.uvprojx проект с семью целями

Остальные ветки репозитория (`cdc-only`, `hid-only`, `msc-only`, `msc-hid`, `webusb`, `msc-webusb`) содержат те же примеры по одному.
