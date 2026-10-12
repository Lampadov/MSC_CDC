// Страница для платы К1986ВЕ9х: светодиоды, кнопки, датчик температуры и змейка по WebUSB.
//
// Формат кадров (little-endian), см. main.c в прошивке:
//   плата -> страница  [0x01][кнопки][светодиоды][АЦП: 2 байта][мс с запуска: 4 байта]  (9 байт)
//   страница -> плата  [0x01][светодиоды]                                               (2 байта)
// Биты кнопок: 0 SELECT, 1 UP, 2 DOWN, 3 LEFT, 4 RIGHT. Биты светодиодов: 0 VD3, 1 VD4.

const USB_VID    = 0xCAFE;   // должен совпадать с USB_VID в usb_descriptors.c
const EP_OUT     = 1;        // EP1 OUT: команды
const EP_IN      = 2;        // EP2 IN:  кадры состояния
const FRAME_SIZE = 9;
const BTN        = { SELECT: 0, UP: 1, DOWN: 2, LEFT: 3, RIGHT: 4 };

function parseFrame(view) {
  if (view.byteLength < FRAME_SIZE || view.getUint8(0) !== 0x01) return null;
  return {
    buttons: view.getUint8(1),
    leds: view.getUint8(2),
    adc: view.getUint16(3, true),
    ms: view.getUint32(5, true),        // время работы платы по SysTick, мс
  };
}

// Время работы платы: "ЧЧ:ММ:СС", с суточным счётчиком после 24 часов
function formatUptime(ms) {
  const t = Math.floor(ms / 1000), d = Math.floor(t / 86400);
  const p = (n) => String(n).padStart(2, '0');
  const clock = p(Math.floor(t / 3600) % 24) + ':' + p(Math.floor(t / 60) % 60) + ':' + p(t % 60);
  return d > 0 ? d + ' д ' + clock : clock;
}

function ledsCommand(mask) {
  return new Uint8Array([0x01, mask & 0x03]);
}

// ---------- змейка: чистая логика без рисования ----------
const DIRS     = { up: [0, -1], down: [0, 1], left: [-1, 0], right: [1, 0] };
const OPPOSITE = { up: 'down', down: 'up', left: 'right', right: 'left' };

function newGame(w, h, rand = Math.random) {
  const y = h >> 1;
  const game = { w, h, rand, body: [{ x: 4, y }, { x: 3, y }, { x: 2, y }], dir: 'right', score: 0, food: null };
  game.food = placeFood(game);
  return game;
}

function placeFood(game) {
  const free = [];
  for (let y = 0; y < game.h; y++) {
    for (let x = 0; x < game.w; x++) {
      if (!game.body.some((p) => p.x === x && p.y === y)) free.push({ x, y });
    }
  }
  return free[Math.floor(game.rand() * free.length)];
}

// Один ход. Возвращает 'eat' (съели яблоко), 'die' (врезались) или null
function stepGame(game, wantedDir) {
  if (wantedDir && wantedDir !== OPPOSITE[game.dir]) game.dir = wantedDir;

  const [dx, dy] = DIRS[game.dir];
  const next = { x: game.body[0].x + dx, y: game.body[0].y + dy };
  const eats = next.x === game.food.x && next.y === game.food.y;

  const hitsWall = next.x < 0 || next.y < 0 || next.x >= game.w || next.y >= game.h;
  const body = eats ? game.body : game.body.slice(0, -1);       // хвост освободит клетку, если не растём
  if (hitsWall || body.some((p) => p.x === next.x && p.y === next.y)) return 'die';

  game.body.unshift(next);
  if (!eats) { game.body.pop(); return null; }
  game.score++;
  game.food = placeFood(game);
  return 'eat';
}

// Сглаживание отсчётов АЦП: экспоненциальное среднее, чем меньше alpha, тем плавнее линия
function smooth(previous, sample, alpha = 0.06) {
  return previous === null ? sample : previous + (sample - previous) * alpha;
}

if (typeof module !== 'undefined') module.exports = { formatUptime, parseFrame, ledsCommand, newGame, stepGame, smooth };

