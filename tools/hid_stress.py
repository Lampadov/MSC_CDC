#!/usr/bin/env python3
"""
Стресс-тест HID-эхо (прошивка собрана с HID_MODE=HID_MODE_ECHO, PID 0x4044).

Хост шлёт 64-байтные отчёты (EP1 OUT), устройство возвращает их обратно (EP2 IN).
Проверяется: целостность данных, порядок, потери, дубликаты, задержка.

  pip install hidapi

Примеры:
  python hid_stress.py                          # пинг-понг 30 секунд
  python hid_stress.py --mode window --window 4 --duration 120
  python hid_stress.py --mode pingpong --count 20000 --seed 7
  python hid_stress.py --list                   # показать найденные HID-устройства

Режимы:
  pingpong  отправил один отчёт - дождался эха - следующий (проверяет каждую транзакцию)
  window    держит в полёте до --window отчётов (нагружает очередь устройства; максимум 6,
            в прошивке очередь на 7 отчётов, больше - устройство честно отбрасывает)

Код возврата: 0 - ошибок нет, 1 - были ошибки, 2 - устройство не найдено.
"""
import argparse
import random
import struct
import sys
import time
import zlib

try:
    import hid
except ImportError:
    sys.exit("Нужен модуль hidapi:  pip install hidapi")

REPORT_SIZE = 64
DEFAULT_VID = 0xABAB
DEFAULT_PID = 0x4044          # HID_MODE_ECHO: 0x4004 | ((3 - 1) << 5)
MAX_WINDOW = 6


def make_report(seed, seq):
    """seq(4) | crc32 хвоста(4) | 56 псевдослучайных байт. Содержимое восстанавливается по seq."""
    rnd = random.Random((seed << 32) ^ seq)
    tail = bytes(rnd.getrandbits(8) for _ in range(REPORT_SIZE - 8))
    return struct.pack("<II", seq, zlib.crc32(tail)) + tail


def check_report(seed, data):
    """Возвращает (seq, ok). ok=False, если данные повреждены."""
    data = bytes(data)                 # hidapi.read() возвращает список int
    if len(data) != REPORT_SIZE:
        return None, False
    seq = struct.unpack_from("<I", data)[0]
    return seq, data == make_report(seed, seq)


class Stats:
    def __init__(self):
        self.sent = self.received = 0
        self.lost = self.dups = self.reorder = self.corrupt = self.timeouts = 0
        self.lat = []

    def report(self, elapsed):
        print()
        print(f"Время:             {elapsed:.1f} с")
        print(f"Отправлено:        {self.sent}")
        print(f"Получено:          {self.received}")
        print(f"Потеряно:          {self.lost}")
        print(f"Дубликаты:         {self.dups}")
        print(f"Нарушен порядок:   {self.reorder}")
        print(f"Повреждено:        {self.corrupt}")
        print(f"Таймауты:          {self.timeouts}")
        if elapsed > 0 and self.received:
            print(f"Скорость:          {self.received / elapsed:.0f} отчётов/с "
                  f"({self.received * REPORT_SIZE / elapsed / 1024:.1f} КиБ/с в каждую сторону)")
        if self.lat:
            lat = sorted(self.lat)
            pct = lambda p: lat[min(len(lat) - 1, int(len(lat) * p))] * 1000
            print(f"Задержка, мс:      мин {lat[0]*1000:.2f}  медиана {pct(0.5):.2f}  "
                  f"p99 {pct(0.99):.2f}  макс {lat[-1]*1000:.2f}")

    @property
    def errors(self):
        return self.lost + self.dups + self.reorder + self.corrupt + self.timeouts


def open_device(args):
    infos = hid.enumerate(args.vid, args.pid)
    if not infos:
        return None
    dev = hid.device()
    dev.open_path(infos[0]["path"])
    return dev


def send(dev, data):
    # hidapi: первый байт - Report ID (у нас 0, так как нумерованных отчётов нет)
    n = dev.write(b"\x00" + data)
    if n < 0:
        raise OSError("hid write failed: " + str(dev.error()))


