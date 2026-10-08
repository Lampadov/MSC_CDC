#!/usr/bin/env python3
r"""
Стресс-тест USB-диска (MSC) на MDR32F9Q2I. Только стандартная библиотека Python 3.

Два режима:

  raw    Чтение диска "в сыром виде" случайными кусками (1..16 секторов) и сравнение с эталоном.
         Это настоящая нагрузка на USB (READ10 разной длины), кэш ОС не мешает.
         Диск при этом не изменяется. Нужны права администратора / root.
           Windows (cmd от администратора):  python msc_stress.py raw \\.\E: --minutes 10
           Linux:                            sudo python3 msc_stress.py raw /dev/sdX --minutes 10

  files  Создание, перезапись и удаление файлов на смонтированном диске с проверкой содержимого
         (WRITE10 + READ10 через файловую систему). Права администратора не нужны.
           python msc_stress.py files E:\ --minutes 10

Не запускайте режимы одновременно. Ошибки печатаются сразу, итог - в конце, код выхода 1 при ошибках.
"""
import argparse, os, random, sys, time, errno

SECTOR = 512


def now():
    return time.perf_counter()


class Stats:
    def __init__(self):
        self.ops = 0
        self.errors = 0
        self.worst = 0.0       # самая долгая операция, секунд (зависания USB видны здесь)
        self.start = now()

    def op(self, dt):
        self.ops += 1
        self.worst = max(self.worst, dt)

    def error(self, msg):
        self.errors += 1
        print(f"[ОШИБКА #{self.errors}] {msg}", flush=True)

    def report(self):
        t = now() - self.start
        print(f"\nИтог: {self.ops} операций за {t:.0f} с ({self.ops / max(t, 1e-9):.1f} оп/с), "
              f"самая долгая операция {self.worst * 1000:.0f} мс, ошибок: {self.errors}")
        print("РЕЗУЛЬТАТ:", "OK" if self.errors == 0 else "ЕСТЬ ОШИБКИ")
        return 0 if self.errors == 0 else 1


def tick(st, last, label):
    if now() - last >= 10:
        print(f"  ... {label}: {st.ops} операций, ошибок {st.errors}", flush=True)
        return now()
    return last


def raw_test(dev, minutes, size, seed):
    rnd = random.Random(seed)
    st = Stats()
    f = open(dev, 'rb', buffering=0)
    ref = f.read(size)
    if len(ref) != size:
        print(f"Не удалось прочитать {size} байт с {dev} (получено {len(ref)})")
        return 1
    print(f"Эталон прочитан ({size} байт), жму {minutes} мин...")
    end, last = now() + minutes * 60, now()
    nsec = size // SECTOR
    while now() < end:
        lba = rnd.randrange(nsec)
        cnt = rnd.randint(1, nsec - lba)
        t0 = now()
        try:
            f.seek(lba * SECTOR)
            data = f.read(cnt * SECTOR)
        except OSError as e:
            st.error(f"чтение lba={lba} count={cnt}: {e}")
            try:
                f.close()
                time.sleep(1)
                f = open(dev, 'rb', buffering=0)
            except OSError as e2:
                st.error(f"диск пропал: {e2}")
                break
            continue
        st.op(now() - t0)
        if data != ref[lba * SECTOR:(lba + cnt) * SECTOR]:
            st.error(f"данные не совпали: lba={lba} count={cnt}")
        last = tick(st, last, "raw")
    f.close()
    return st.report()


def files_test(path, minutes, seed, max_files, max_size):
    rnd = random.Random(seed)
    st = Stats()
    model = {}                         # имя -> ожидаемое содержимое
    end, last = now() + minutes * 60, now()
    print(f"Тест файлов на {path}, {minutes} мин...")

    def full_path(n):
        return os.path.join(path, n)

    def write(name):
        data = bytes(rnd.getrandbits(8) for _ in range(rnd.randint(1, max_size)))
        t0 = now()
        try:
            with open(full_path(name), 'wb') as f:
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
        except OSError as e:
            if e.errno == errno.ENOSPC:        # диск 6 КБ - переполнение это нормально
                model.pop(name, None)
                try:
                    os.remove(full_path(name))
                except OSError:
                    pass
                return
            st.error(f"запись {name} ({len(data)} Б): {e}")
            return
        st.op(now() - t0)
        model[name] = data

    def check(name):
        t0 = now()
        try:
            with open(full_path(name), 'rb') as f:
                got = f.read()
        except OSError as e:
            st.error(f"чтение {name}: {e}")
            return
        st.op(now() - t0)
        if got != model[name]:
            st.error(f"{name}: содержимое не совпало (ждали {len(model[name])} Б, прочитали {len(got)} Б)")

    names = [f"S{i:03d}.BIN" for i in range(max_files)]
    while now() < end:
        r = rnd.random()
        name = rnd.choice(names)
        if r < 0.45:
            write(name)
        elif r < 0.65 and model:
            n = rnd.choice(list(model))
            t0 = now()
            try:
                os.remove(full_path(n))
                st.op(now() - t0)
                del model[n]
            except OSError as e:
                st.error(f"удаление {n}: {e}")
        elif model:
            check(rnd.choice(list(model)))
        if st.ops % 25 == 0:
            for n in list(model):
                check(n)
        last = tick(st, last, "files")

    for n in list(model):                       # финальная проверка и уборка
        check(n)
        try:
            os.remove(full_path(n))
        except OSError as e:
            st.error(f"уборка {n}: {e}")
    return st.report()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('mode', choices=['raw', 'files'])
    ap.add_argument('target', help=r'raw: \\.\E: или /dev/sdX;  files: E:\ или /media/...')
    ap.add_argument('--minutes', type=float, default=5)
    ap.add_argument('--size', type=int, default=16 * SECTOR, help='размер диска в байтах (raw), по умолчанию 8192')
    ap.add_argument('--seed', type=int, default=None, help='зерно случайных чисел (для повторения прогона)')
    ap.add_argument('--max-files', type=int, default=8)
    ap.add_argument('--max-size', type=int, default=1500, help='максимальный размер файла, байт')
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f"seed={seed}")
    if a.mode == 'raw':
        return raw_test(a.target, a.minutes, a.size, seed)
    return files_test(a.target, a.minutes, seed, a.max_files, a.max_size)


if __name__ == '__main__':
    sys.exit(main())
