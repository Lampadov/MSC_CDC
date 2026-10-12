#!/usr/bin/env python3
r"""
Стресс-тест энумерации USB: плата многократно физически отключается от компьютера и подключается
обратно. Каждый цикл проверяется, что Windows (или Linux) нашла устройство, подняла все его
интерфейсы без ошибок и устройство работает: появился COM-порт, смонтировался и читается диск,
к интерфейсу WebUSB подключён драйвер WinUSB. Время появления измеряется, неудачные циклы
записываются в журнал вместе со списком USB-устройств в момент отказа.

Только стандартная библиотека Python 3.9+. pyserial нужен только для реле на линиях DTR/RTS
и для проверки COM-порта:  pip install pyserial.
Запускайте от имени администратора (нужно для режима pnputil и для чтения свойств драйверов).

Как отключать плату (--power). Без доп. железа, штатными средствами Windows:

  winport   (по умолчанию в Windows) Команда IOCTL_USB_HUB_CYCLE_PORT на хаб, к которому подключена
            плата: стек USB сам выключает питание порта и включает его снова, то есть программно
            имитирует выдёргивание и вставку кабеля. Нужна Windows 8 или новее и права
            администратора. Паузу без питания выбирает Windows, --off-min/--off-max здесь не
            действуют. Если хаб не умеет выключать питание порта (у части контроллеров и
            хабов так), порт сбрасывается и устройство энумерируется заново; от настоящего
            отключения VBUS это отличается, плату питайте тогда через реле (режим relay).
  pnputil   Windows отключает и включает устройство в диспетчере (pnputil /disable-device).
            Порт при этом остаётся под питанием, это самая мягкая проверка.

С внешним железом:
  relay     Реле или ключ в разрыве линии VBUS, управляется линией DTR или RTS любого
            USB-UART адаптера:  --relay-port COM7 --relay-line dtr [--relay-invert]
            Это настоящее отключение питания: плата перезагружается и энумерируется с нуля.
  uhubctl   USB-хаб с управлением питанием портов (программа uhubctl):
            --hub-location 1-1 --hub-port 2
  cmd       Любые свои команды: --off-cmd "..." --on-cmd "..." (умная розетка, Arduino с реле и т. п.).
  manual    Вы сами выдёргиваете и вставляете кабель; скрипт ждёт и фиксирует моменты. Скрипт
            подсказывает, что делать, и измеряет время от вставки до готовности.

Примеры (Windows, командная строка от администратора):
  python enum_stress.py msc-hid --cycles 500 --interrupt 0.3        # штатный winport
  python enum_stress.py msc-hid --power relay --relay-port COM7 --relay-line dtr --cycles 500
  python enum_stress.py webusb --power manual --cycles 50
  python enum_stress.py msc-only --power pnputil --minutes 30
  python enum_stress.py --list                 # что скрипт видит на шине сейчас

Паузы случайные, чтобы поймать редкие гонки: --off-min/--off-max (сколько плата без питания),
--settle-min/--settle-max (сколько работает до следующего отключения) и --interrupt P: с
вероятностью P питание снова отключается в середине энумерации.
В режиме manual время готовности отсчитывается от появления устройства на шине.
Итог печатается в конце, журнал циклов пишется в CSV, код выхода 1 при ошибках.
"""
import argparse
import csv
import json
import os
import random
import shutil
import statistics
import subprocess
import sys
import time

IS_WIN = os.name == "nt"

# Что проверять у каждого примера: VID/PID, сколько минимум дочерних интерфейсов ждём (MI_xx),
# метка диска, нужен ли COM-порт, какой драйвер должен быть у узла (суффикс -> служба)
PROFILES = {
    "cdc-only":   dict(vid=0xCAFE, pid=0x4001, children=0, com=True),
    "msc-only":   dict(vid=0xCAFE, pid=0x4002, children=0, label="Milandr MSC"),
    "main":       dict(vid=0xCAFE, pid=0x4003, children=2, com=True, label="Milandr MSC"),
    "hid-only":   dict(vid=0xABAB, pid=0x4004, children=0),
    "msc-hid":    dict(vid=0xCAFE, pid=0x4006, children=2, label="Milandr MSC"),
    "webusb":     dict(vid=0xCAFE, pid=0x4015, children=0, service={"": "WINUSB"}),
    "msc-webusb": dict(vid=0xCAFE, pid=0x4016, children=2, label="Milandr WEB", service={"MI_01": "WINUSB"}),
}

POLL = 0.01            # период опроса шины, с


def now():
    return time.perf_counter()


class Node:
    """Узел USB в диспетчере устройств."""
    def __init__(self, iid, started=True, problem=0, service=""):
        self.iid, self.started, self.problem, self.service = iid, started, problem, service
        self.devinst = 0

    @property
    def ok(self):
        return self.problem == 0 and self.started

    def __str__(self):
        return "%s  started=%s problem=%s service=%s" % (self.iid, self.started, self.problem, self.service or "-")


