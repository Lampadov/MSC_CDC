#!/usr/bin/env python3
r"""
Простой тест энумерации (Windows, запуск от администратора).

Раз за разом программно "выдёргивает" порт, к которому подключена плата, и проверяет, что Windows
нашла устройство и подняла все его интерфейсы без ошибок. Печатает время каждого подключения.

Критерий успеха цикла: у всех узлов устройства в диспетчере нет ошибок и драйверы запущены, поднялись
все интерфейсы. Если у примера есть диск (msc-only, msc-hid, msc-webusb), он ещё должен смонтироваться,
читаться целиком и совпадать с образом, прочитанным при первом подключении.

  python enum_test.py <пример> [число циклов] [пауза между циклами, с]

  python enum_test.py msc-hid           # 100 циклов
  python enum_test.py cdc-only 500 0    # 500 циклов без паузы

Примеры: cdc-only, hid-mouse, hid-keyboard, msc-only, msc-hid, webusb-only, msc-webusb.
Плату подключайте прямо к порту компьютера, без хаба. Код выхода 1, если были отказы.
"""
import ctypes
import sys
import time
import uuid
from ctypes import wintypes

# пример: (VID, PID, сколько интерфейсов-функций должно подняться кроме самого устройства)
EXAMPLES = {
    "cdc-only":   (0xCAFE, 0x4001, 0),
    "hid-mouse":    (0xCAFE, 0x4002, 0),   # каталог examples/hid-only, по умолчанию мышь
    "hid-only":     (0xCAFE, 0x4002, 0),   # старое имя, то же что hid-mouse
    "hid-keyboard": (0xCAFE, 0x4003, 0),   # examples/hid-only, собранный с HID_MODE=2 (клавиатура)
    "msc-only":   (0xCAFE, 0x4004, 0),
    "msc-hid":    (0xCAFE, 0x4006, 2),
    "webusb-only": (0xCAFE, 0x4005, 0),
    "msc-webusb": (0xCAFE, 0x4007, 2),
}
# метка диска у примеров с флешкой
LABELS = {"msc-only": "Milandr MSC", "msc-hid": "Milandr MSC", "msc-webusb": "Milandr WEB"}
TIMEOUT = 10.0                      # сколько ждать появления устройства, с

setupapi = ctypes.WinDLL("setupapi", use_last_error=True)
cfgmgr = ctypes.WinDLL("cfgmgr32")
kernel = ctypes.WinDLL("kernel32", use_last_error=True)


class GUID(ctypes.Structure):
    _fields_ = [("a", wintypes.DWORD), ("b", wintypes.WORD), ("c", wintypes.WORD), ("d", ctypes.c_ubyte * 8)]


class DEVINFO(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.DWORD), ("guid", GUID), ("DevInst", wintypes.DWORD), ("res", ctypes.c_void_p)]


