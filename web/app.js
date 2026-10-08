// Страница для платы К1986ВЕ9х: связь по WebUSB и несколько «приколов» на её кнопках и светодиодах.
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
    uptimeMs: view.getUint32(5, true),
  };
}

function ledsCommand(mask) {
  return new Uint8Array([0x01, mask & 0x03]);
}

// ---------- азбука Морзе: текст -> список «горит / не горит» с длительностями ----------
const MORSE = {
  'A': '.-', 'B': '-...', 'C': '-.-.', 'D': '-..', 'E': '.', 'F': '..-.', 'G': '--.', 'H': '....', 'I': '..',
  'J': '.---', 'K': '-.-', 'L': '.-..', 'M': '--', 'N': '-.', 'O': '---', 'P': '.--.', 'Q': '--.-', 'R': '.-.',
  'S': '...', 'T': '-', 'U': '..-', 'V': '...-', 'W': '.--', 'X': '-..-', 'Y': '-.--', 'Z': '--..',
  'А': '.-', 'Б': '-...', 'В': '.--', 'Г': '--.', 'Д': '-..', 'Е': '.', 'Ё': '.', 'Ж': '...-', 'З': '--..',
  'И': '..', 'Й': '.---', 'К': '-.-', 'Л': '.-..', 'М': '--', 'Н': '-.', 'О': '---', 'П': '.--.', 'Р': '.-.',
  'С': '...', 'Т': '-', 'У': '..-', 'Ф': '..-.', 'Х': '....', 'Ц': '-.-.', 'Ч': '---.', 'Ш': '----',
  'Щ': '--.-', 'Ъ': '-..-', 'Ь': '-..-', 'Ы': '-.--', 'Э': '..-..', 'Ю': '..--', 'Я': '.-.-',
  '0': '-----', '1': '.----', '2': '..---', '3': '...--', '4': '....-', '5': '.....',
  '6': '-....', '7': '--...', '8': '---..', '9': '----.',
};

// Слова разделяются пробелом, буквы идут через `letterGap`; символы вне таблицы пропускаются
function morseWords(text) {
  return text.toUpperCase().split(/\s+/)
    .map((word) => [...word].map((ch) => MORSE[ch]).filter(Boolean))
    .filter((letters) => letters.length > 0);
}

function morseText(text) {
  return morseWords(text).map((letters) => letters.join(' ')).join('  /  ');
}

