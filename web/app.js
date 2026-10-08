// WebUSB-демо Миландр: связь страницы с платой (см. main.c в прошивке).
//
// Формат кадров (little-endian):
//   плата -> страница  [0x01][кнопки][светодиоды][АЦП: 2 байта][мс с запуска: 4 байта]  (9 байт)
//   страница -> плата  [0x01][светодиоды]                                               (2 байта)

const USB_VID = 0xCAFE;          // должен совпадать с USB_VID в usb_descriptors.c
const EP_OUT  = 1;               // EP1 OUT: команды
const EP_IN   = 2;               // EP2 IN:  кадры состояния
const FRAME_SIZE = 9;
const VREF = 3.3;                // опорное напряжение АЦП, В (для пересчёта кода в вольты)
const HISTORY = 500;             // сколько последних отсчётов показывать на графике (около 10 с)

// ---------- разбор и сборка кадров (отдельные функции: их проверяют тесты) ----------
function parseFrame(view) {
  if (view.byteLength < FRAME_SIZE || view.getUint8(0) !== 0x01) return null;
  return {
    buttons: view.getUint8(1),
    leds: view.getUint8(2),
    adc: view.getUint16(3, true),
    uptimeMs: view.getUint32(5, true),
  };
}

function ledsCommand(mask) {
  return new Uint8Array([0x01, mask & 0x03]);
}

if (typeof module !== 'undefined') module.exports = { parseFrame, ledsCommand };

// ---------- остальной код работает только в браузере ----------
if (typeof document !== 'undefined') (function () {
  const $ = (id) => document.getElementById(id);
  let device = null;
  let ledMask = 0;                 // что показывает плата
  let wantedMask = null;           // что мы просили, пока ждём подтверждения
  let sentAt = 0;
  const history = [];
  let frames = 0, lastFpsTime = performance.now();

  if (!navigator.usb) {
    $('nosupport').style.display = 'block';
    $('connect').disabled = true;
    return;
  }

  function setStatus(text, on) {
    $('statusText').textContent = text;
    $('status').classList.toggle('on', on);
    $('connect').textContent = on ? 'Отключить' : 'Подключить плату';
  }

  async function connect(dev) {
    device = dev;
    await device.open();
    if (device.configuration === null) await device.selectConfiguration(1);
    await device.claimInterface(0);
    setStatus('подключено: ' + (device.productName || 'плата'), true);
    readLoop();
  }

  async function disconnect() {
    if (!device) return;
    const d = device;
    device = null;
    try { await d.close(); } catch (e) { /* уже отключена */ }
    setStatus('не подключено', false);
  }

  async function readLoop() {
    const dev = device;
    while (device === dev && dev) {
      try {
        const r = await dev.transferIn(EP_IN, 64);
        if (r.status === 'ok') onData(r.data);
        else if (r.status === 'stall') await dev.clearHalt('in', EP_IN);
      } catch (e) {
        if (device === dev) { await disconnect(); }
        return;
      }
    }
  }

  function onData(view) {
    // в одном USB-пакете может прийти несколько кадров подряд
    for (let off = 0; off + FRAME_SIZE <= view.byteLength; off += FRAME_SIZE) {
      const f = parseFrame(new DataView(view.buffer, view.byteOffset + off, FRAME_SIZE));
      if (f) showFrame(f);
    }
  }

  function showFrame(f) {
    for (let i = 0; i < 5; i++) $('k' + i).classList.toggle('down', !!(f.buttons & (1 << i)));

    ledMask = f.leds;
    for (let i = 0; i < 2; i++) $('led' + i).classList.toggle('on', !!(f.leds & (1 << i)));
    if (wantedMask !== null && f.leds === wantedMask) {         // плата подтвердила команду
      $('rtt').textContent = Math.round(performance.now() - sentAt) + ' мс';
      wantedMask = null;
    }

    history.push(f.adc);
    if (history.length > HISTORY) history.shift();
    $('adc').textContent = f.adc;
    $('volt').textContent = (f.adc * VREF / 4096).toFixed(3) + ' В';
    const s = Math.floor(f.uptimeMs / 1000);
    $('uptime').textContent = Math.floor(s / 60) + ' мин ' + (s % 60) + ' с';

    frames++;
    const now = performance.now();
    if (now - lastFpsTime >= 1000) {
      $('fps').textContent = Math.round(frames * 1000 / (now - lastFpsTime));
      frames = 0; lastFpsTime = now;
    }
  }

  async function toggleLed(i) {
    if (!device) return;
    wantedMask = ledMask ^ (1 << i);
    sentAt = performance.now();
    try { await device.transferOut(EP_OUT, ledsCommand(wantedMask)); }
    catch (e) { await disconnect(); }
  }

  // ---------- график ----------
  const canvas = $('chart');
  const ctx = canvas.getContext('2d');
  function draw() {
    const w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = '#2a3541'; ctx.lineWidth = 1;
    for (let i = 1; i < 4; i++) { ctx.beginPath(); ctx.moveTo(0, h * i / 4); ctx.lineTo(w, h * i / 4); ctx.stroke(); }
    if (history.length > 1) {
      let min = Math.min(...history), max = Math.max(...history);
      if (max - min < 8) { const c = (max + min) / 2; min = c - 4; max = c + 4; }   // не растягивать шум на весь график
      ctx.fillStyle = '#8b98a5'; ctx.font = '12px system-ui';
      ctx.fillText(Math.round(max), 6, 14); ctx.fillText(Math.round(min), 6, h - 6);
      ctx.strokeStyle = '#3fb6ff'; ctx.lineWidth = 2; ctx.beginPath();
      history.forEach((v, i) => {
        const x = i * w / (HISTORY - 1), y = h - 20 - (v - min) / (max - min) * (h - 40);
        i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      });
      ctx.stroke();
    }
    requestAnimationFrame(draw);
  }
  draw();

  // ---------- события ----------
  $('connect').onclick = async () => {
    if (device) return disconnect();
    try {
      connect(await navigator.usb.requestDevice({ filters: [{ vendorId: USB_VID }] }));
    } catch (e) { /* окно выбора закрыли */ }
  };
  $('led0').onclick = () => toggleLed(0);
  $('led1').onclick = () => toggleLed(1);

  navigator.usb.addEventListener('disconnect', (e) => { if (e.device === device) disconnect(); });
  navigator.usb.addEventListener('connect', async (e) => { if (!device && e.device.vendorId === USB_VID) connect(e.device); });
  navigator.usb.getDevices().then((list) => {                  // плата уже разрешена: подключаемся сразу
    const d = list.find((x) => x.vendorId === USB_VID);
    if (d && !device) connect(d);
  });
})();