// ---------- остальной код работает только в браузере ----------
if (typeof document !== 'undefined') (function () {
  const $ = (id) => document.getElementById(id);
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
  const ORANGE = '#f08300', ORANGE_LIGHT = '#f6b160', TEXT = '#2a2623';

  if (!navigator.usb) {
    $('nosupport').style.display = 'block';
    $('connect').disabled = true;
    return;
  }

  // ===== подключение и обмен =====
  let device = null;
  let boardLeds = 0;            // светодиоды, как их показывает плата
  let prevButtons = 0;          // кнопки в прошлом кадре: по разнице находим нажатия

  function setStatus(text, on) {
    $('statusText').textContent = text;
    $('status').classList.toggle('on', on);
    if (!on) $('uptime').textContent = '--:--:--';
    $('connect').textContent = on ? 'Отключить' : 'Подключить плату';
  }

  async function connect(dev) {
    device = dev;
    await device.open();
    if (device.configuration === null) await device.selectConfiguration(1);
    await device.claimInterface(0);
    sentLeds = -1;
    setStatus('подключено', true);
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
        if (device !== dev) return;                 // пока ждали кадр, плату отключили
        if (r.status === 'ok') {
          const frame = parseFrame(r.data);
          if (frame) onFrame(frame);
        } else if (r.status === 'stall') {
          await dev.clearHalt('in', EP_IN);
        }
      } catch (e) {
        if (device === dev) await disconnect();
        return;
      }
    }
  }

  // Светодиоды: отправляем последнее пожелание, промежуточные при частой смене пропускаем
  let wantedLeds = 0, sentLeds = -1, sending = false;

  async function setLeds(mask) {
    wantedLeds = mask & 3;
    if (sending || !device) return;
    sending = true;
    try {
      while (device && sentLeds !== wantedLeds) {
        sentLeds = wantedLeds;
        await device.transferOut(EP_OUT, ledsCommand(sentLeds));
      }
    } catch (e) {
      await disconnect();
    } finally {
      sending = false;
    }
  }

  // Короткая вспышка светодиода; те, что были включены, остаются включёнными
  async function flash(mask, ms) {
    setLeds(boardLeds | mask);
    await sleep(ms);
    setLeds(boardLeds & ~mask);
  }

  // ===== кадр от платы =====
  const history = [];                               // сглаженные отсчёты АЦП для графика
  let averaged = null;
  const HISTORY = 500;                              // около 10 секунд

  function onFrame(f) {
    $('uptime').textContent = formatUptime(f.ms);
    for (let i = 0; i < 5; i++) $('k' + i).classList.toggle('down', !!(f.buttons & (1 << i)));

    boardLeds = f.leds;
    for (let i = 0; i < 2; i++) $('led' + i).setAttribute('aria-pressed', String(!!(f.leds & (1 << i))));

    averaged = smooth(averaged, f.adc);
    $('adc').textContent = Math.round(averaged);
    history.push(averaged);
    if (history.length > HISTORY) history.shift();

    const pressed = f.buttons & ~prevButtons;       // кнопки, нажатые с прошлого кадра
    prevButtons = f.buttons;
    for (let i = 0; i < 5; i++) if (pressed & (1 << i)) gameButton(i);
  }

  [0, 1].forEach((i) => { $('led' + i).onclick = () => setLeds(boardLeds ^ (1 << i)); });

  // ===== график температуры =====
  const chart = $('chart'), c2d = chart.getContext('2d');

  function drawChart() {
    const w = chart.width, h = chart.height;
    c2d.clearRect(0, 0, w, h);
    if (history.length > 1) {
      let min = Math.min(...history), max = Math.max(...history);
      if (max - min < 20) { const c = (max + min) / 2; min = c - 10; max = c + 10; }   // не растягивать шум на весь график
      c2d.strokeStyle = ORANGE; c2d.lineWidth = 3; c2d.lineJoin = 'round'; c2d.beginPath();
      const px = (i) => i * w / (HISTORY - 1);
      const py = (v) => h - 8 - (v - min) / (max - min) * (h - 16);
      c2d.moveTo(px(0), py(history[0]));
      for (let i = 1; i < history.length - 1; i++) {      // кривая через середины отрезков: без изломов
        c2d.quadraticCurveTo(px(i), py(history[i]), (px(i) + px(i + 1)) / 2, (py(history[i]) + py(history[i + 1])) / 2);
      }
      c2d.lineTo(px(history.length - 1), py(history[history.length - 1]));
      c2d.stroke();
    }
    requestAnimationFrame(drawChart);
  }
  drawChart();

  // ===== змейка =====
  const GRID_W = 20, GRID_H = 14, CELL = 28, TICK_MS = 140;
  const canvas = $('game'), g2d = canvas.getContext('2d');
  let game = newGame(GRID_W, GRID_H);
  let state = 'idle';                               // idle, running, paused, over
  let wantedDir = null, timer = null, best = 0;
  try { best = Number(localStorage.getItem('snakeBest')) || 0; } catch (e) { /* без хранилища */ }
  $('best').textContent = best;

  // Кнопки платы и стрелки клавиатуры управляют одним и тем же
  function gameButton(button) {
    const dirs = { [BTN.UP]: 'up', [BTN.DOWN]: 'down', [BTN.LEFT]: 'left', [BTN.RIGHT]: 'right' };
    if (dirs[button]) wantedDir = dirs[button];
    if (button === BTN.SELECT) toggleGame();
  }

  function toggleGame() {
    if (state === 'running') {
      state = 'paused';
      clearInterval(timer);
    } else {
      if (state === 'idle' || state === 'over') {
        game = newGame(GRID_W, GRID_H);
        wantedDir = null;
        $('score').textContent = 0;
      }
      state = 'running';
      timer = setInterval(tick, TICK_MS);
    }
    draw();
  }

  function tick() {
    const result = stepGame(game, wantedDir);
    if (result === 'eat') {
      $('score').textContent = game.score;
      flash(2, 120);                                // яблоко: VD4
    } else if (result === 'die') {
      state = 'over';
      clearInterval(timer);
      if (game.score > best) {
        best = game.score;
        $('best').textContent = best;
        try { localStorage.setItem('snakeBest', String(best)); } catch (e) { /* без хранилища */ }
      }
      flash(1, 500);                                // проигрыш: VD3
    }
    draw();
  }

  function draw() {
    g2d.fillStyle = '#fff';
    g2d.fillRect(0, 0, canvas.width, canvas.height);

    g2d.fillStyle = TEXT;                           // яблоко
    g2d.beginPath();
    g2d.arc(game.food.x * CELL + CELL / 2, game.food.y * CELL + CELL / 2, CELL / 2 - 6, 0, 7);
    g2d.fill();

    game.body.forEach((p, i) => {                   // змейка: голова темнее хвоста
      g2d.fillStyle = i === 0 ? ORANGE : ORANGE_LIGHT;
      g2d.fillRect(p.x * CELL + 2, p.y * CELL + 2, CELL - 4, CELL - 4);
    });

    const messages = { idle: 'SELECT или щелчок: старт', paused: 'Пауза', over: 'Игра окончена. SELECT: ещё раз' };
    if (messages[state]) {
      g2d.fillStyle = 'rgba(255, 255, 255, .85)';
      g2d.fillRect(0, canvas.height / 2 - 28, canvas.width, 56);
      g2d.fillStyle = TEXT;
      g2d.font = '600 20px "Segoe UI", system-ui, sans-serif';
      g2d.textAlign = 'center';
      g2d.fillText(messages[state], canvas.width / 2, canvas.height / 2 + 7);
    }
  }

  canvas.onclick = toggleGame;                      // без платы тоже можно играть: щелчок и стрелки
  document.addEventListener('keydown', (e) => {
    const arrows = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right' };
    if (arrows[e.key]) { wantedDir = arrows[e.key]; e.preventDefault(); }
  });
  draw();

  // ===== подключение и отключение =====
  $('connect').onclick = async () => {
    if (device) return disconnect();
    try {
      connect(await navigator.usb.requestDevice({ filters: [{ vendorId: USB_VID }] }));
    } catch (e) { /* окно выбора закрыли */ }
  };

  navigator.usb.addEventListener('disconnect', (e) => { if (e.device === device) disconnect(); });
  navigator.usb.addEventListener('connect', (e) => { if (!device && e.device.vendorId === USB_VID) connect(e.device); });
  navigator.usb.getDevices().then((list) => {        // плата уже разрешена: подключаемся сразу
    const d = list.find((x) => x.vendorId === USB_VID);
    if (d && !device) connect(d);
  });
})();