// Точка = 1 единица, тире = 3, пауза в букве = 1, между буквами = 3, между словами = 7
function morseSteps(text, unit = 150) {
  const steps = [];
  morseWords(text).forEach((letters, w) => {
    if (w > 0) steps.push({ on: false, ms: 7 * unit });
    letters.forEach((code, l) => {
      if (l > 0) steps.push({ on: false, ms: 3 * unit });
      [...code].forEach((sign, s) => {
        if (s > 0) steps.push({ on: false, ms: unit });
        steps.push({ on: true, ms: (sign === '.' ? 1 : 3) * unit });
      });
    });
  });
  return steps;
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

if (typeof module !== 'undefined') {
  module.exports = { parseFrame, ledsCommand, morseText, morseSteps, newGame, stepGame, MORSE };
}

// ---------- остальной код работает только в браузере ----------
if (typeof document !== 'undefined') (function () {
  const $ = (id) => document.getElementById(id);
  const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

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
    $('connect').textContent = on ? 'Отключить' : 'Подключить плату';
  }

  async function connect(dev) {
    device = dev;
    await device.open();
    if (device.configuration === null) await device.selectConfiguration(1);
    await device.claimInterface(0);
    sentLeds = -1;
    setStatus('подключено: ' + (device.productName || 'плата'), true);
    readLoop();
  }

  async function disconnect() {
    if (!device) return;
    const d = device;
    device = null;
    stopEffects();
    try { await d.close(); } catch (e) { /* уже отключена */ }
    setStatus('не подключено', false);
  }

  async function readLoop() {
    const dev = device;
    while (device === dev && dev) {
      try {
        const r = await dev.transferIn(EP_IN, 64);
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
  let rttMask = null, rttStart = 0;

  async function setLeds(mask) {
    wantedLeds = mask & 3;
    if (sending || !device) return;
    sending = true;
    try {
      while (device && sentLeds !== wantedLeds) {
        sentLeds = wantedLeds;
        rttMask = sentLeds;
        rttStart = performance.now();
        await device.transferOut(EP_OUT, ledsCommand(sentLeds));
      }
    } catch (e) {
      await disconnect();
    } finally {
      sending = false;
    }
  }

  // Короткая вспышка, если сейчас не идёт эффект
  async function pulse(mask, ms) {
    if (fxRunning) return;
    await setLeds(mask);
    await sleep(ms);
    if (!fxRunning) await setLeds(0);
  }

  // ===== разбор кадра: плата, показания, нажатия =====
  const baseline = { sum: 0, count: 0, value: 0 };
  let frames = 0, lastFpsTime = performance.now();
  const log = [];                                   // отсчёты АЦП для графика и CSV

  function onFrame(f) {
    for (let i = 0; i < 5; i++) $('k' + i).classList.toggle('down', !!(f.buttons & (1 << i)));

    boardLeds = f.leds;
    for (let i = 0; i < 2; i++) $('led' + i).classList.toggle('on', !!(f.leds & (1 << i)));
    if (rttMask !== null && f.leds === rttMask) {
      $('rtt').textContent = Math.round(performance.now() - rttStart) + ' мс';
      rttMask = null;
    }

    showAdc(f);

    const pressed = f.buttons & ~prevButtons;
    prevButtons = f.buttons;
    for (let i = 0; i < 5; i++) if (pressed & (1 << i)) onPress(i);

    frames++;
    const now = performance.now();
    if (now - lastFpsTime >= 1000) {
      $('fps').textContent = Math.round(frames * 1000 / (now - lastFpsTime));
      frames = 0; lastFpsTime = now;
    }
    const s = Math.floor(f.uptimeMs / 1000);
    $('uptime').textContent = Math.floor(s / 60) + ' мин ' + (s % 60) + ' с';
  }

  function showAdc(f) {
    if (baseline.count < 25) {                      // первые отсчёты считаем «комнатной» температурой
      baseline.sum += f.adc;
      baseline.value = baseline.sum / ++baseline.count;
    }
    const dev = Math.round(f.adc - baseline.value);
    $('adc').textContent = f.adc;
    $('dev').textContent = (dev > 0 ? '+' : dev < 0 ? '−' : '') + Math.abs(dev);
    $('heat').style.opacity = Math.min(1, Math.abs(dev) / 40);   // чип светится при отклонении

    log.push({ t: f.uptimeMs, adc: f.adc });
    if (log.length > 5000) log.shift();
  }

  // ===== вкладки =====
  const tabs = ['game', 'piano', 'lights', 'chart'];
  let activeTab = 'game';

  tabs.forEach((name) => {
    $('tab-' + name).onclick = () => {
      activeTab = name;
      tabs.forEach((t) => {
        $('tab-' + t).setAttribute('aria-selected', String(t === name));
        $('panel-' + t).hidden = t !== name;
      });
    };
  });

  // Нажатие кнопки платы: что оно значит, зависит от открытой вкладки
  function onPress(button) {
    if (activeTab === 'game') gameButton(button);
    if (activeTab === 'piano') playNote(button);
  }

  // ===== светодиоды на плате в SVG: клик включает и выключает =====
  function toggleLed(i) {
    stopEffects();
    setLeds(boardLeds ^ (1 << i));
  }
  [0, 1].forEach((i) => {
    const led = $('led' + i);
    led.onclick = () => toggleLed(i);
    led.onkeydown = (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); toggleLed(i); } };
  });

  // ===== змейка =====
  const GRID_W = 20, GRID_H = 14, CELL = 28, TICK_MS = 140;
  const canvasGame = $('game'), g2d = canvasGame.getContext('2d');
  let game = newGame(GRID_W, GRID_H);
  let gameState = 'idle';                           // idle, running, paused, over
  let wantedDir = null, gameTimer = null;
  let best = 0;
  try { best = Number(localStorage.getItem('snakeBest')) || 0; } catch (e) { /* без хранилища */ }
  $('best').textContent = best;

  function gameButton(button) {
    const dirs = { [BTN.UP]: 'up', [BTN.DOWN]: 'down', [BTN.LEFT]: 'left', [BTN.RIGHT]: 'right' };
    if (dirs[button]) wantedDir = dirs[button];
    if (button === BTN.SELECT) toggleGame();
  }

  function toggleGame() {
    if (gameState === 'running') {
      gameState = 'paused';
      clearInterval(gameTimer);
    } else {
      if (gameState === 'idle' || gameState === 'over') {
        game = newGame(GRID_W, GRID_H);
        wantedDir = null;
        $('score').textContent = 0;
      }
      gameState = 'running';
      gameTimer = setInterval(gameTick, TICK_MS);
    }
    drawGame();
  }

  function gameTick() {
    const result = stepGame(game, wantedDir);
    if (result === 'eat') {
      $('score').textContent = game.score;
      pulse(2, 120);                                // яблоко - VD4
    } else if (result === 'die') {
      gameState = 'over';
      clearInterval(gameTimer);
      if (game.score > best) {
        best = game.score;
        $('best').textContent = best;
        try { localStorage.setItem('snakeBest', String(best)); } catch (e) { /* без хранилища */ }
      }
      pulse(1, 500);                                // проигрыш - VD3
    }
    drawGame();
  }

  function drawGame() {
    g2d.fillStyle = '#0d2a23';
    g2d.fillRect(0, 0, canvasGame.width, canvasGame.height);
    g2d.fillStyle = '#1d4a3d';
    for (let y = 0; y < GRID_H; y++) for (let x = 0; x < GRID_W; x++) g2d.fillRect(x * CELL + 13, y * CELL + 13, 2, 2);

    g2d.fillStyle = '#ff7a5c';                      // яблоко
    g2d.beginPath();
    g2d.arc(game.food.x * CELL + CELL / 2, game.food.y * CELL + CELL / 2, CELL / 2 - 5, 0, 7);
    g2d.fill();

    game.body.forEach((p, i) => {                   // змейка: голова ярче хвоста
      g2d.fillStyle = i === 0 ? '#f2b83b' : '#c48f24';
      g2d.fillRect(p.x * CELL + 2, p.y * CELL + 2, CELL - 4, CELL - 4);
    });

    const messages = { idle: 'SELECT или щелчок - начать', paused: 'Пауза', over: 'Игра окончена. SELECT - ещё раз' };
    if (messages[gameState]) {
      g2d.fillStyle = 'rgba(11, 38, 32, .75)';
      g2d.fillRect(0, canvasGame.height / 2 - 30, canvasGame.width, 60);
      g2d.fillStyle = '#e8efe4';
      g2d.font = '22px Bahnschrift, "Segoe UI", sans-serif';
      g2d.textAlign = 'center';
      g2d.fillText(messages[gameState], canvasGame.width / 2, canvasGame.height / 2 + 8);
    }
  }

  canvasGame.onclick = toggleGame;                  // без платы тоже можно играть: щелчок и стрелки
  document.addEventListener('keydown', (e) => {
    const arrows = { ArrowUp: 'up', ArrowDown: 'down', ArrowLeft: 'left', ArrowRight: 'right' };
    if (activeTab === 'game' && arrows[e.key]) { wantedDir = arrows[e.key]; e.preventDefault(); }
  });
  drawGame();

  // ===== пианино =====
  const NOTES = [                                   // порядок на экране: LEFT, DOWN, SELECT, UP, RIGHT
    { button: BTN.LEFT,   name: 'До',   hz: 261.63 },
    { button: BTN.DOWN,   name: 'Ре',   hz: 293.66 },
    { button: BTN.SELECT, name: 'Ми',   hz: 329.63 },
    { button: BTN.UP,     name: 'Соль', hz: 392.0 },
    { button: BTN.RIGHT,  name: 'Ля',   hz: 440.0 },
  ];
  let audio = null;

  NOTES.forEach((n, i) => {
    const key = document.createElement('span');
    key.id = 'note' + n.button;
    key.innerHTML = n.name + '<small>' + Object.keys(BTN)[n.button] + '</small>';
    key.onclick = () => playNote(n.button);         // можно играть и мышкой
    $('notes').appendChild(key);
  });

  function playNote(button) {
    const note = NOTES.find((n) => n.button === button);
    const key = $('note' + button);
    key.classList.add('down');
    setTimeout(() => key.classList.remove('down'), 200);
    pulse(NOTES.indexOf(note) % 2 === 0 ? 1 : 2, 150);        // светодиоды мигают в такт

    if (!$('sound').checked) return;
    audio = audio || new (window.AudioContext || window.webkitAudioContext)();
    const osc = audio.createOscillator(), gain = audio.createGain();
    osc.type = 'triangle';
    osc.frequency.value = note.hz;
    gain.gain.setValueAtTime(0.25, audio.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.0001, audio.currentTime + 0.5);
    osc.connect(gain).connect(audio.destination);
    osc.start();
    osc.stop(audio.currentTime + 0.5);
  }

  // ===== световые эффекты и азбука Морзе =====
  let fxId = 0, fxRunning = false;

  function stopEffects() {
    fxId++;
    fxRunning = false;
  }

  // Повторять кадры (маски светодиодов) с заданным шагом, пока эффект не остановят
  async function loopEffect(masks, stepMs) {
    const my = ++fxId;
    fxRunning = true;
    for (let i = 0; fxId === my && device; i++) {
      await setLeds(masks[i % masks.length]);
      await sleep(stepMs);
    }
  }

  async function playMorse() {
    const text = $('morse-text').value;
    $('morse-code').textContent = morseText(text) || 'В тексте нет букв и цифр для азбуки Морзе';
    const my = ++fxId;
    fxRunning = true;
    for (const step of morseSteps(text)) {
      if (fxId !== my || !device) return;
      await setLeds(step.on ? 1 : 0);
      await sleep(step.ms);
    }
    if (fxId === my) { fxRunning = false; setLeds(0); }
  }

  $('fx-run').onclick    = () => loopEffect([1, 2], 120);
  $('fx-blink').onclick  = () => loopEffect([3, 0], 300);
  $('fx-beacon').onclick = () => loopEffect([3, 0, 3, 0, 0, 0, 0, 0, 0, 0], 100);
  $('fx-stop').onclick   = () => { stopEffects(); setLeds(0); };
  $('morse-send').onclick = playMorse;

  // ===== график АЦП =====
  const HISTORY = 500;                              // отсчётов на графике: около 10 секунд
  const canvas = $('chart'), ctx = canvas.getContext('2d');

  function drawChart() {
    const w = canvas.width, h = canvas.height;
    ctx.clearRect(0, 0, w, h);
    ctx.strokeStyle = '#1d4a3d'; ctx.lineWidth = 1;
    for (let i = 1; i < 4; i++) { ctx.beginPath(); ctx.moveTo(0, h * i / 4); ctx.lineTo(w, h * i / 4); ctx.stroke(); }

    const shown = log.slice(-HISTORY).map((p) => p.adc);
    if (shown.length > 1) {
      let min = Math.min(...shown), max = Math.max(...shown);
      $('amin').textContent = min;
      $('amax').textContent = max;
      if (max - min < 8) { const c = (max + min) / 2; min = c - 4; max = c + 4; }   // не растягивать шум на весь график
      ctx.fillStyle = '#8fb0a3'; ctx.font = '12px Bahnschrift, system-ui';
      ctx.fillText(Math.round(max), 6, 14); ctx.fillText(Math.round(min), 6, h - 6);
      ctx.strokeStyle = '#f2b83b'; ctx.lineWidth = 2; ctx.beginPath();
      shown.forEach((v, i) => {
        const x = i * w / (HISTORY - 1), y = h - 20 - (v - min) / (max - min) * (h - 40);
        i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      });
      ctx.stroke();
    }
    requestAnimationFrame(drawChart);
  }
  drawChart();

  $('csv').onclick = () => {
    const text = 'мс с запуска платы;код АЦП\n' + log.map((p) => p.t + ';' + p.adc).join('\n');
    const link = document.createElement('a');
    link.href = URL.createObjectURL(new Blob(['﻿' + text], { type: 'text/csv' }));
    link.download = 'adc.csv';
    link.click();
    URL.revokeObjectURL(link.href);
  };

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
