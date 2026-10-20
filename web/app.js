// Осциллограф для платы К1986ВЕ92FI: отсчёты АЦП приходят по WebUSB кадрами.
//
// Кадр (little-endian), см. main.c в прошивке:
//   [A5][5A][флаги: бит 0 - запуск по синхронизации][номер скорости][число отсчётов: 2][номер кадра: 2]
//   [частота дискретизации, Гц: 4][отсчёты по 2 байта, 12 бит]
// Команды (vendor-запрос на интерфейс 1, значение в wValue):
//   0x11  биты 0..3 - номер скорости, биты 4..10 - число отсчётов / 16
//   0x12  биты 0..11 - уровень синхронизации, бит 12 - спад, бит 13 - ждущий режим, бит 14 - пуск

const USB_VID = 0xCAFE;     // как USB_VID в usb_descriptors.c
const VREF = 3.3, FULL = 4095, HEAD = 12;
const RATES = ['~357 квыб/с', '~178 квыб/с', '~89 квыб/с', '~45 квыб/с', '~22 квыб/с', '~11 квыб/с'];
const LENS = [160, 352, 704, 1120];
const $ = (id) => document.getElementById(id);
const cv = $('cv'), g = cv.getContext('2d');
let dev = null, running = true, frame = null, lastSeq = -1, fps = 0, t0 = performance.now(), cnt = 0;

for (const [i, r] of RATES.entries()) $('rate').add(new Option(r, i));
for (const n of LENS) $('len').add(new Option(n + ' точек', n));
$('len').value = 352;

function fmt(v, u) {
  const a = Math.abs(v);
  if (a >= 1) return v.toFixed(2) + ' ' + u;
  if (a >= 1e-3) return (v * 1e3).toFixed(1) + ' м' + u;
  return (v * 1e6).toFixed(1) + ' мк' + u;
}
const volts = (c) => c * VREF / FULL;

// Очередь команд: один controlTransfer за раз, в конце отправляется самое свежее состояние
let busy = false, again = false;
async function send() {
  if (!dev) return;
  if (busy) { again = true; return; }
  busy = true;
  try {
    do {
      again = false;
      const cap = Number($('rate').value) | (Number($('len').value) / 16 << 4);
      const trg = Number($('lvl').value) | (Number($('edge').value) << 12) | (Number($('mode').value) << 13) | ((running ? 1 : 0) << 14);
      for (const [request, value] of [[0x11, cap], [0x12, trg]])
        await dev.controlTransferOut({ requestType: 'vendor', recipient: 'interface', request, value, index: 1 });
    } while (again);
  } catch (e) { setStatus('Ошибка: ' + e.message); }
  busy = false;
}

function setStatus(t) { $('status').textContent = t; }

function parse(buf) {                      // buf - Uint8Array с накопленными данными; возвращает остаток
  for (;;) {
    let i = 0;
    while (i + 1 < buf.length && !(buf[i] === 0xA5 && buf[i + 1] === 0x5A)) i++;
    buf = buf.subarray(i);
    if (buf.length < HEAD) return buf;
    const v = new DataView(buf.buffer, buf.byteOffset, buf.length);
    const n = v.getUint16(4, true), len = HEAD + 2 * n;
    if (n < 16 || n > 1120) return buf.subarray(2);
    if (buf.length < len) return buf;
    if (running) {
      const s = new Uint16Array(n);
      for (let k = 0; k < n; k++) s[k] = v.getUint16(HEAD + 2 * k, true);
      frame = { trig: buf[2] & 1, fs: v.getUint32(8, true), s };
      cnt++;
    }
    buf = buf.subarray(len);
  }
}

async function connect(d) {
  dev = d;
  await dev.open();
  if (dev.configuration === null) await dev.selectConfiguration(1);
  await dev.claimInterface(1);
  setStatus('Плата подключена'); $('connect').textContent = 'Отключить';
  await send();
  let rest = new Uint8Array(0);
  while (dev === d) {
    try {
      const r = await d.transferIn(3, 1024);
      const a = new Uint8Array(r.data.buffer, r.data.byteOffset, r.data.byteLength);
      const m = new Uint8Array(rest.length + a.length); m.set(rest); m.set(a, rest.length);
      rest = parse(m);
    } catch (e) { break; }
  }
  if (dev === d) disconnect();
}

