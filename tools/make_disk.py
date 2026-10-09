#!/usr/bin/env python3
"""Собирает образ диска FAT12 из файлов каталога web/ и записывает его в src/disk_image.c.

Образ лежит во флеш-памяти микроконтроллера (const-массив) и отдаётся хосту как флешка только для чтения.
После любых правок в web/ запустите:

    python tools/make_disk.py

и пересоберите проект в Keil.

Раскладка (сектор 512 байт, один сектор на кластер):
    0 загрузочный сектор | 1.. таблица FAT | затем корневой каталог (16 записей) | данные файлов
Имена файлов только 8.3 (длинных имён нет), поэтому страница лежит как INDEX.HTM, а не index.html.
Флаг 0x18 в записи каталога просит Windows показывать имя строчными буквами: index.htm, app.js.
"""
import os
import re
import sys

SECTOR = 512
ROOT_ENTRIES = 16
LABEL = b"Milandr WEB"          # метка тома, 11 символов

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WEB = os.path.join(ROOT, "web")

README = (
    "﻿Milandr K1986VE9x - USB flash drive + WebUSB (TinyUSB).\r\n"
    "\r\n"
    "Open index.htm from this drive in Chrome or Edge,\r\n"
    "press the \"Connect board\" button and choose the board in the list.\r\n"
    "The page controls the LEDs, shows the buttons and the temperature.\r\n"
    "\r\n"
    "The page is stored in the flash memory of the microcontroller.\r\n"
    "The disk is read only.\r\n"
)



def minify_js(src):
    """Убирает комментарии, отступы, пустые строки и пробелы вокруг знаков. Переводы строк остаются,
    поэтому код работает как прежде. В исходнике нет регулярных выражений и конструкций вида "a + +b"."""
    out, strings, i, n = [], [], 0, len(src)
    while i < n:
        c = src[i]
        if c in "'\"`":                                   # строка: прячем под номером, чтобы не трогать
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            strings.append(src[i:j + 1])
            out.append("\x00%d\x00" % (len(strings) - 1))
            i = j + 1
        elif src.startswith("//", i):                      # однострочный комментарий
            while i < n and src[i] != "\n":
                i += 1
        elif src.startswith("/*", i):                      # многострочный комментарий
            i = src.index("*/", i) + 2
        else:
            out.append(c)
            i += 1
    text = "".join(out)
    text = re.sub(r"[ \t]+", " ", text)
    text = re.sub(r" ?([=+\-*/%<>!&|?:,;{}()\[\]]) ?", r"\1", text)   # пробелы вокруг знаков
    text = re.sub(r"\n+", "\n", text.replace("\r", ""))
    text = "\n".join(ln.strip() for ln in text.split("\n")).strip("\n")
    return re.sub("\x00(\\d+)\x00", lambda m: strings[int(m.group(1))], text)


def minify_css(css):
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    css = re.sub(r"\s+", " ", css)
    return re.sub(r"\s*([{};:,>])\s*", r"\1", css).replace(";}", "}").strip()


def minify_html(src):
    src = re.sub(r"<!--.*?-->", "", src, flags=re.S)
    src = re.sub(r"<style>(.*?)</style>", lambda m: "<style>" + minify_css(m.group(1)) + "</style>", src, flags=re.S)
    lines = (ln.strip() for ln in src.split("\n"))
    return "\n".join(ln for ln in lines if ln)


def read(name):
    return open(os.path.join(WEB, name), encoding="utf-8").read()


# Образ собирается из уменьшенных копий файлов (без комментариев и отступов), чтобы поместиться в 32 КБ
# бесплатной версии Keil. Исходники в web/ остаются читаемыми.
# (имя 8.3, содержимое)
FILES = [
    ("INDEX   HTM", minify_html(read("index.html")).encode("utf-8")),
    ("APP     JS ", minify_js(read("app.js")).encode("utf-8")),
    ("README  TXT", README.encode("utf-8")),
]