def run(dev, args, st):
    seed = args.seed
    window = 1 if args.mode == "pingpong" else min(args.window, MAX_WINDOW)
    timeout_ms = int(args.timeout * 1000)

    next_send = 0          # следующий seq к отправке
    expect = 0             # какой seq ждём в эхе
    sent_at = {}
    start = last_print = time.monotonic()
    deadline = start + args.duration if args.count is None else None

    def finished():
        if args.count is not None:
            return expect >= args.count
        return time.monotonic() >= deadline

    while not finished():
        # заполнить окно
        while next_send - expect < window and (args.count is None or next_send < args.count):
            send(dev, make_report(seed, next_send))
            sent_at[next_send] = time.monotonic()
            next_send += 1
            st.sent += 1

        data = dev.read(REPORT_SIZE, timeout_ms)
        now = time.monotonic()
        if not data:
            st.timeouts += 1
            print(f"ТАЙМАУТ: эхо на отчёт #{expect} не пришло за {args.timeout} с")
            # считаем отчёт потерянным и идём дальше
            st.lost += 1
            sent_at.pop(expect, None)
            expect += 1
            if st.timeouts >= args.max_errors:
                print("Слишком много таймаутов, остановка.")
                return
            continue

        seq, ok = check_report(seed, data)
        st.received += 1
        if not ok:
            st.corrupt += 1
            print(f"ПОВРЕЖДЁН отчёт (ждали #{expect}, seq в данных {seq}): {bytes(data[:12]).hex()}...")
            sent_at.pop(expect, None)
            expect += 1
        elif seq == expect:
            st.lat.append(now - sent_at.pop(seq))
            expect += 1
        elif seq < expect:
            st.dups += 1
            print(f"ДУБЛИКАТ: пришёл #{seq}, уже приняты все до #{expect - 1}")
        elif seq < next_send:
            lost = seq - expect
            st.lost += lost
            st.reorder += 0 if lost else 1
            print(f"ПОТЕРЯНО {lost}: ждали #{expect}, пришёл #{seq}")
            for s in range(expect, seq):
                sent_at.pop(s, None)
            st.lat.append(now - sent_at.pop(seq))
            expect = seq + 1
        else:
            st.corrupt += 1
            print(f"Неизвестный seq #{seq} (отправлено только до #{next_send - 1})")
            expect += 1

        if st.errors >= args.max_errors:
            print("Слишком много ошибок, остановка.")
            return

        if now - last_print >= 1:
            last_print = now
            print(f"\r{now - start:6.1f} с  отправлено {st.sent}  принято {st.received}  "
                  f"в полёте {next_send - expect}  ошибок {st.errors}", end="", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--vid", type=lambda x: int(x, 0), default=DEFAULT_VID)
    ap.add_argument("--pid", type=lambda x: int(x, 0), default=DEFAULT_PID)
    ap.add_argument("--mode", choices=["pingpong", "window"], default="pingpong")
    ap.add_argument("--window", type=int, default=4, help="отчётов в полёте (режим window, до 6)")
    ap.add_argument("--duration", type=float, default=30, help="длительность, с")
    ap.add_argument("--count", type=int, default=None, help="число отчётов вместо --duration")
    ap.add_argument("--timeout", type=float, default=1.0, help="ожидание эха, с")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--max-errors", type=int, default=20)
    ap.add_argument("--list", action="store_true", help="показать HID-устройства и выйти")
    args = ap.parse_args()

    if args.list:
        for d in hid.enumerate():
            print(f"{d['vendor_id']:04X}:{d['product_id']:04X}  {d.get('manufacturer_string')} / "
                  f"{d.get('product_string')}  {d['path']}")
        return 0

    dev = open_device(args)
    if dev is None:
        print(f"Устройство {args.vid:04X}:{args.pid:04X} не найдено. "
              f"Прошивка должна быть собрана с HID_MODE=3 (ECHO). См. --list.")
        return 2

    st = Stats()
    t0 = time.monotonic()
    try:
        run(dev, args, st)
    except KeyboardInterrupt:
        print("\nПрервано.")
    except OSError as e:
        print(f"\nОшибка ввода-вывода (устройство отключилось?): {e}")
        st.timeouts += 1
    finally:
        dev.close()
    st.report(time.monotonic() - t0)
    print("РЕЗУЛЬТАТ:", "OK" if st.errors == 0 else "ЕСТЬ ОШИБКИ")
    return 0 if st.errors == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