async function disconnect() {
  const d = dev; dev = null;
  setStatus('Плата не подключена'); $('connect').textContent = 'Подключить плату';
  try { await d.close(); } catch (e) {}
}

// ---------- рисование ----------
function draw() {
  const W = cv.width, H = cv.height, dx = W / 10, dy = H / 8;
  g.fillStyle = '#10151a'; g.fillRect(0, 0, W, H);
  g.strokeStyle = '#26323b'; g.lineWidth = 1; g.beginPath();
  for (let i = 0; i <= 10; i++) { g.moveTo(i * dx + .5, 0); g.lineTo(i * dx + .5, H); }
  for (let j = 0; j <= 8; j++) { g.moveTo(0, j * dy + .5); g.lineTo(W, j * dy + .5); }
  g.stroke();
  const Y = (c) => H - 10 - (c / FULL) * (H - 20);
  const lv = Number($('lvl').value);
  g.strokeStyle = '#8a5a14'; g.setLineDash([6, 6]); g.beginPath(); g.moveTo(0, Y(lv)); g.lineTo(W, Y(lv)); g.stroke(); g.setLineDash([]);
  let info = '<span>Нет данных</span>';
  if (frame) {
    const s = frame.s, n = s.length, X = (i) => i / (n - 1) * W;
    g.strokeStyle = '#ffb347'; g.lineWidth = 2; g.lineJoin = 'round'; g.beginPath();
    for (let i = 0; i < n; i++) i ? g.lineTo(X(i), Y(s[i])) : g.moveTo(X(i), Y(s[i]));
    g.stroke();
    g.fillStyle = frame.trig ? '#4cd964' : '#7a736c'; const tx = X(Math.floor(n / 5));
    g.beginPath(); g.moveTo(tx - 6, 0); g.lineTo(tx + 6, 0); g.lineTo(tx, 10); g.fill();
    let mn = 4095, mx = 0, sum = 0;
    for (const c of s) { mn = Math.min(mn, c); mx = Math.max(mx, c); sum += c; }
    const mid = (mn + mx) / 2, h = (mx - mn) / 8 + 1; let up = [], hi = s[0] > mid;
    for (let i = 1; i < n; i++) {
      if (!hi && s[i] > mid + h) { hi = true; up.push(i); } else if (hi && s[i] < mid - h) hi = false;
    }
    const T = frame.fs ? (up.length > 1 ? (up[up.length - 1] - up[0]) / (up.length - 1) / frame.fs : 0) : 0;
    const perDiv = frame.fs ? n / frame.fs / 10 : 0;
    info = `<span>Размах <b>${fmt(volts(mx - mn), 'В')}</b></span><span>Макс <b>${volts(mx).toFixed(2)} В</b></span>` +
      `<span>Мин <b>${volts(mn).toFixed(2)} В</b></span><span>Среднее <b>${volts(sum / n).toFixed(2)} В</b></span>` +
      `<span>Частота <b>${T ? fmt(1 / T, 'Гц') : '—'}</b></span><span>Fs <b>${(frame.fs / 1e3).toFixed(0)} кГц</b></span>` +
      `<span>${fmt(perDiv, 'с')}/дел · 0,41 В/дел</span><span>${frame.trig ? 'Синхронизация' : 'Нет синхронизации'}</span><span>${fps} кадр/с</span>`;
  }
  if ($('info').dataset.t !== info) { $('info').innerHTML = info; $('info').dataset.t = info; }
  requestAnimationFrame(draw);
}
setInterval(() => { const t = performance.now(); fps = Math.round(cnt * 1000 / (t - t0)); cnt = 0; t0 = t; }, 1000);

// ---------- элементы ----------
for (const id of ['rate', 'len', 'lvl', 'edge', 'mode']) $(id).addEventListener('input', send);
$('run').onclick = () => { running = !running; $('run').textContent = running ? 'Стоп' : 'Пуск'; send(); };
$('connect').onclick = async () => {
  if (dev) return disconnect();
  try { connect(await navigator.usb.requestDevice({ filters: [{ vendorId: USB_VID }] })); } catch (e) {}
};
if (!navigator.usb) $('nosupport').style.display = 'block';
else {
  navigator.usb.addEventListener('disconnect', (e) => { if (e.device === dev) disconnect(); });
  navigator.usb.getDevices().then((l) => { const d = l.find((x) => x.vendorId === USB_VID); if (d) connect(d); });
}
draw();