def build():
    clusters_per_file = [max(1, -(-len(data) // SECTOR)) for _, data in FILES]
    data_clusters = sum(clusters_per_file)
    total_clusters = data_clusters + 2                       # +2 служебных элемента FAT
    if total_clusters >= 4085:
        sys.exit("слишком большой образ для FAT12")
    fat_sectors = -(-(total_clusters * 3 // 2 + 1) // SECTOR)
    root_sectors = ROOT_ENTRIES * 32 // SECTOR
    first_data = 1 + fat_sectors + root_sectors
    total = max(first_data + data_clusters, 16)              # меньше 8 КБ Windows не монтирует

    img = bytearray(total * SECTOR)

    # загрузочный сектор
    boot = bytearray([0xEB, 0x3C, 0x90]) + b"MSDOS5.0"
    boot += (SECTOR).to_bytes(2, "little") + bytes([1])      # байт в секторе, секторов в кластере
    boot += (1).to_bytes(2, "little") + bytes([1])           # зарезервировано, копий FAT
    boot += ROOT_ENTRIES.to_bytes(2, "little") + total.to_bytes(2, "little")
    boot += bytes([0xF8]) + fat_sectors.to_bytes(2, "little")
    boot += (1).to_bytes(2, "little") + (1).to_bytes(2, "little") + bytes(4) + bytes(4)
    boot += bytes([0x80, 0x00, 0x29]) + bytes([0x34, 0x12, 0x00, 0x00]) + LABEL + b"FAT12   "
    assert len(boot) == 62
    img[0:62] = boot
    img[510:512] = b"\x55\xAA"

    # FAT12: цепочки кластеров
    fat = [0xFF8, 0xFFF] + [0] * data_clusters
    root = bytearray(ROOT_ENTRIES * 32)
    root[0:11] = LABEL
    root[11] = 0x08                                          # запись-метка тома
    cluster = 2
    for i, ((name, data), n) in enumerate(zip(FILES, clusters_per_file)):
        for k in range(n):
            fat[cluster + k] = cluster + k + 1 if k < n - 1 else 0xFFF
        off = (first_data + cluster - 2) * SECTOR
        img[off:off + len(data)] = data
        e = root[(i + 1) * 32:(i + 2) * 32]
        e[0:11] = name.encode("ascii")
        e[11] = 0x21                                         # только чтение + архивный
        e[12] = 0x18                                         # имя и расширение строчными
        e[24:26] = (0x5A21).to_bytes(2, "little")            # дата 01.01.2025
        e[22:24] = (0).to_bytes(2, "little")
        e[26:28] = cluster.to_bytes(2, "little")
        e[28:32] = len(data).to_bytes(4, "little")
        root[(i + 1) * 32:(i + 2) * 32] = e
        cluster += n

    fat_bytes = bytearray(fat_sectors * SECTOR)
    for i, v in enumerate(fat):
        p = i * 3 // 2
        if i % 2 == 0:
            fat_bytes[p] = v & 0xFF
            fat_bytes[p + 1] = (fat_bytes[p + 1] & 0xF0) | (v >> 8)
        else:
            fat_bytes[p] = (fat_bytes[p] & 0x0F) | ((v & 0x0F) << 4)
            fat_bytes[p + 1] = v >> 4
    img[SECTOR:SECTOR + len(fat_bytes)] = fat_bytes
    r = (1 + fat_sectors) * SECTOR
    img[r:r + len(root)] = root
    return bytes(img), total


def main():
    img, total = build()
    out = os.path.join(ROOT, "src", "disk_image.c")
    lines = []
    for i in range(0, len(img), 16):
        lines.append("  " + ", ".join("0x%02X" % b for b in img[i:i + 16]) + ",")
    with open(out, "w", newline="\n") as f:
        f.write("// Файл создан tools/make_disk.py из каталога web/. Не правьте вручную.\n")
        f.write("// Образ диска FAT12 (%d секторов по 512 байт, %d байт) лежит во флеш-памяти.\n\n" % (total, len(img)))
        f.write("#include <stdint.h>\n\n")
        f.write("const uint32_t disk_image_blocks = %d;\n\n" % total)
        f.write("const uint8_t disk_image[%d] = {\n" % len(img))
        f.write("\n".join(lines) + "\n};\n")
    if len(sys.argv) > 1:                                    # python make_disk.py file.img - сохранить и образ
        open(sys.argv[1], "wb").write(img)
    for name, data in FILES:
        print("  %s %d байт" % (name.strip(), len(data)))
    print("src/disk_image.c: %d секторов, %d байт" % (total, len(img)))


if __name__ == "__main__":
    main()
