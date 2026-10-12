#!/usr/bin/env python3
r"""
Нагрузочный тест WebUSB-интерфейса примеров webusb и msc-webusb (Windows или Linux).

Одновременно:
  - читает кадры состояния (msc-webusb: EP3 IN, webusb: EP2 IN) и проверяет каждый: длина 9 байт, заголовок 0x01, время платы
    растёт, АЦП не больше 4095, пауза между кадрами не больше 300 мс (иначе зависание);
  - в случайные моменты шлёт команду светодиодов со случайной маской (msc-webusb: vendor-запрос 0x10,
    webusb: bulk-пакет 01 <маска> в EP1 OUT) и проверяет, что маска появилась в кадрах в течение 300 мс;
  - читает дескрипторы: устройства, строку Microsoft OS 0xEE, Compatible ID и свойства MS OS 1.0;
  - шлёт неизвестный vendor-запрос и проверяет, что плата отвечает STALL и продолжает работать.

Для msc-webusb параллельно можно запустить msc_stress.py raw на диске платы: так проверяется и составное
устройство под нагрузкой. Закройте страницу WebUSB в браузере: устройство занимает только одна программа.

Нужно:  pip install pyusb libusb-package
Драйвер WinUSB Windows ставит сама (MS OS 1.0). Примеры:
  python webusb_stress.py msc-webusb --minutes 10
  python webusb_stress.py webusb --minutes 10
  python webusb_stress.py msc-webusb --pid 0x4016 --minutes 30 --seed 5
Код выхода 1, если были ошибки.
"""
import argparse
import random
import sys
import threading
import time

try:
    import usb.core
    import usb.util
except ImportError:
    sys.exit("Нужны pyusb и libusb-package: pip install pyusb libusb-package")

REQ_LEDS, MS_VENDOR = 0x10, 0x03
# профили: интерфейс, EP IN, EP OUT для команд (None - команды vendor-запросом), PID
PROFILES = {
    "msc-webusb": dict(itf=1, ep_in=0x83, ep_out=None, pid=0x4016),
    "webusb":     dict(itf=0, ep_in=0x82, ep_out=0x01, pid=0x4015),
}
ITF, EP_IN, EP_OUT = 1, 0x83, None
FRAME_GAP = 0.3


class Stats:
    def __init__(self):
        self.lock = threading.Lock()
        self.errors, self.frames, self.ctrl = 0, 0, 0
        self.worst_gap = 0.0
        self.leds_seen = 0                 # последняя маска светодиодов из кадра
        self.t_frame = time.perf_counter()

    def error(self, msg):
        with self.lock:
            self.errors += 1
            print("[ОШИБКА #%d] %s" % (self.errors, msg), flush=True)


def reader(dev, st, stop):
    last_ms, last_t = None, time.perf_counter()
    while not stop.is_set():
        try:
            data = bytes(dev.read(EP_IN, 64, timeout=500))
        except usb.core.USBError as e:
            if stop.is_set():
                return
            if getattr(e, "errno", None) in (110, 10060, None) and "timed out" in str(e).lower():
                st.error("кадров нет более 500 мс (зависание IN-точки)")
                continue
            st.error("чтение IN-точки: %s" % e)
            time.sleep(0.2)
            continue
        now = time.perf_counter()
        st.worst_gap = max(st.worst_gap, now - last_t)
        if now - last_t > FRAME_GAP:
            st.error("пауза между кадрами %.0f мс" % ((now - last_t) * 1000))
        last_t = now
        st.t_frame = now
        if len(data) != 9 or data[0] != 0x01:
            st.error("неверный кадр: %s" % data.hex())
            continue
        ms = int.from_bytes(data[5:9], "little")
        adc = int.from_bytes(data[3:5], "little")
        if adc > 4095:
            st.error("АЦП вне диапазона: %d" % adc)
        if last_ms is not None and not (0 < ((ms - last_ms) & 0xFFFFFFFF) < 2000):
            st.error("время платы скачет: было %d, стало %d" % (last_ms, ms))
        last_ms = ms
        st.leds_seen = data[2]
        st.frames += 1


