#!/usr/bin/env python3
r"""
Нагрузочный тест CDC (виртуальный COM-порт) на MDR32F9Q2I.
Прошивка должна работать эхом: всё принятое отправляется обратно (так сделано в main.c).

Нужны Python 3.9+ и pyserial:   pip install pyserial

Режимы (можно указать несколько через запятую или all):

  stream    Непрерывный поток псевдослучайных данных в обе стороны одновременно, блоками
            случайной длины (1..4096 Б, часто кратными 64 Б - это проверяет короткие и
            нулевые пакеты). Принятое сравнивается с эталоном побайтно. Показывает скорость.
  pingpong  Запрос-ответ: пакет случайной длины (в т.ч. 1, 63, 64, 65, 128, 129 Б), ждём эхо,
            проверяем содержимое и задержку.
  reopen    Многократное закрытие и открытие порта (как переподключение программы) с проверкой эха.

Примеры:
  Windows:  python cdc_stress.py COM5 all --minutes 15
  Linux:    python3 cdc_stress.py /dev/ttyACM0 stream --minutes 30

При ошибке выводятся смещение и данные, итог в конце, код выхода 1 при ошибках.
Для повторения прогона используйте тот же --seed (печатается в начале).
"""
import argparse, random, sys, threading, time

try:
    import serial
except ImportError:
    sys.exit("Нужен pyserial: pip install pyserial")

STALL_SEC = 3.0          # столько ждём эхо, прежде чем объявить зависание


class Result:
    def __init__(self, name):
        self.name, self.errors, self.bytes, self.t0 = name, 0, 0, time.perf_counter()
        self.extra = ""

    def error(self, msg):
        self.errors += 1
        print(f"[{self.name}] ОШИБКА #{self.errors}: {msg}", flush=True)

    def report(self):
        t = time.perf_counter() - self.t0
        print(f"[{self.name}] {'OK' if not self.errors else 'ЕСТЬ ОШИБКИ'}: {self.bytes} Б за {t:.0f} с "
              f"({self.bytes / max(t, 1e-9) / 1024:.1f} КБ/с в одну сторону), ошибок: {self.errors}. {self.extra}",
              flush=True)
        return self.errors == 0


# Эталонный поток: байт номер i зависит только от i (не от разбиения на блоки записи/чтения)
_L = 65521                                  # простое число: границы блоков не совпадают с 64 Б
_base = random.Random(12345).randbytes(_L)
_xor = {}