# ---------------------------------------------------------------- перечень устройств

def enum_windows():
    """Все присутствующие устройства с перечислителем USB: через SetupAPI и cfgmgr32 (быстро, ~мс)."""
    import ctypes
    from ctypes import wintypes

    class GUID(ctypes.Structure):
        _fields_ = [("a", wintypes.DWORD), ("b", wintypes.WORD), ("c", wintypes.WORD), ("d", ctypes.c_ubyte * 8)]

    class DEVINFO(ctypes.Structure):
        _fields_ = [("cbSize", wintypes.DWORD), ("guid", GUID), ("DevInst", wintypes.DWORD), ("res", ctypes.c_void_p)]

    sa = ctypes.WinDLL("setupapi", use_last_error=True)
    cm = ctypes.WinDLL("cfgmgr32")
    sa.SetupDiGetClassDevsW.restype = ctypes.c_void_p
    sa.SetupDiGetClassDevsW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD]
    sa.SetupDiEnumDeviceInfo.argtypes = [ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(DEVINFO)]
    sa.SetupDiGetDeviceInstanceIdW.argtypes = [ctypes.c_void_p, ctypes.POINTER(DEVINFO), wintypes.LPWSTR,
                                               wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    sa.SetupDiGetDeviceRegistryPropertyW.argtypes = [ctypes.c_void_p, ctypes.POINTER(DEVINFO), wintypes.DWORD,
                                                     ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p,
                                                     wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    sa.SetupDiDestroyDeviceInfoList.argtypes = [ctypes.c_void_p]
    cm.CM_Get_DevNode_Status.argtypes = [ctypes.POINTER(wintypes.ULONG), ctypes.POINTER(wintypes.ULONG),
                                         wintypes.DWORD, wintypes.ULONG]

    DIGCF_PRESENT, DIGCF_ALLCLASSES, SPDRP_SERVICE, DN_STARTED = 0x2, 0x4, 4, 0x8
    h = sa.SetupDiGetClassDevsW(None, "USB", None, DIGCF_PRESENT | DIGCF_ALLCLASSES)
    if h in (None, ctypes.c_void_p(-1).value):
        raise OSError("SetupDiGetClassDevs: ошибка %d" % ctypes.get_last_error())
    nodes = []
    try:
        i = 0
        while True:
            d = DEVINFO()
            d.cbSize = ctypes.sizeof(DEVINFO)
            if not sa.SetupDiEnumDeviceInfo(h, i, ctypes.byref(d)):
                break
            i += 1
            buf = ctypes.create_unicode_buffer(512)
            need = wintypes.DWORD()
            if not sa.SetupDiGetDeviceInstanceIdW(h, ctypes.byref(d), buf, 512, ctypes.byref(need)):
                continue
            status, problem = wintypes.ULONG(), wintypes.ULONG()
            if cm.CM_Get_DevNode_Status(ctypes.byref(status), ctypes.byref(problem), d.DevInst, 0) != 0:
                continue                                 # узел исчез между вызовами
            svc = ctypes.create_unicode_buffer(128)
            rtype = wintypes.DWORD()
            if not sa.SetupDiGetDeviceRegistryPropertyW(h, ctypes.byref(d), SPDRP_SERVICE, ctypes.byref(rtype),
                                                        svc, ctypes.sizeof(svc), ctypes.byref(need)):
                svc.value = ""
            node = Node(buf.value.upper(), bool(status.value & DN_STARTED), problem.value, svc.value)
            node.devinst = d.DevInst
            nodes.append(node)
    finally:
        sa.SetupDiDestroyDeviceInfoList(h)
    return nodes


def enum_windows_powershell():
    """Запасной вариант, если ctypes недоступен: Get-PnpDevice (медленно, около секунды на опрос)."""
    cmd = ("Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'USB\\*' } | "
           "Select-Object InstanceId,Status,Problem,Service | ConvertTo-Json -Compress")
    out = subprocess.run(["powershell", "-NoProfile", "-Command", cmd], capture_output=True, text=True).stdout
    data = json.loads(out) if out.strip() else []
    if isinstance(data, dict):
        data = [data]
    return [Node(d["InstanceId"].upper(), d.get("Status") == "OK",
                 0 if str(d.get("Problem")) in ("0", "CM_PROB_NONE", "None") else 1, d.get("Service") or "")
            for d in data]


def enum_linux():
    base = "/sys/bus/usb/devices"
    nodes = []
    for name in os.listdir(base) if os.path.isdir(base) else []:
        p = os.path.join(base, name)
        try:
            vid = open(os.path.join(p, "idVendor")).read().strip().upper()
            pid = open(os.path.join(p, "idProduct")).read().strip().upper()
        except OSError:
            continue
        iid = "USB\\VID_%s&PID_%s\\%s" % (vid, pid, name.upper())
        nodes.append(Node(iid))
        try:
            n = int(open(os.path.join(p, "bNumInterfaces")).read().strip())
        except (OSError, ValueError):
            n = 0
        if n > 1:                                         # у составного устройства по узлу на интерфейс
            for k in range(n):
                nodes.append(Node("USB\\VID_%s&PID_%s&MI_%02d\\%s" % (vid, pid, k, name.upper())))
    return nodes


_enum_impl = None


def enum_usb():
    global _enum_impl
    if _enum_impl is None:
        if IS_WIN:
            try:
                enum_windows()
                _enum_impl = enum_windows
            except Exception as e:                        # noqa: BLE001
                print("SetupAPI недоступен (%s), перехожу на PowerShell: опрос будет медленнее" % e)
                _enum_impl = enum_windows_powershell
        else:
            _enum_impl = enum_linux
    return _enum_impl()


# ---------------------------------------------------------------- проверка готовности

def drive_with_label(label):
    """Буква диска (Windows) со своей меткой тома, либо None."""
    if not IS_WIN:
        return None
    import ctypes
    k = ctypes.windll.kernel32
    mask = k.GetLogicalDrives()
    for i in range(26):
        if not mask & (1 << i):
            continue
        root = "%s:\\" % chr(65 + i)
        if k.GetDriveTypeW(root) not in (2, 3):           # съёмный или фиксированный
            continue
        buf = ctypes.create_unicode_buffer(261)
        if k.GetVolumeInformationW(root, buf, 261, None, None, None, None, 0) and buf.value == label:
            return root
    return None


def read_disk(root):
    """Читает весь диск через файловую систему: имена и содержимое всех файлов.
    Исключение - диск не работает."""
    names = sorted(os.listdir(root))
    if not names:
        raise OSError("каталог пуст")
    content = {}
    for n in names:
        p = os.path.join(root, n)
        if os.path.isfile(p):
            with open(p, "rb") as f:
                data = f.read()
            if not data:
                raise OSError("файл %s пуст" % n)
            content[n] = data
    if not content:
        raise OSError("на диске нет файлов")
    return content


def read_disk_raw(root):
    """Читает весь том как устройство (\\\\.\\E:), минуя файловую систему и кэш: быстро, без AutoPlay и антивируса.
    Возвращает {"тело": байты}; том из одних нулей считается ошибкой."""
    path = "\\\\.\\" + root.rstrip("\\")
    data = bytearray()
    with open(path, "rb", buffering=0) as f:
        while len(data) < (1 << 20):                       # дисков больше 1 МБ у примеров нет
            chunk = f.read(4096)
            if not chunk:
                break
            data += chunk
    if not data:
        raise OSError("том не отдаёт данных")
    if not any(data):
        raise OSError("том состоит из одних нулей")
    return {"тело": bytes(data)}


def com_port(vid, pid):
    try:
        from serial.tools import list_ports
    except ImportError:
        return "?"                                        # без pyserial порт не проверяем
    for p in list_ports.comports():
        if p.vid == vid and p.pid == pid:
            return p.device
    return None


class Checker:
    """Одним вызовом отвечает: устройство готово или чего не хватает."""
    def __init__(self, prof, functional=True, disk_check="raw"):
        self.p, self.functional = prof, functional
        self.disk_check = disk_check
        self.prefix = "USB\\VID_%04X&PID_%04X" % (prof["vid"], prof["pid"])
        self.disk_ref = None                               # эталон: содержимое диска при первом чтении

    def mine(self, nodes):
        return [n for n in nodes if n.iid.startswith(self.prefix)]

    def check(self, nodes):
        """-> (готово, причина)"""
        mine = self.mine(nodes)
        if not mine:
            return False, "устройство не найдено"
        dev = [n for n in mine if "&MI_" not in n.iid]
        kids = [n for n in mine if "&MI_" in n.iid]
        if not dev:
            return False, "нет узла устройства"
        bad = [n for n in mine if not n.ok]
        if bad:
            return False, "ошибка узла: " + "; ".join(str(n) for n in bad)
        if len(kids) < self.p["children"]:
            return False, "интерфейсов %d из %d" % (len(kids), self.p["children"])
        for suffix, svc in self.p.get("service", {}).items():
            tgt = [n for n in mine if (suffix in n.iid if suffix else "&MI_" not in n.iid)]
            if not tgt or not any(n.service.upper() == svc for n in tgt):
                return False, "драйвер %s не подключён (%s)" % (svc, suffix or "устройство")
        if not self.functional:
            return True, ""
        if self.p.get("com"):
            port = com_port(self.p["vid"], self.p["pid"])
            if port is None:
                return False, "нет COM-порта"
        if self.p.get("label") and IS_WIN:
            root = drive_with_label(self.p["label"])
            if not root:
                return False, "диск с меткой '%s' не смонтирован" % self.p["label"]
            if self.disk_check == "none":
                return True, ""
            try:
                content = (read_disk_raw if self.disk_check == "raw" else read_disk)(root)
            except OSError as e:
                return False, "диск %s не читается: %s" % (root, e)
            if self.disk_ref is None:
                self.disk_ref = content
            elif content != self.disk_ref:
                diff = sorted(set(content) ^ set(self.disk_ref)) or [n for n in content if content[n] != self.disk_ref[n]]
                if diff == ["тело"]:
                    a, b = content["тело"], self.disk_ref["тело"]
                    first = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
                    diff = ["тело тома, первое отличие в секторе %d (размер %d, был %d)" % (first // 512, len(a), len(b))]
                return False, "диск отличается от эталона: %s" % ", ".join(diff)
        return True, ""

    def unknown_devices(self, nodes):
        """Устройства, которые Windows не смогла опознать (сбой запроса дескриптора или сброса порта)."""
        return {n.iid for n in nodes if n.iid.startswith(("USB\\VID_0000&PID_0002", "USB\\VID_0000&PID_0001"))}


# ---------------------------------------------------------------- управление питанием

class Power:
    name = "?"

    def off(self):
        raise NotImplementedError

    def on(self):
        raise NotImplementedError

    def close(self):
        pass


class RelayPower(Power):
    name = "relay"

    def __init__(self, port, line, invert):
        try:
            import serial
        except ImportError:
            sys.exit("Для реле нужен pyserial: pip install pyserial")
        self.s = serial.Serial(port)
        self.line, self.invert = line, invert
        self._set(True)

    def _set(self, powered):
        level = powered != self.invert                    # powered=True -> линия включена (без --relay-invert)
        (self.s.setDTR if self.line == "dtr" else self.s.setRTS)(level)

    def off(self):
        self._set(False)

    def on(self):
        self._set(True)

    def close(self):
        try:
            self._set(True)                               # плату оставляем включённой
            self.s.close()
        except Exception:                                 # noqa: BLE001
            pass


class UhubctlPower(Power):
    name = "uhubctl"

    def __init__(self, location, port):
        if not shutil.which("uhubctl"):
            sys.exit("uhubctl не найден в PATH")
        self.args = ["uhubctl", "-l", location, "-p", str(port)]

    def _run(self, action):
        subprocess.run(self.args + ["-a", action], capture_output=True, check=False)

    def off(self):
        self._run("off")

    def on(self):
        self._run("on")


class CmdPower(Power):
    name = "cmd"

    def __init__(self, off_cmd, on_cmd):
        self.off_cmd, self.on_cmd = off_cmd, on_cmd

    def off(self):
        subprocess.run(self.off_cmd, shell=True, check=False)

    def on(self):
        subprocess.run(self.on_cmd, shell=True, check=False)


class WinPortPower(Power):
    """Штатное средство Windows: IOCTL_USB_HUB_CYCLE_PORT на хабе, к которому подключена плата.
    Стек USB сам отключает порт и включает его снова (питание порта выключается, если хаб это умеет;
    иначе порт сбрасывается и устройство энумерируется заново). Нужны права администратора.
    Паузу без питания здесь задать нельзя, её выбирает Windows."""
    name = "winport"
    self_recovering = True                                 # включение выполняет сама Windows

    IOCTL_USB_HUB_CYCLE_PORT = 0x00220444
    CM_DRP_ADDRESS = 0x1D                                  # номер порта на хабе

    def __init__(self, checker):
        if not IS_WIN:
            sys.exit("--power winport работает только в Windows")
        self.checker = checker
        self.hub = self.port = None

    def resolve(self):
        import ctypes
        import uuid
        from ctypes import wintypes
        cm = ctypes.WinDLL("cfgmgr32")
        node = next((n for n in self.checker.mine(enum_windows()) if "&MI_" not in n.iid), None)
        if node is None:
            sys.exit("Плата не найдена на шине, подключите её и запустите снова")

        port, rtype, size = wintypes.DWORD(), wintypes.ULONG(), wintypes.ULONG(4)
        cr = cm.CM_Get_DevNode_Registry_PropertyW(node.devinst, self.CM_DRP_ADDRESS, ctypes.byref(rtype),
                                                  ctypes.byref(port), ctypes.byref(size), 0)
        if cr != 0:
            sys.exit("Не удалось узнать номер порта платы (cfgmgr32: %d)" % cr)
        parent = wintypes.DWORD()
        if cm.CM_Get_Parent(ctypes.byref(parent), node.devinst, 0) != 0:
            sys.exit("Не удалось найти хаб, к которому подключена плата")
        hub_id = ctypes.create_unicode_buffer(512)
        cm.CM_Get_Device_IDW(parent, hub_id, 512, 0)

        guid = (ctypes.c_ubyte * 16).from_buffer_copy(uuid.UUID("F18A0E88-C30C-11D0-8815-00A0C906BED8").bytes_le)
        length = wintypes.ULONG()
        if cm.CM_Get_Device_Interface_List_SizeW(ctypes.byref(length), ctypes.byref(guid), hub_id, 0) != 0 or length.value < 2:
            sys.exit("У хаба %s нет интерфейса управления" % hub_id.value)
        buf = ctypes.create_unicode_buffer(length.value)
        if cm.CM_Get_Device_Interface_ListW(ctypes.byref(guid), hub_id, buf, length.value, 0) != 0:
            sys.exit("Не удалось получить путь к хабу %s" % hub_id.value)
        self.hub, self.port, self.hub_id = buf.value, port.value, hub_id.value
        print("Хаб: %s, порт %d" % (self.hub_id, self.port))

    def off(self):
        import ctypes
        from ctypes import wintypes
        if self.hub is None:
            self.resolve()
        k = ctypes.WinDLL("kernel32", use_last_error=True)
        k.CreateFileW.restype = ctypes.c_void_p
        k.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                  wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
        k.DeviceIoControl.argtypes = [ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD,
                                      ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
        k.CloseHandle.argtypes = [ctypes.c_void_p]
        h = k.CreateFileW(self.hub, 0x40000000, 2, None, 3, 0, None)   # GENERIC_WRITE, SHARE_WRITE, OPEN_EXISTING
        if h in (None, ctypes.c_void_p(-1).value):
            sys.exit("Не удалось открыть хаб %s (запущено от администратора?): ошибка %d" % (self.hub, ctypes.get_last_error()))
        try:
            params = (wintypes.ULONG * 2)(self.port, 0)               # ConnectionIndex, StatusReturned
            got = wintypes.DWORD()
            if k.DeviceIoControl(h, self.IOCTL_USB_HUB_CYCLE_PORT, params, 8, params, 8, ctypes.byref(got), None):
                return True
            err = ctypes.get_last_error()
        finally:
            k.CloseHandle(h)
        if err in (1, 50, 87):                             # команду хаб не понимает
            sys.exit("Хаб не поддерживает IOCTL_USB_HUB_CYCLE_PORT (ошибка %d). Нужна Windows 8 или новее, "
                     "используйте --power pnputil или реле." % err)
        self.last_error = err                              # 2 - на порту сейчас нет устройства (идёт переподключение)
        return False

    def on(self):
        pass                                               # порт включит сама Windows

    def cycle(self, retry_s=3.0):
        """Отключает порт; если Windows отвечает, что на порту нет устройства (оно ещё
        подключается после прошлого цикла), пробует снова в течение retry_s секунд."""
        t0 = now()
        while True:
            if self.off():
                return True
            if now() - t0 > retry_s:
                return False
            time.sleep(0.05)


class PnputilPower(Power):
    name = "pnputil"

    def __init__(self, checker):
        self.checker, self.iid = checker, None

    def _find(self):
        for n in self.checker.mine(enum_usb()):
            if "&MI_" not in n.iid:
                self.iid = n.iid
                return
        if not self.iid:
            sys.exit("Плата не найдена на шине, подключите её и запустите снова")

    def off(self):
        self._find()
        subprocess.run(["pnputil", "/disable-device", self.iid], capture_output=True)

    def on(self):
        subprocess.run(["pnputil", "/enable-device", self.iid], capture_output=True)


class ManualPower(Power):
    name = "manual"
    is_manual = True

    def off(self):
        print("\a  >>> ВЫДЕРНИТЕ кабель платы", flush=True)

    def on(self):
        print("\a  >>> ВСТАВЬТЕ кабель платы", flush=True)


class SimPower(Power):
    """Для самопроверки скрипта: выдуманная плата с случайными сбоями."""
    name = "simulate"

    def __init__(self, self_rec=False):
        self.present_at = None
        self.mode = "ok"
        self.rnd = random.Random(1)
        self.self_recovering = self_rec
        if self_rec:
            self.cycle = lambda retry_s=0: (self.off(), True)[1]

    def off(self):
        self.present_at = None
        if self.self_recovering:
            self.on()

    def on(self):
        r = self.rnd.random()
        self.mode = "ok" if r < 0.8 else "bad" if r < 0.9 else "none" if r < 0.95 else "half"
        self.t = now()
        self.delay = self.rnd.uniform(0.2, 0.9)
        self.present_at = self.t

    def nodes(self, prefix):
        if self.present_at is None:
            return []
        dt = now() - self.t
        if self.mode == "bad":
            return [Node("USB\\VID_0000&PID_0002\\5&1&0&2", False, 43)] if dt > 0.3 else []
        if self.mode == "none" or dt < self.delay:
            return []
        out = [Node(prefix + "\\SIM")]
        if self.mode == "ok":
            out += [Node(prefix + "&MI_00\\SIM"), Node(prefix + "&MI_01\\SIM")]
        return out


# ---------------------------------------------------------------- один цикл

class Cycle:
    def __init__(self, n):
        self.n, self.result, self.detail = n, "ok", ""
        self.off_ms = self.gone_ms = self.seen_ms = self.ready_ms = None
        self.interrupted = False
        self.bad_enum = 0
        self.slow = False


def snapshot(nodes):
    return "\n".join("    " + str(n) for n in sorted(nodes, key=lambda n: n.iid) if "ROOT_HUB" not in n.iid)


def wait(pred, timeout):
    """Ждёт, пока pred() вернёт истину; возвращает время ожидания или None по таймауту."""
    t0 = now()
    while now() - t0 < timeout:
        if pred():
            return now() - t0
        time.sleep(POLL)
    return None


def run_cycle(n, args, power, chk, sim, log):
    c = Cycle(n)
    get = (lambda: sim.nodes(chk.prefix)) if sim else enum_usb
    manual = getattr(power, "is_manual", False)

    off_s = random.uniform(args.off_min, args.off_max)
    c.off_ms = int(off_s * 1000)
    base_unknown = chk.unknown_devices(get())

    # 1. отключаем и ждём, пока устройство исчезнет
    t_off = now()
    if hasattr(power, "cycle"):
        if not power.cycle():
            c.result, c.detail = "ошибка_команды", "хаб отклонил отключение порта (ошибка Windows %s)" % getattr(power, "last_error", "?")
            return c
    else:
        power.off()
    # устройство "ушло", когда на шине нет его исправных узлов (отключённое pnputil остаётся в списке с ошибкой)
    gone = wait(lambda: not any(n.ok for n in chk.mine(get())), args.gone_timeout if not manual else 600)
    self_rec = getattr(power, "self_recovering", False)
    if gone is None:
        c.result, c.detail = "не_отключилась", "устройство осталось на шине через %.0f с после отключения" % args.gone_timeout
        return c
    c.gone_ms = int(gone * 1000)
    if not manual and not self_rec:                        # у winport паузу выбирает Windows
        rest = off_s - (now() - t_off)
        if rest > 0:
            time.sleep(rest)

    # 2. включаем; иногда обрываем энумерацию на полуслове
    interrupts = 0
    while True:
        t_on = t_off if (self_rec and interrupts == 0) else now()   # у winport отсчёт от команды
        power.on()
        if manual:                                         # отсчёт от появления, а не от подсказки
            wait(lambda: chk.mine(get()), 600)
            t_on = now()
        if not manual and random.random() < args.interrupt and interrupts < 2:
            interrupts += 1
            c.interrupted = True
            if self_rec:                                   # порт можно сбросить, лишь когда устройство уже на нём
                wait(lambda: chk.mine(get()), 5)
            time.sleep(random.uniform(0.0 if self_rec else 0.02, args.interrupt_max))
            if self_rec:
                if not power.cycle():
                    c.result, c.detail = "ошибка_команды", "хаб отклонил повторное отключение порта (ошибка Windows %s)" % getattr(power, "last_error", "?")
                    return c
            else:
                power.off()
                time.sleep(random.uniform(args.off_min, args.off_max))
            continue
        break

    # 3. ждём появления и готовности
    seen = ready = None
    why = "устройство не найдено"
    timeline = []                                          # что и когда мешало готовности (для медленных циклов)
    while now() - t_on < args.timeout:
        nodes = get()
        if seen is None and chk.mine(nodes):
            seen = now() - t_on
        ready_now, why = chk.check(nodes)
        if not ready_now and (not timeline or timeline[-1][1] != why):
            timeline.append((int((now() - t_on) * 1000), why))
        if ready_now:
            ready = now() - t_on
            break
        time.sleep(POLL)

    nodes = get()
    c.bad_enum = len(chk.unknown_devices(nodes) - base_unknown)
    if seen is not None:
        c.seen_ms = int(seen * 1000)
    if ready is not None:
        c.ready_ms = int(ready * 1000)
        if c.ready_ms > args.slow_ms:                      # цикл прошёл, но медленно: фиксируем, чего ждали
            c.slow = True
            log.write("--- цикл %d: медленный, готова за %d мс (появилась за %s мс). Чего ждали:\n%s\n"
                      % (n, c.ready_ms, c.seen_ms, "\n".join("    с %6d мс: %s" % t for t in timeline)))
            log.flush()
        return c

    if seen is None:
        c.result = "не_появилась" if not c.bad_enum else "ошибка_энумерации"
        c.detail = "устройство не найдено за %.0f с" % args.timeout
        if c.bad_enum:
            c.detail += "; Windows показывает неопознанное USB-устройство (сбой запроса дескриптора или сброса порта)"
    else:
        c.result = "не_готова"
        c.detail = why
    log.write("--- цикл %d: %s: %s\n  USB-устройства:\n%s\n" % (n, c.result, c.detail, snapshot(nodes)))
    log.flush()
    return c


# ---------------------------------------------------------------- итоги

def pct(v, p):
    s = sorted(v)
    return s[min(len(s) - 1, int(len(s) * p))]


def summary(cycles, t_start):
    total = len(cycles)
    if not total:
        print("Циклов не было.")
        return 0
    fails = [c for c in cycles if c.result != "ok"]
    print("\n" + "=" * 70)
    print("Циклов: %d, успешных: %d (%.2f %%), отказов: %d, время теста: %.0f с"
          % (total, total - len(fails), 100.0 * (total - len(fails)) / total, len(fails), now() - t_start))
    kinds = {}
    for c in fails:
        kinds[c.result] = kinds.get(c.result, 0) + 1
    for k, v in sorted(kinds.items()):
        print("  %-20s %d" % (k, v))
    slow = [c for c in cycles if c.slow]
    if slow:
        print("Медленных циклов (готовность дольше порога): %d, подробности в .log" % len(slow))
    interrupted = [c for c in cycles if c.interrupted]
    if interrupted:
        print("С обрывом энумерации: %d циклов, отказов среди них: %d"
              % (len(interrupted), sum(1 for c in interrupted if c.result != "ok")))
    unknown = sum(1 for c in cycles if c.bad_enum)
    if unknown:
        print("Циклов, где Windows показывала неопознанное устройство: %d" % unknown)
    for title, key in (("Появление на шине, мс", "seen_ms"), ("Готовность (все интерфейсы и функция), мс", "ready_ms")):
        v = [getattr(c, key) for c in cycles if getattr(c, key) is not None]
        if v:
            print("%s: min %d, медиана %d, p95 %d, max %d" % (title, min(v), statistics.median(v), pct(v, 0.95), max(v)))
    print("=" * 70)
    return 1 if fails else 0


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description="Стресс-тест энумерации USB с физическим отключением платы",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("example", nargs="?", choices=sorted(PROFILES), help="какой пример тестируем")
    ap.add_argument("--vid", type=lambda s: int(s, 0), help="свой VID (вместо профиля)")
    ap.add_argument("--pid", type=lambda s: int(s, 0), help="свой PID (вместо профиля)")
    ap.add_argument("--list", action="store_true", help="показать устройства на шине и выйти")
    ap.add_argument("--power", choices=["winport", "relay", "uhubctl", "cmd", "pnputil", "manual"],
                    default="winport" if IS_WIN else "manual", help="чем отключать плату (по умолчанию в Windows winport)")
    ap.add_argument("--relay-port"); ap.add_argument("--relay-line", choices=["dtr", "rts"], default="dtr")
    ap.add_argument("--relay-invert", action="store_true", help="инвертировать линию (реле включается нулём)")
    ap.add_argument("--hub-location"); ap.add_argument("--hub-port", type=int)
    ap.add_argument("--off-cmd"); ap.add_argument("--on-cmd")
    ap.add_argument("--cycles", type=int, default=100)
    ap.add_argument("--minutes", type=float, help="ограничить тест по времени")
    ap.add_argument("--off-min", type=float, default=0.1, help="минимум паузы без питания, с")
    ap.add_argument("--off-max", type=float, default=2.0, help="максимум паузы без питания, с")
    ap.add_argument("--settle-min", type=float, default=0.5, help="минимум работы до следующего отключения, с")
    ap.add_argument("--settle-max", type=float, default=3.0, help="максимум работы до следующего отключения, с")
    ap.add_argument("--interrupt", type=float, default=0.0, metavar="P",
                    help="вероятность 0..1, что питание отключат снова прямо во время энумерации")
    ap.add_argument("--interrupt-max", type=float, default=0.6, help="максимум задержки до такого обрыва, с")
    ap.add_argument("--timeout", type=float, default=15.0, help="сколько ждать готовности после включения, с")
    ap.add_argument("--gone-timeout", type=float, default=10.0, help="сколько ждать исчезновения устройства, с")
    ap.add_argument("--slow-ms", type=int, default=3000, help="цикл дольше этого (мс) пишется в журнал как медленный")
    ap.add_argument("--disk-check", choices=["raw", "files", "none"], default="raw",
                    help="как проверять диск: raw - читать том целиком (быстро, по умолчанию), files - через файловую систему")
    ap.add_argument("--no-functional", action="store_true", help="не проверять диск и COM-порт, только узлы USB")
    ap.add_argument("--stop-on-fail", action="store_true", help="остановиться на первом отказе, оставив плату как есть")
    ap.add_argument("--seed", type=int, help="зерно случайных пауз (повторение прогона)")
    ap.add_argument("--out", help="файл CSV (по умолчанию enum_<пример>_<время>.csv)")
    ap.add_argument("--selftest", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args()

    if args.list:
        for n in sorted(enum_usb(), key=lambda n: n.iid):
            if "ROOT_HUB" not in n.iid:
                print(n)
        return 0
    if not args.example and args.vid is None:
        ap.error("укажите пример (cdc-only, msc-only, ...) или --vid/--pid")

    prof = dict(PROFILES.get(args.example, dict(vid=0, pid=0, children=0)))
    if args.vid is not None:
        prof["vid"] = args.vid
    if args.pid is not None:
        prof["pid"] = args.pid
    chk = Checker(prof, not args.no_functional, args.disk_check)

    sim = None
    if args.selftest:
        sim = SimPower(self_rec=args.power == "winport")
        power = sim
        global POLL
        POLL = 0.002
    elif args.power == "relay":
        if not args.relay_port:
            ap.error("--relay-port обязателен для --power relay")
        power = RelayPower(args.relay_port, args.relay_line, args.relay_invert)
    elif args.power == "uhubctl":
        if not args.hub_location or not args.hub_port:
            ap.error("нужны --hub-location и --hub-port")
        power = UhubctlPower(args.hub_location, args.hub_port)
    elif args.power == "cmd":
        if not args.off_cmd or not args.on_cmd:
            ap.error("нужны --off-cmd и --on-cmd")
        power = CmdPower(args.off_cmd, args.on_cmd)
    elif args.power == "winport":
        power = WinPortPower(chk)
    elif args.power == "pnputil":
        power = PnputilPower(chk)
    else:
        power = ManualPower()

    seed = args.seed if args.seed is not None else random.randrange(1 << 30)
    random.seed(seed)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    name = args.example or "%04X_%04X" % (prof["vid"], prof["pid"])
    out = args.out or "enum_%s_%s.csv" % (name, stamp)
    log_path = os.path.splitext(out)[0] + ".log"
    print("Пример: %s (VID %04X PID %04X), питание: %s, сид: %d" % (name, prof["vid"], prof["pid"], power.name, seed))
    print("Журнал: %s, отказы подробно: %s" % (out, log_path))

    # плата должна быть на месте и готова
    if not sim:
        if getattr(power, "is_manual", False):
            print("Подключите плату...")
        t = wait(lambda: chk.check(enum_usb())[0], 30)
        if t is None:
            print("Плата не готова до начала теста: %s" % chk.check(enum_usb())[1])
            return 2

    cycles, t_start = [], now()
    with open(out, "w", newline="", encoding="utf-8") as f, open(log_path, "w", encoding="utf-8") as log:
        w = csv.writer(f, delimiter=";")
        w.writerow(["цикл", "время", "без_питания_мс", "исчезла_мс", "появилась_мс", "готова_мс", "обрыв", "неопознанное", "результат", "подробности"])
        try:
            n = 0
            while n < args.cycles and (not args.minutes or now() - t_start < args.minutes * 60):
                n += 1
                if getattr(power, "is_manual", False):
                    print("Цикл %d: выдерните кабель..." % n, flush=True)
                c = run_cycle(n, args, power, chk, sim, log)
                cycles.append(c)
                w.writerow([n, time.strftime("%H:%M:%S"), c.off_ms, c.gone_ms, c.seen_ms, c.ready_ms,
                            int(c.interrupted), c.bad_enum, c.result, c.detail])
                f.flush()
                ok = sum(1 for x in cycles if x.result == "ok")
                print("%4d: %-18s %s  (успешно %d из %d)" % (
                    n, c.result,
                    ("готова за %d мс (появилась за %s)%s%s" % (c.ready_ms, c.seen_ms, ", с обрывом" if c.interrupted else "", " МЕДЛЕННО" if c.slow else "")) if c.result == "ok" else c.detail,
                    ok, n), flush=True)
                if c.result != "ok":
                    if args.stop_on_fail:
                        print("Остановлено на отказе. Плата оставлена в текущем состоянии.")
                        break
                    # приводим плату в порядок: длинное отключение
                    if hasattr(power, "cycle"):
                        power.cycle(10)
                    else:
                        power.off()
                        time.sleep(3)
                        power.on()
                    wait(lambda: (sim and True) or chk.check(enum_usb())[0], args.timeout)
                time.sleep(random.uniform(args.settle_min, args.settle_max) if not sim else 0)
        except KeyboardInterrupt:
            print("\nПрервано.")
        finally:
            power.close()
    return summary(cycles, t_start)


if __name__ == "__main__":
    sys.exit(main())
