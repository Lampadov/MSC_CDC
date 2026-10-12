#!/usr/bin/env python3
r"""
Проверка HID-примеров (Windows). У HID-устройства нет канала от компьютера к плате, поэтому нагрузку
создаёт сама плата, а скрипт принимает то, что она "набирает" или "рисует", и проверяет результат.
Нужны только стандартная библиотека Python 3.

  keyboard  Плата набирает текст как клавиатура. Скрипт принимает символы и сравнивает с ожидаемым:
            любая потеря, дублирование или перестановка символа это ошибка.
              hid-only (HID_MODE_KEYBOARD):  python hid_check.py keyboard --runs 20
                 на плате каждый раз зажмите UP+DOWN (плата набирает "Hello from Milandr!")
              msc-hid:  python hid_check.py keyboard --expect-file E:\TEXT.TXT --runs 20
                 на плате каждый раз нажмите SELECT (плата набирает содержимое TEXT.TXT)
            Окно консоли с запущенным скриптом должно быть активным, раскладка английская.

  mouse     Плата двигает курсор (hid-only в режиме HID_MODE_MOUSE). Каждый раз зажмите UP+DOWN: курсор
            обходит окружность радиусом около 60 пикселей и возвращается в начало. Скрипт отслеживает
            положение курсора и проверяет, что он прошёл не менее 100 пикселей и вернулся в исходную точку
            (допуск 15 пикселей; если у вас включено "повышенная точность указателя", отключите её).

Каждый прогон - одно срабатывание кнопки. Код выхода 1, если были ошибки.
"""
import argparse
import ctypes
import math
import msvcrt
import sys
import time
from ctypes import wintypes


def read_typed(first_timeout, idle):
    """Ждёт первый символ, затем читает, пока не наступит пауза idle секунд."""
    chars, t_first, last = [], None, None
    t0 = time.perf_counter()
    while True:
        now = time.perf_counter()
        if msvcrt.kbhit():
            ch = msvcrt.getwch()
            if ch in ("\x00", "\xe0"):                      # служебный код: стрелки и т. п.
                ch = "<" + str(ord(msvcrt.getwch())) + ">"
            chars.append(ch)
            t_first = t_first or now
            last = now
        elif last is None and now - t0 > first_timeout:
            return None, 0.0
        elif last is not None and now - last > idle:
            return "".join(chars), last - t_first
        else:
            time.sleep(0.002)


def keyboard(args):
    if args.expect_file:
        with open(args.expect_file, "rb") as f:
            expected = f.read().decode("ascii", "replace")
    else:
        expected = args.expect
    expected = expected.replace("\r\n", "\n").replace("\n", "\r")      # Enter приходит как "\r"
    print("Ожидаемый текст: %r" % expected)
    ok = bad = 0
    for run in range(1, args.runs + 1):
        print("Прогон %d из %d: запустите набор на плате..." % (run, args.runs), flush=True)
        got, dt = read_typed(args.timeout, args.idle)
        if got is None:
            print("  ОШИБКА: за %d с ничего не набрано" % args.timeout)
            bad += 1
            continue
        if got == expected:
            ok += 1
            print("  ok, %d символов за %.2f с" % (len(got), dt))
        else:
            bad += 1
            i = next((k for k in range(min(len(got), len(expected))) if got[k] != expected[k]), min(len(got), len(expected)))
            print("  ОШИБКА: получено %r, расхождение с позиции %d" % (got, i))
        while msvcrt.kbhit():                               # съедаем хвост
            msvcrt.getwch()
    return ok, bad


def mouse(args):
    user32 = ctypes.windll.user32
    pt = wintypes.POINT()

    def pos():
        user32.GetCursorPos(ctypes.byref(pt))
        return pt.x, pt.y

    ok = bad = 0
    for run in range(1, args.runs + 1):
        print("Прогон %d из %d: зажмите UP+DOWN на плате..." % (run, args.runs), flush=True)
        start, t0 = pos(), time.perf_counter()
        while pos() == start and time.perf_counter() - t0 < args.timeout:
            time.sleep(0.005)
        if pos() == start:
            print("  ОШИБКА: курсор не двинулся за %d с" % args.timeout)
            bad += 1
            continue
        start, far, last_move = pos(), 0.0, time.perf_counter()
        prev = start
        while time.perf_counter() - last_move < args.idle:
            p = pos()
            far = max(far, math.hypot(p[0] - start[0], p[1] - start[1]))
            if p != prev:
                last_move, prev = time.perf_counter(), p
            time.sleep(0.005)
        end = pos()
        back = math.hypot(end[0] - start[0], end[1] - start[1])
        if far >= 100 and back <= 15:
            ok += 1
            print("  ok, максимальное удаление %d пикс., вернулся на %d пикс. от старта" % (far, back))
        else:
            bad += 1
            print("  ОШИБКА: удаление %d пикс. (нужно от 100), возврат %d пикс. (нужно до 15)" % (far, back))
    return ok, bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["keyboard", "mouse"])
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--expect", default="Hello from Milandr!", help="ожидаемый текст (keyboard)")
    ap.add_argument("--expect-file", help="взять ожидаемый текст из файла, например E:\\TEXT.TXT (keyboard)")
    ap.add_argument("--timeout", type=float, default=30, help="сколько ждать начала набора, с")
    ap.add_argument("--idle", type=float, default=1.0, help="пауза, по которой набор считается законченным, с")
    args = ap.parse_args()
    ok, bad = (keyboard if args.mode == "keyboard" else mouse)(args)
    print("\nПрогонов %d, успешно %d, ошибок %d" % (ok + bad, ok, bad))
    print("РЕЗУЛЬТАТ:", "OK" if bad == 0 else "ЕСТЬ ОШИБКИ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