def stream(off, n):
    out = bytearray()
    while n:
        p, k = off % _L, (off // _L) & 0xFF
        take = min(n, _L - p)
        part = _base[p:p + take]
        if k:
            if k not in _xor:
                _xor[k] = bytes(b ^ k for b in range(256))
            part = part.translate(_xor[k])
        out += part
        off += take
        n -= take
    return bytes(out)


def hexdump(b, n=16):
    return " ".join(f"{x:02X}" for x in b[:n])


def open_port(port, **kw):
    return serial.Serial(port, 115200, timeout=0.05, write_timeout=2, **kw)


def read_exact(ser, n, timeout):
    """Прочитать ровно n байт за timeout секунд; вернуть, что успели."""
    buf, end = bytearray(), time.perf_counter() + timeout
    while len(buf) < n and time.perf_counter() < end:
        buf += ser.read(n - len(buf))
    return bytes(buf)


def drain(ser, quiet=0.3):
    """Вычитать всё, что осталось в порту (пока не станет тихо)."""
    last = time.perf_counter()
    while time.perf_counter() - last < quiet:
        if ser.read(4096):
            last = time.perf_counter()


# ---------------------------------------------------------------- stream
def test_stream(port, seconds, seed):
    res = Result("stream")
    ser = open_port(port)
    drain(ser)
    sent = recvd = 0
    stop = threading.Event()
    last_rx = [time.perf_counter()]
    lock = threading.Lock()

    def reader():
        nonlocal recvd
        while not stop.is_set():
            try:
                data = ser.read(4096)
            except Exception as e:
                res.error(f"чтение: {e}")
                stop.set()
                return
            if not data:
                continue
            exp = stream(recvd, len(data))
            if data != exp:
                i = next(k for k in range(len(data)) if data[k] != exp[k])
                res.error(f"данные не совпали на смещении {recvd + i}: "
                          f"получено {hexdump(data[i:])} ждали {hexdump(exp[i:])}")
                stop.set()
                return
            with lock:
                recvd += len(data)
                last_rx[0] = time.perf_counter()

    th = threading.Thread(target=reader, daemon=True)
    th.start()
    sizes = [1, 2, 63, 64, 65, 128, 192, 256, 1000, 4096]
    rnd = random.Random(seed ^ 0x5A5A)
    end, tick = time.perf_counter() + seconds, time.perf_counter()
    try:
        while time.perf_counter() < end and not stop.is_set():
            n = rnd.choice(sizes) if rnd.random() < 0.6 else rnd.randint(1, 4096)
            while sent - recvd > 2048 and not stop.is_set():      # окно: не уходим далеко вперёд
                time.sleep(0.001)
                if time.perf_counter() - last_rx[0] > STALL_SEC:
                    res.error(f"эхо не приходит {STALL_SEC:.0f} с (отправлено {sent}, получено {recvd}) - зависание")
                    stop.set()
            if stop.is_set():
                break
            ser.write(stream(sent, n))
            sent += n
            if time.perf_counter() - tick > 10:
                tick = time.perf_counter()
                print(f"  ... stream: {sent} Б отправлено, {recvd} Б принято и сверено, в пути {sent - recvd} Б (норма до ~6000)", flush=True)
        # дождаться хвоста
        t_end = time.perf_counter() + STALL_SEC
        while recvd < sent and not stop.is_set() and time.perf_counter() < t_end:
            time.sleep(0.01)
        if recvd < sent and not stop.is_set():
            res.error(f"потеряно {sent - recvd} Б из {sent}")
    except Exception as e:
        res.error(f"запись: {e}")
    stop.set()
    th.join(1)
    ser.close()
    res.bytes = recvd
    res.extra = f"отправлено {sent} Б, принято и сверено {recvd} Б"
    return res.report()


# ---------------------------------------------------------------- pingpong
def test_pingpong(port, seconds, seed):
    res = Result("pingpong")
    rnd = random.Random(seed)
    ser = open_port(port)
    drain(ser)
    lat, special = [], [1, 2, 62, 63, 64, 65, 66, 127, 128, 129, 192, 256, 300]
    end, tick = time.perf_counter() + seconds, time.perf_counter()
    while time.perf_counter() < end:
        n = rnd.choice(special) if rnd.random() < 0.5 else rnd.randint(1, 300)
        data = rnd.randbytes(n)
        t0 = time.perf_counter()
        try:
            ser.write(data)
            got = read_exact(ser, n, STALL_SEC)
        except Exception as e:
            res.error(f"ввод-вывод: {e}")
            break
        dt = time.perf_counter() - t0
        if got != data:
            res.error(f"пакет {n} Б: получено {len(got)} Б, "
                      f"{'данные отличаются' if len(got) == n else 'неполный ответ'} "
                      f"(получено {hexdump(got)} ждали {hexdump(data)})")
            drain(ser)
            continue
        extra = ser.read(64)                      # лишних байт быть не должно
        if extra:
            res.error(f"после пакета {n} Б пришли лишние байты: {hexdump(extra)}")
            drain(ser)
        lat.append(dt)
        res.bytes += n
        if time.perf_counter() - tick > 10:
            tick = time.perf_counter()
            print(f"  ... pingpong: {len(lat)} обменов, ошибок {res.errors}", flush=True)
    ser.close()
    if lat:
        lat.sort()
        res.extra = (f"{len(lat)} обменов, задержка мин/медиана/макс = "
                     f"{lat[0] * 1000:.1f}/{lat[len(lat) // 2] * 1000:.1f}/{lat[-1] * 1000:.1f} мс")
    return res.report()


# ---------------------------------------------------------------- reopen
def test_reopen(port, seconds, seed):
    res = Result("reopen")
    rnd = random.Random(seed)
    end, cycles = time.perf_counter() + seconds, 0
    while time.perf_counter() < end:
        try:
            ser = open_port(port)
            try:
                ser.dtr = True
            except OSError:
                pass
            drain(ser, 0.1)
            for _ in range(3):
                n = rnd.randint(1, 100)
                data = rnd.randbytes(n)
                ser.write(data)
                got = read_exact(ser, n, STALL_SEC)
                if got != data:
                    res.error(f"цикл {cycles}: эхо {len(got)} из {n} Б")
                    break
                res.bytes += n
            ser.close()
        except Exception as e:
            res.error(f"цикл {cycles}: {e}")
            time.sleep(1)
        cycles += 1
        time.sleep(0.2)
    res.extra = f"{cycles} циклов открытия"
    return res.report()


TESTS = {"stream": test_stream, "pingpong": test_pingpong, "reopen": test_reopen}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", help="COM5 или /dev/ttyACM0")
    ap.add_argument("modes", help="stream,pingpong,reopen или all")
    ap.add_argument("--minutes", type=float, default=5, help="длительность КАЖДОГО режима")
    ap.add_argument("--seed", type=int, default=None)
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    modes = list(TESTS) if a.modes == "all" else a.modes.split(",")
    print(f"seed={seed}")
    ok = True
    for m in modes:
        if m not in TESTS:
            sys.exit(f"неизвестный режим {m}")
        print(f"--- {m}, {a.minutes} мин ---", flush=True)
        ok &= TESTS[m](a.port, a.minutes * 60, seed)
    print("\nРЕЗУЛЬТАТ:", "OK" if ok else "ЕСТЬ ОШИБКИ")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