def control_loop(dev, st, stop, rnd, vid, pid):
    def fail(what, e):
        st.error("%s: %s" % (what, e))

    while not stop.is_set():
        r = rnd.random()
        try:
            if r < 0.55:                                    # светодиоды: команда и проверка по кадрам
                mask = rnd.randrange(4)
                if EP_OUT is None:
                    dev.ctrl_transfer(0x41, REQ_LEDS, mask, ITF, None, timeout=1000)
                else:
                    dev.write(EP_OUT, bytes([0x01, mask]), timeout=1000)
                t0 = time.perf_counter()
                while st.leds_seen != mask and time.perf_counter() - t0 < FRAME_GAP:
                    time.sleep(0.002)
                if st.leds_seen != mask:
                    st.error("маска %d не появилась в кадрах за %d мс (в кадрах %d)" % (mask, FRAME_GAP * 1000, st.leds_seen))
            elif r < 0.65:
                d = bytes(dev.ctrl_transfer(0x80, 6, 0x0100, 0, 18, timeout=1000))
                if len(d) != 18 or int.from_bytes(d[8:10], "little") != vid or int.from_bytes(d[10:12], "little") != pid:
                    st.error("дескриптор устройства: %s" % d.hex())
            elif r < 0.75:
                d = bytes(dev.ctrl_transfer(0x80, 6, 0x03EE, 0x0409, 18, timeout=1000))
                if d[2:16].decode("utf-16-le", "replace") != "MSFT100" or d[16] != MS_VENDOR:
                    st.error("строка MS OS 0xEE: %s" % d.hex())
            elif r < 0.85:
                d = bytes(dev.ctrl_transfer(0xC0, MS_VENDOR, 0, 4, 40, timeout=1000))
                if len(d) != 40 or d[18:24] != b"WINUSB":
                    st.error("Compatible ID: %s" % d.hex())
            elif r < 0.92:
                d = bytes(dev.ctrl_transfer(0xC1, MS_VENDOR, ITF, 5, 142, timeout=1000))
                if len(d) != 142:
                    st.error("свойства MS OS: получено %d байт, ждали 142" % len(d))
            else:                                           # неизвестный запрос: должен быть STALL
                try:
                    dev.ctrl_transfer(0x41, 0x77, 0, ITF, None, timeout=1000)
                    st.error("неизвестный vendor-запрос принят, ждали STALL")
                except usb.core.USBError:
                    pass
            st.ctrl += 1
        except usb.core.USBError as e:
            fail("управляющая передача (r=%.2f)" % r, e)
            time.sleep(0.2)
        time.sleep(rnd.choice([0, 0, 0.001, 0.005, 0.02, 0.1]))


def main():
    global ITF, EP_IN, EP_OUT
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("example", nargs="?", choices=sorted(PROFILES), default="msc-webusb")
    ap.add_argument("--vid", type=lambda s: int(s, 0), default=0xCAFE)
    ap.add_argument("--pid", type=lambda s: int(s, 0), default=None, help="по умолчанию PID примера")
    ap.add_argument("--minutes", type=float, default=5)
    ap.add_argument("--seed", type=int)
    a = ap.parse_args()
    prof = PROFILES[a.example]
    ITF, EP_IN, EP_OUT = prof["itf"], prof["ep_in"], prof["ep_out"]
    if a.pid is None:
        a.pid = prof["pid"]
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print("seed=%d" % seed)

    backend = None
    try:
        import libusb_package
        backend = libusb_package.get_libusb1_backend()
    except ImportError:
        pass
    dev = usb.core.find(idVendor=a.vid, idProduct=a.pid, backend=backend)
    if dev is None:
        sys.exit("Устройство %04X:%04X не найдено (драйвер WinUSB подключён? страница в браузере закрыта?)" % (a.vid, a.pid))
    try:
        dev.set_configuration()
    except usb.core.USBError:
        pass                                                # конфигурация уже выбрана
    usb.util.claim_interface(dev, ITF)

    st, stop = Stats(), threading.Event()
    threads = [threading.Thread(target=reader, args=(dev, st, stop), daemon=True),
               threading.Thread(target=control_loop, args=(dev, st, stop, random.Random(seed), a.vid, a.pid), daemon=True)]
    t0 = time.perf_counter()
    for t in threads:
        t.start()
    try:
        last = t0
        while time.perf_counter() - t0 < a.minutes * 60:
            time.sleep(0.5)
            if time.perf_counter() - last >= 10:
                last = time.perf_counter()
                print("  ... кадров %d, управляющих запросов %d, ошибок %d" % (st.frames, st.ctrl, st.errors), flush=True)
    except KeyboardInterrupt:
        print("\nПрервано.")
    stop.set()
    for t in threads:
        t.join(2)
    usb.util.release_interface(dev, ITF)
    t = time.perf_counter() - t0
    print("\nИтог: %d кадров (%.1f в секунду), %d управляющих запросов, самая долгая пауза между кадрами %.0f мс, ошибок: %d"
          % (st.frames, st.frames / t, st.ctrl, st.worst_gap * 1000, st.errors))
    print("РЕЗУЛЬТАТ:", "OK" if st.errors == 0 else "ЕСТЬ ОШИБКИ")
    return 1 if st.errors else 0


if __name__ == "__main__":
    sys.exit(main())
