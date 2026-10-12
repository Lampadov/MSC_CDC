# TinyUSB на К1986ВЕ9х (MDR32F9Q2I): набор примеров в одном проекте

Один проект Keil `USB_EXAMPLES.uvprojx` с несколькими целями (Target). Код TinyUSB и драйвер контроллера `src/tusb_mdr32fxqi.c` общие для всех примеров, у каждого примера в `examples/<имя>/` лежат только его файлы: `main.c`, `usb_descriptors.c`, `tusb_config.h` (и `msc_disk.c`, `disk_image.c`, где они нужны).

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

`HID_MOUSE` и `HID_KEYBOARD` собираются из одних исходников; отличаются макросом `HID_MODE` (во втором `HID_MODE=2` в Define цели). Режим входит в PID, поэтому Windows считает их разными устройствами.

Подробности: `examples/webusb/README.md` и `examples/msc-webusb/README.md`. В `WEBUSB` страницу открывают с локального сервера, в `MSC_WEBUSB` она лежит на самой плате.

## Сборка

1. Откройте `USB_EXAMPLES.uvprojx`, выберите цель в выпадающем списке.
2. Build, Download. Образ каждой цели лежит в `Objects\<ЦЕЛЬ>\`, листинги в `Listings\<ЦЕЛЬ>\`, поэтому цели не затирают друг друга.
3. Цели собираются в бесплатной версии Keil (32 КБ) при условии, что код укладывается в лимит; размер `MSC_WEBUSB` близок к границе.

Новая цель: скопируйте существующую (Project -> Manage -> Components, Environment -> вкладка Targets), замените каталог примера первым элементом в Include Path и файлы в группе Example.

## Тесты

Все скрипты на Python 3 (Windows; `msc_stress.py` и `cdc_stress.py` работают и на Linux). Каждый тест запускайте на плате с соответствующей целью.

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

Для `MSC_WEBUSB` запускайте `msc_stress.py raw` и `webusb_stress.py msc-webusb` одновременно: так проверяется составное устройство под нагрузкой.

## Образ диска примера MSC_WEBUSB

После правки страницы в `examples/msc-webusb/web/` выполните `python tools/make_disk.py` и пересоберите цель.

## Состав репозитория

    src/                 TinyUSB v0.21 и порт для MDR32F9Q2I (общие)
    examples/<имя>/      файлы конкретного примера
    tools/               тесты и make_disk.py
    RTE/                 файлы компонентов Keil (для каждой цели свой RTE_Components.h)
    USB_EXAMPLES.uvprojx проект с семью целями

Остальные ветки репозитория (`cdc-only`, `hid-only`, `msc-only`, `msc-hid`, `webusb`, `msc-webusb`) содержат те же примеры по одному: каждый со своим проектом Keil.