setupapi.SetupDiGetClassDevsW.restype = ctypes.c_void_p
setupapi.SetupDiGetClassDevsW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD]
setupapi.SetupDiEnumDeviceInfo.argtypes = [ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(DEVINFO)]
setupapi.SetupDiGetDeviceInstanceIdW.argtypes = [ctypes.c_void_p, ctypes.POINTER(DEVINFO), wintypes.LPWSTR,
                                                 wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
setupapi.SetupDiDestroyDeviceInfoList.argtypes = [ctypes.c_void_p]
cfgmgr.CM_Get_DevNode_Status.argtypes = [ctypes.POINTER(wintypes.ULONG), ctypes.POINTER(wintypes.ULONG),
                                         wintypes.DWORD, wintypes.ULONG]
kernel.CreateFileW.restype = ctypes.c_void_p
kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                               wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
kernel.DeviceIoControl.argtypes = [ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD,
                                   ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
kernel.CloseHandle.argtypes = [ctypes.c_void_p]
kernel.GetVolumeInformationW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.DWORD, ctypes.c_void_p,
                                         ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD]


def usb_nodes():
    """Все присутствующие USB-устройства: список (идентификатор, devinst, исправен)."""
    h = setupapi.SetupDiGetClassDevsW(None, "USB", None, 0x2 | 0x4)       # PRESENT | ALLCLASSES
    if h in (None, ctypes.c_void_p(-1).value):
        sys.exit("Не удалось получить список USB-устройств (ошибка %d)" % ctypes.get_last_error())
    result, i = [], 0
    try:
        while True:
            d = DEVINFO()
            d.cbSize = ctypes.sizeof(DEVINFO)
            if not setupapi.SetupDiEnumDeviceInfo(h, i, ctypes.byref(d)):
                break
            i += 1
            buf = ctypes.create_unicode_buffer(512)
            need = wintypes.DWORD()
            if not setupapi.SetupDiGetDeviceInstanceIdW(h, ctypes.byref(d), buf, 512, ctypes.byref(need)):
                continue
            status, problem = wintypes.ULONG(), wintypes.ULONG()
            if cfgmgr.CM_Get_DevNode_Status(ctypes.byref(status), ctypes.byref(problem), d.DevInst, 0) != 0:
                continue
            ok = problem.value == 0 and bool(status.value & 0x8)         # без ошибки и запущен (DN_STARTED)
            result.append((buf.value.upper(), d.DevInst, ok))
    finally:
        setupapi.SetupDiDestroyDeviceInfoList(h)
    return result


disk_reference = {}                 # образ диска, прочитанный при первом подключении


def read_disk(label):
    """Находит том по метке и читает его целиком как устройство (\\\\.\\E:), минуя файловую систему.
    Возвращает (данные, "") или (None, причина)."""
    mask = kernel.GetLogicalDrives()
    for i in range(26):
        if not mask & (1 << i):
            continue
        root = "%s:\\" % chr(65 + i)
        name = ctypes.create_unicode_buffer(261)
        if kernel.GetVolumeInformationW(root, name, 261, None, None, None, None, 0) and name.value == label:
            data = bytearray()
            try:
                with open("\\\\.\\%s:" % chr(65 + i), "rb", buffering=0) as f:
                    while len(data) < (1 << 20):
                        chunk = f.read(4096)
                        if not chunk:
                            break
                        data += chunk
            except OSError as e:
                return None, "диск %s: ошибка чтения (%s)" % (root[:2], e)
            if not any(data):
                return None, "диск %s отдаёт одни нули" % root[:2]
            return bytes(data), ""
    return None, "диск '%s' не смонтирован" % label


def board_state(prefix, children, label=None):
    """-> (готова, описание): устройство и все его интерфейсы найдены и исправны, диск (если есть) читается."""
    mine = [n for n in usb_nodes() if n[0].startswith(prefix)]
    if not mine:
        return False, "устройства нет на шине"
    bad = [n[0] for n in mine if not n[2]]
    if bad:
        return False, "ошибка у " + ", ".join(bad)
    kids = [n for n in mine if "&MI_" in n[0]]
    if len(kids) < children:
        return False, "поднялось интерфейсов %d из %d" % (len(kids), children)
    if label:
        data, why = read_disk(label)
        if data is None:
            return False, why
        if "data" not in disk_reference:
            disk_reference["data"] = data
        elif data != disk_reference["data"]:
            return False, "диск отличается от первого чтения (размер %d, был %d)" % (len(data), len(disk_reference["data"]))
    return True, ""


def find_port(prefix):
    """Путь к хабу и номер порта, к которому подключена плата."""
    dev = next((n for n in usb_nodes() if n[0].startswith(prefix) and "&MI_" not in n[0]), None)
    if dev is None:
        sys.exit("Плата не найдена на шине: подключите её и проверьте VID/PID")
    port, rtype, size = wintypes.DWORD(), wintypes.ULONG(), wintypes.ULONG(4)
    if cfgmgr.CM_Get_DevNode_Registry_PropertyW(dev[1], 0x1D, ctypes.byref(rtype), ctypes.byref(port),
                                                ctypes.byref(size), 0) != 0:      # CM_DRP_ADDRESS
        sys.exit("Не удалось узнать номер порта")
    parent = wintypes.DWORD()
    if cfgmgr.CM_Get_Parent(ctypes.byref(parent), dev[1], 0) != 0:
        sys.exit("Не удалось найти хаб")
    hub_id = ctypes.create_unicode_buffer(512)
    cfgmgr.CM_Get_Device_IDW(parent, hub_id, 512, 0)
    guid = (ctypes.c_ubyte * 16).from_buffer_copy(uuid.UUID("F18A0E88-C30C-11D0-8815-00A0C906BED8").bytes_le)
    length = wintypes.ULONG()
    if cfgmgr.CM_Get_Device_Interface_List_SizeW(ctypes.byref(length), ctypes.byref(guid), hub_id, 0) != 0 or length.value < 2:
        sys.exit("У хаба нет интерфейса управления")
    path = ctypes.create_unicode_buffer(length.value)
    if cfgmgr.CM_Get_Device_Interface_ListW(ctypes.byref(guid), hub_id, path, length.value, 0) != 0:
        sys.exit("Не удалось получить путь к хабу")
    print("Хаб %s, порт %d" % (hub_id.value, port.value))
    return path.value, port.value


def cycle_port(hub, port):
    """Выключает и включает порт (IOCTL_USB_HUB_CYCLE_PORT). True, если хаб принял команду."""
    h = kernel.CreateFileW(hub, 0x40000000, 2, None, 3, 0, None)   # GENERIC_WRITE, SHARE_WRITE, OPEN_EXISTING
    if h in (None, ctypes.c_void_p(-1).value):
        sys.exit("Не удалось открыть хаб: запустите от имени администратора (ошибка %d)" % ctypes.get_last_error())
    try:
        params = (wintypes.ULONG * 2)(port, 0)
        got = wintypes.DWORD()
        return bool(kernel.DeviceIoControl(h, 0x00220444, params, 8, params, 8, ctypes.byref(got), None))
    finally:
        kernel.CloseHandle(h)


def wait(cond, timeout):
    """Ждёт, пока cond() вернёт истину. Возвращает затраченное время или None."""
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < timeout:
        if cond():
            return time.perf_counter() - t0
        time.sleep(0.01)
    return None


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in EXAMPLES:
        sys.exit(__doc__)
    vid, pid, children = EXAMPLES[sys.argv[1]]
    cycles = int(sys.argv[2]) if len(sys.argv) > 2 else 100
    pause = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5
    prefix = "USB\\VID_%04X&PID_%04X" % (vid, pid)
    label = LABELS.get(sys.argv[1])

    if wait(lambda: board_state(prefix, children, label)[0], 15) is None:
        sys.exit("Плата не готова перед началом теста: " + board_state(prefix, children, label)[1])
    hub, port = find_port(prefix)

    times, fails = [], 0
    for n in range(1, cycles + 1):
        t0 = time.perf_counter()
        # хаб может отказать, пока плата ещё подключается после прошлого цикла: повторяем до 3 с
        if wait(lambda: cycle_port(hub, port), 3) is None:
            print("%4d: ОШИБКА, хаб не принял команду" % n)
            fails += 1
            continue
        if wait(lambda: not any(x[2] for x in usb_nodes() if x[0].startswith(prefix)), 5) is None:
            print("%4d: ОШИБКА, плата не отключилась" % n)
            fails += 1
            continue
        if wait(lambda: board_state(prefix, children, label)[0], TIMEOUT) is None:
            print("%4d: ОШИБКА за %d с: %s" % (n, TIMEOUT, board_state(prefix, children, label)[1]))
            fails += 1
            continue
        ms = int((time.perf_counter() - t0) * 1000)
        times.append(ms)
        print("%4d: ok, %d мс" % (n, ms))
        time.sleep(pause)

    print("\nЦиклов %d, успешно %d, ошибок %d" % (cycles, len(times), fails))
    if times:
        print("Время подключения, мс: min %d, среднее %d, max %d" % (min(times), sum(times) // len(times), max(times)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
