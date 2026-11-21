#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <functional>
#include "AppConfig.h"
#include "SandisolarRegisters.h"
#include "SandisolarModbus.h"
#include "AutomationEngine.h"
#include "TelegramBotManager.h"
#include "OledDisplayManager.h"
#include "DebugLogger.h"
#include "RemoteConsoleManager.h"

const char LITE_TTY_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html lang="uk"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SandiSolar S3 — Web TTY</title><style>body{background:#090d16;color:#10b981;font-family:monospace;margin:10px}input,button{background:#1e293b;color:#f8fafc;border:1px solid #334155;padding:6px 10px;border-radius:4px;font-family:monospace}pre{background:#000;color:#a7f3d0;padding:10px;border:1px solid #1e293b;border-radius:6px;height:62vh;overflow:auto;white-space:pre-wrap;font-size:12px}</style></head><body><div><b>⚡ SandiSolar ESP32-S3 Web TTY</b> | <a href="/" style="color:#38bdf8">Повний Дашборд</a> | Порти: TCP <code>2323</code> / UDP <code>4444</code></div><div style="margin:8px 0;display:flex;gap:6px;flex-wrap:wrap"><button onclick="run('status')">📊 Статус</button><button onclick="run('log 40')">📋 Лог</button><button onclick="run('swap')">🔄 Swap RX/TX (17↔18)</button><button onclick="run('trace on')">🟢 Trace ON</button><button onclick="run('trace off')">⚪ Trace OFF</button><button onclick="run('help')">❓ Help</button></div><form onsubmit="ev(event)" style="display:flex;gap:6px;margin-bottom:8px"><input id="c" style="flex:1" placeholder="Введіть команду (status, log, swap, mb 3 0 1, set rx_pin 18...)" autofocus><button type="submit">Виконати</button></form><pre id="o">Завантаження...</pre><form method="POST" action="/update" enctype="multipart/form-data" style="margin-top:8px">OTA (.bin): <input type="file" name="update" accept=".bin"><button type="submit">🚀 Прошити OTA</button></form><script>async function run(c){const o=document.getElementById('o');o.textContent+='\n> '+c+'\n';const r=await fetch('/api/cli?cmd='+encodeURIComponent(c));o.textContent+=await r.text();o.scrollTop=o.scrollHeight}function ev(e){e.preventDefault();const i=document.getElementById('c');if(i.value){run(i.value);i.value=''}}run('status');</script></body></html>)rawliteral";

const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SandiSolar 6.5 Pro — ESP32-S3 Standalone Controller</title>
<link rel="icon" href="data:image/svg+xml,<svg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 100 100%22><text y=%22.9em%22 font-size=%2290%22>⚡</text></svg>">
<style>
:root {
  --bg: #0b0f19;
  --card: rgba(17, 24, 39, 0.9);
  --card-inner: rgba(30, 41, 59, 0.55);
  --border: rgba(255, 255, 255, 0.09);
  --text: #f3f4f6;
  --muted: #94a3b8;
  --accent: #38bdf8;
  --green: #10b981;
  --yellow: #f59e0b;
  --red: #ef4444;
  --purple: #a855f7;
}
* { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
body { background: var(--bg); color: var(--text); padding: 14px; min-height: 100vh; }
.container { max-width: 1080px; margin: 0 auto; display: flex; flex-direction: column; gap: 14px; }
.header { background: var(--card); border: 1px solid var(--border); border-radius: 16px; padding: 16px 20px; display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 12px; box-shadow: 0 8px 24px rgba(0,0,0,0.45); }
.title-wrap h1 { font-size: 1.25rem; color: var(--accent); display: flex; align-items: center; gap: 8px; }
.title-wrap p { font-size: 0.78rem; color: var(--muted); margin-top: 2px; }
.badges { display: flex; gap: 8px; flex-wrap: wrap; align-items: center; }
.badge { padding: 4px 10px; border-radius: 999px; font-size: 0.73rem; font-weight: 700; text-transform: uppercase; display: inline-flex; align-items: center; gap: 5px; }
.b-green { background: rgba(16,185,129,0.18); color: #34d399; border: 1px solid #10b981; }
.b-red { background: rgba(239,68,68,0.18); color: #f87171; border: 1px solid #ef4444; }
.b-yellow { background: rgba(245,158,11,0.18); color: #fbbf24; border: 1px solid #f59e0b; }
.b-blue { background: rgba(56,189,248,0.18); color: #38bdf8; border: 1px solid #0284c7; }
.b-purple { background: rgba(168,85,247,0.18); color: #c084fc; border: 1px solid #9333ea; }

.tabs { display: flex; gap: 6px; overflow-x: auto; padding-bottom: 4px; }
.tab-btn { background: var(--card); border: 1px solid var(--border); color: var(--muted); padding: 9px 14px; border-radius: 10px; cursor: pointer; font-weight: 600; font-size: 0.82rem; white-space: nowrap; transition: 0.2s; }
.tab-btn:hover { color: var(--text); border-color: rgba(56,189,248,0.4); }
.tab-btn.active { background: linear-gradient(135deg, #0284c7, #2563eb); color: #fff; border-color: #38bdf8; }
.tab-pane { display: none; flex-direction: column; gap: 14px; }
.tab-pane.active { display: flex; }

.card { background: var(--card); border: 1px solid var(--border); border-radius: 16px; padding: 18px; box-shadow: 0 8px 24px rgba(0,0,0,0.35); }
.card h2 { font-size: 0.95rem; color: var(--accent); margin-bottom: 14px; padding-bottom: 8px; border-bottom: 1px solid var(--border); display: flex; justify-content: space-between; align-items: center; }

/* Energy Flow SVG Diagram */
.flow-wrap { position: relative; width: 100%; background: rgba(15, 23, 42, 0.7); border: 1px solid var(--border); border-radius: 14px; padding: 14px; }
.flow-grid { display: grid; grid-template-columns: 1fr 1.2fr 1fr; gap: 12px; align-items: center; position: relative; z-index: 2; }
.flow-col { display: flex; flex-direction: column; gap: 14px; }
.flow-node { background: rgba(17, 24, 39, 0.92); border: 1px solid rgba(255,255,255,0.12); border-radius: 12px; padding: 12px; text-align: center; transition: 0.3s; }
.flow-node.active-solar { border-color: #fbbf24; box-shadow: 0 0 14px rgba(251,191,36,0.2); }
.flow-node.active-grid { border-color: #38bdf8; box-shadow: 0 0 14px rgba(56,189,248,0.2); }
.flow-node.active-bat { border-color: #10b981; box-shadow: 0 0 14px rgba(16,185,129,0.2); }
.flow-node.active-load { border-color: #c084fc; box-shadow: 0 0 14px rgba(192,132,252,0.2); }
.fn-title { font-size: 0.72rem; color: var(--muted); text-transform: uppercase; }
.fn-val { font-size: 1.25rem; font-weight: 800; margin: 4px 0; }
.fn-sub { font-size: 0.75rem; color: var(--muted); }

/* Stat Cards Grid */
.grid-6 { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 12px; }
.stat-box { background: var(--card-inner); border: 1px solid var(--border); border-radius: 12px; padding: 14px; display: flex; flex-direction: column; justify-content: space-between; min-height: 118px; }
.st-title { font-size: 0.73rem; color: var(--muted); text-transform: uppercase; letter-spacing: 0.4px; }
.st-val { font-size: 1.55rem; font-weight: 800; margin: 6px 0; color: #f8fafc; }
.st-sub { font-size: 0.76rem; color: var(--muted); }
.progress-bar { width: 100%; height: 8px; background: rgba(255,255,255,0.1); border-radius: 4px; overflow: hidden; margin-top: 6px; }
.progress-fill { height: 100%; background: linear-gradient(90deg, #10b981, #38bdf8); width: 0%; transition: width 0.4s; }

/* Buttons & Inputs */
.btn-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(145px, 1fr)); gap: 10px; }
.btn { display: inline-flex; align-items: center; justify-content: center; gap: 6px; padding: 10px 14px; border-radius: 10px; font-weight: 600; font-size: 0.83rem; cursor: pointer; border: none; color: #fff; transition: 0.15s; text-decoration: none; }
.btn:hover { opacity: 0.92; transform: translateY(-1px); }
.btn-green { background: linear-gradient(135deg, #059669, #10b981); }
.btn-red { background: linear-gradient(135deg, #dc2626, #ef4444); }
.btn-yellow { background: linear-gradient(135deg, #d97706, #f59e0b); }
.btn-blue { background: linear-gradient(135deg, #0284c7, #2563eb); }
.btn-gray { background: rgba(51, 65, 85, 0.85); border: 1px solid var(--border); }

.param-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 12px; }
.param-item { background: var(--card-inner); border: 1px solid var(--border); border-radius: 10px; padding: 12px; display: flex; flex-direction: column; gap: 8px; }
.param-head { display: flex; justify-content: space-between; align-items: center; font-size: 0.82rem; font-weight: 600; }
.param-cur { font-size: 0.78rem; color: var(--accent); font-family: ui-monospace, monospace; }
.param-ctrl { display: flex; gap: 8px; align-items: center; }
input, select { width: 100%; padding: 8px 11px; background: rgba(15, 23, 42, 0.9); border: 1px solid rgba(255,255,255,0.14); border-radius: 8px; color: #fff; font-size: 0.85rem; }
input:focus, select:focus { outline: none; border-color: var(--accent); }

.console { background: #030712; border: 1px solid var(--border); border-radius: 10px; padding: 12px; font-family: ui-monospace, Menlo, Consolas, monospace; font-size: 0.76rem; color: #a5f3fc; height: 240px; overflow-y: auto; line-height: 1.45; }
.log-row { border-bottom: 1px solid rgba(255,255,255,0.04); padding: 2px 0; }
.log-ts { color: #64748b; margin-right: 6px; }
.log-tx { color: #38bdf8; }
.log-rx { color: #4ade80; }
.log-warn { color: #fbbf24; }
.log-err { color: #f87171; }
.toast { position: fixed; bottom: 20px; right: 20px; background: #1e293b; border: 1px solid var(--accent); color: #fff; padding: 12px 18px; border-radius: 10px; font-size: 0.85rem; font-weight: 600; box-shadow: 0 8px 20px rgba(0,0,0,0.6); display: none; z-index: 999; }
</style>
</head>
<body>
<div class="container">
  <!-- Header -->
  <div class="header">
    <div class="title-wrap">
      <h1>⚡ SandiSolar 6.5 Pro — ESP32-S3</h1>
      <p>Автономний RS-485 Modbus RTU контролер (XY-S485 + 1.3" OLED + Telegram + Tailscale)</p>
    </div>
    <div class="badges">
      <span id="hdr-rs485" class="badge b-yellow">RS-485: ...</span>
      <span id="hdr-mode" class="badge b-blue">Mode: ...</span>
      <span id="hdr-vpn" class="badge b-purple">VPN: ...</span>
      <span id="hdr-wifi" class="badge b-green">Wi-Fi: ...</span>
    </div>
  </div>

  <!-- Navigation Tabs -->
  <div class="tabs">
    <button class="tab-btn active" onclick="openTab('tab-main', this)">📊 Головна</button>
    <button class="tab-btn" onclick="openTab('tab-battery', this)">🔋 Батарея та BMS</button>
    <button class="tab-btn" onclick="openTab('tab-modes', this)">⚡ Режими та SOC</button>
    <button class="tab-btn" onclick="openTab('tab-switches', this)">⚙️ Перемикачі та Ген.</button>
    <button class="tab-btn" onclick="openTab('tab-diag', this)">🔍 Діагностика RS-485</button>
    <button class="tab-btn" onclick="openTab('tab-settings', this)">🛠️ Налаштування та OTA</button>
  </div>

  <!-- TAB 1: OVERVIEW & ENERGY FLOW -->
  <div id="tab-main" class="tab-pane active">
    <div class="card">
      <h2>
        <span>🔌 Енергетичні потоки в реальному часі</span>
        <span id="eta-banner" style="font-size:0.78rem; color:#34d399; font-weight:600;">⏳ ETA: —</span>
      </h2>
      <div class="flow-wrap">
        <div class="flow-grid">
          <div class="flow-col">
            <div class="flow-node" id="fn-solar">
              <div class="fn-title">☀️ Сонячне поле PV1</div>
              <div class="fn-val" id="fl-pv-w">0 Вт</div>
              <div class="fn-sub" id="fl-pv-va">0.0 В / 0.0 А</div>
            </div>
            <div class="flow-node" id="fn-grid">
              <div class="fn-title">🗼 Міська мережа</div>
              <div class="fn-val" id="fl-grid-v">0.0 В</div>
              <div class="fn-sub" id="fl-grid-w">0 Вт (0.00 Гц)</div>
            </div>
          </div>
          <div class="flow-col">
            <div class="flow-node" id="fn-inv" style="border-color:var(--accent);">
              <div class="fn-title">⚡ SandiSolar 6.5 Pro</div>
              <div class="fn-val" id="fl-inv-mode">--</div>
              <div class="fn-sub" id="fl-inv-temp">T1: --°C | T2: --°C | T3: --°C</div>
              <div style="margin-top:6px;"><span id="fl-inv-sw" class="badge b-blue">Switch: --</span></div>
            </div>
          </div>
          <div class="flow-col">
            <div class="flow-node" id="fn-bat">
              <div class="fn-title">🔋 Акумулятор LiFePO4</div>
              <div class="fn-val" id="fl-bat-soc">-- %</div>
              <div class="fn-sub" id="fl-bat-va">0.0 В | 0.0 А (0 Вт)</div>
            </div>
            <div class="flow-node" id="fn-load">
              <div class="fn-title">🏠 Споживання дому</div>
              <div class="fn-val" id="fl-load-w">0 Вт</div>
              <div class="fn-sub" id="fl-load-va">0 ВА | cos φ: 1.00</div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- 6 Status Cards -->
    <div class="grid-6">
      <div class="stat-box">
        <div>
          <div class="st-title">Мережа 220В (Reg 42)</div>
          <div class="st-val" id="st-grid-v">-- V</div>
          <div class="st-sub" id="st-grid-sub">-- A | -- Hz</div>
        </div>
        <div style="margin-top:6px;"><span id="st-grid-badge" class="badge b-yellow">Опитування...</span></div>
      </div>

      <div class="stat-box">
        <div>
          <div class="st-title">Акумулятор (Reg 128)</div>
          <div class="st-val" id="st-soc">-- %</div>
          <div class="st-sub" id="st-bat-sub">-- V | -- A</div>
        </div>
        <div class="progress-bar"><div id="st-soc-bar" class="progress-fill"></div></div>
      </div>

      <div class="stat-box">
        <div>
          <div class="st-title">Power Switch (Reg 0)</div>
          <div class="st-val" id="st-sw">--</div>
          <div class="st-sub" id="st-mode-sub">Режим: --</div>
        </div>
        <div style="margin-top:6px;"><span id="st-sw-badge" class="badge b-blue">--</span></div>
      </div>

      <div class="stat-box">
        <div>
          <div class="st-title">Автоматика SoftOFF</div>
          <div class="st-val" id="st-eval-cd">-- с</div>
          <div class="st-sub" id="st-auto-thresholds">OFF ≥ 95% | Заряд &lt; 75%</div>
        </div>
        <div style="margin-top:6px;"><span id="st-auto-badge" class="badge b-green">Активна</span></div>
      </div>

      <div class="stat-box">
        <div>
          <div class="st-title">Температури (T1–T3)</div>
          <div class="st-sub" style="margin-top:6px; line-height:1.5;">
            T1 Радіатор: <b id="st-t1" style="color:#fff;">-- °C</b><br>
            T2 Boost/IPM: <b id="st-t2" style="color:#fff;">-- °C</b><br>
            T3 LLC тр-р: <b id="st-t3" style="color:#fff;">-- °C</b>
          </div>
        </div>
        <div style="margin-top:4px;"><span id="st-fan-badge" class="badge b-blue">Fan: 0 RPM</span></div>
      </div>

      <div class="stat-box">
        <div>
          <div class="st-title">Температура АКБ (T4 BMS)</div>
          <div class="st-val" id="st-t4">-- °C</div>
          <div class="st-sub" id="st-bms-lim">Ліміти BMS: +--A / ---A</div>
        </div>
        <div style="margin-top:6px;"><span id="st-t4-badge" class="badge b-green">Захист LiFePO4 OK</span></div>
      </div>
    </div>

    <!-- Quick Action Buttons -->
    <div class="card">
      <h2>🕹️ Швидке керування інвертором та автоматикою</h2>
      <p id="last-action-line" style="font-size:0.82rem; color:#cbd5e1; margin-bottom:12px;">Остання дія: --</p>
      <div class="btn-grid">
        <button class="btn btn-green" onclick="sendCmd('on')">⚡ Увімкнути (SoftON)</button>
        <button class="btn btn-red" onclick="sendCmd('off')">🛑 Вимкнути (SoftOFF)</button>
        <button class="btn btn-yellow" onclick="sendCmd('pause')">⏸️ Пауза автоматики</button>
        <button class="btn btn-blue" onclick="sendCmd('resume')">▶️ Старт автоматики</button>
        <button class="btn btn-gray" onclick="sendCmd('poll')">🔄 Опитати RS-485</button>
        <button class="btn btn-gray" onclick="sendCmd('eval')">⚖️ Перевірити правила</button>
        <button class="btn btn-gray" onclick="sendCmd('test_tg')">💬 Тест Telegram</button>
      </div>
    </div>

    <!-- Automation Event Log -->
    <div class="card">
      <h2>📜 Журнал подій автоматики (SoftOFF / SoftON / Сповіщення)</h2>
      <div class="console" id="auto-console"></div>
    </div>
  </div>

  <!-- TAB 2: BATTERY & BMS -->
  <div id="tab-battery" class="tab-pane">
    <div class="card">
      <h2>🔋 Параметри заряду та захисту акумулятора (Holding Регістри)</h2>
      <div class="param-grid">
        <div class="param-item">
          <div class="param-head"><span>Струм заряду від мережі (Reg 189)</span><span class="param-cur" id="cur-r189">-- A</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r189" min="0" max="100" step="1" placeholder="0 - 100 A">
            <button class="btn btn-blue" onclick="writeScaledReg(189, 'in-r189', 10)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Макс. загальний струм заряду (Reg 129)</span><span class="param-cur" id="cur-r129">-- A</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r129" min="10" max="120" step="1" placeholder="10 - 120 A">
            <button class="btn btn-blue" onclick="writeScaledReg(129, 'in-r129', 100)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Напруга постійного заряду CV (Reg 131)</span><span class="param-cur" id="cur-r131">-- V</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r131" min="48.0" max="59.2" step="0.05" placeholder="48.0 - 59.2 V">
            <button class="btn btn-blue" onclick="writeScaledReg(131, 'in-r131', 100)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Напруга підзарядки Float (Reg 190)</span><span class="param-cur" id="cur-r190">-- V</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r190" min="48.0" max="58.4" step="0.1" placeholder="48.0 - 58.4 V">
            <button class="btn btn-blue" onclick="writeScaledReg(190, 'in-r190', 100)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Напруга Stop Discharge (Reg 130)</span><span class="param-cur" id="cur-r130">-- V</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r130" min="40.0" max="52.0" step="0.1" placeholder="40.0 - 52.0 V">
            <button class="btn btn-blue" onclick="writeScaledReg(130, 'in-r130', 100)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Перехід Батарея → Мережа (Reg 191)</span><span class="param-cur" id="cur-r191">-- V</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r191" min="44.0" max="52.0" step="0.1">
            <button class="btn btn-blue" onclick="writeScaledReg(191, 'in-r191', 10)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Перехід Мережа → Батарея (Reg 192)</span><span class="param-cur" id="cur-r192">-- V</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r192" min="48.0" max="58.0" step="0.1">
            <button class="btn btn-blue" onclick="writeScaledReg(192, 'in-r192', 10)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Захисне вимкнення Cut-off (Reg 188)</span><span class="param-cur" id="cur-r188">-- V</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r188" min="40.0" max="48.0" step="0.1">
            <button class="btn btn-blue" onclick="writeScaledReg(188, 'in-r188', 10)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Попередження низької напруги (Reg 195)</span><span class="param-cur" id="cur-r195">-- V</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r195" min="40.0" max="50.0" step="0.1">
            <button class="btn btn-blue" onclick="writeScaledReg(195, 'in-r195', 10)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Макс. час Boost заряду (Reg 194)</span><span class="param-cur" id="cur-r194">-- хв</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r194" min="5" max="900" step="5">
            <button class="btn btn-blue" onclick="writeScaledReg(194, 'in-r194', 1)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Тип акумулятора (Reg 125)</span><span class="param-cur" id="cur-r125">--</span></div>
          <div class="param-ctrl">
            <select id="in-r125">
              <option value="0">0 — Lead-Acid (Свинцево-кислотний)</option>
              <option value="1">1 — Lithium (Літій з BMS)</option>
              <option value="2">2 — Lithium No Comm (Літій без зв'язку)</option>
            </select>
            <button class="btn btn-blue" onclick="writeScaledReg(125, 'in-r125', 1)">Записати</button>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- TAB 3: MODES & SOC THRESHOLDS -->
  <div id="tab-modes" class="tab-pane">
    <div class="card">
      <h2>⚡ Пріоритети роботи інвертора та пороги SOC</h2>
      <div class="param-grid">
        <div class="param-item">
          <div class="param-head"><span>Пріоритет виходу навантаження (Reg 182)</span><span class="param-cur" id="cur-r182">--</span></div>
          <div class="param-ctrl">
            <select id="in-r182">
              <option value="0">0 — SOL (Solar First)</option>
              <option value="1">1 — UTI (Utility First / Байпас)</option>
              <option value="2">2 — SBU (Solar-Battery-Utility)</option>
              <option value="10">10 — Grid-Tied Mode</option>
            </select>
            <button class="btn btn-blue" onclick="writeScaledReg(182, 'in-r182', 1)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Пріоритет заряджання АКБ (Reg 181)</span><span class="param-cur" id="cur-r181">--</span></div>
          <div class="param-ctrl">
            <select id="in-r181">
              <option value="0">0 — CSO (Solar First)</option>
              <option value="1">1 — SNU (Solar & Utility)</option>
              <option value="2">2 — OSO (Only Solar)</option>
            </select>
            <button class="btn btn-blue" onclick="writeScaledReg(181, 'in-r181', 1)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Діапазон вхідної мережі AC (Reg 209)</span><span class="param-cur" id="cur-r209">--</span></div>
          <div class="param-ctrl">
            <select id="in-r209">
              <option value="0">0 — APL (Appliance 90-280V)</option>
              <option value="1">1 — UPS (Uninterruptible 170-280V)</option>
            </select>
            <button class="btn btn-blue" onclick="writeScaledReg(209, 'in-r209', 1)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Комунікація з BMS (Reg 214)</span><span class="param-cur" id="cur-r214">--</span></div>
          <div class="param-ctrl">
            <select id="in-r214">
              <option value="0">0 — Disabled (Voltage Mode)</option>
              <option value="1">1 — CAN Bus</option>
              <option value="2">2 — RS485</option>
            </select>
            <button class="btn btn-blue" onclick="writeScaledReg(214, 'in-r214', 1)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>SOC припинення заряду (Reg 139)</span><span class="param-cur" id="cur-r139">-- %</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r139" min="10" max="100" step="1">
            <button class="btn btn-blue" onclick="writeScaledReg(139, 'in-r139', 1)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>On-Grid: Зупинка розряду SOC (Reg 140)</span><span class="param-cur" id="cur-r140">-- %</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r140" min="10" max="100" step="1">
            <button class="btn btn-blue" onclick="writeScaledReg(140, 'in-r140', 1)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>On-Grid: Відновлення розряду SOC (Reg 185)</span><span class="param-cur" id="cur-r185">-- %</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r185" min="20" max="100" step="1">
            <button class="btn btn-blue" onclick="writeScaledReg(185, 'in-r185', 1)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Off-Grid: Повне вимкнення SOC (Reg 141)</span><span class="param-cur" id="cur-r141">-- %</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r141" min="10" max="100" step="1">
            <button class="btn btn-blue" onclick="writeScaledReg(141, 'in-r141', 1)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Off-Grid: Відновлення розряду SOC (Reg 187)</span><span class="param-cur" id="cur-r187">-- %</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r187" min="10" max="100" step="1">
            <button class="btn btn-blue" onclick="writeScaledReg(187, 'in-r187', 1)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Smart Load Off SOC (Reg 226)</span><span class="param-cur" id="cur-r226">-- %</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r226" min="10" max="95" step="1">
            <button class="btn btn-blue" onclick="writeScaledReg(226, 'in-r226', 1)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Zero Export компенсація (Reg 215)</span><span class="param-cur" id="cur-r215">-- W</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r215" min="0" max="200" step="1">
            <button class="btn btn-blue" onclick="writeScaledReg(215, 'in-r215', 1)">Записати</button>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- TAB 4: SWITCHES & GENERATOR -->
  <div id="tab-switches" class="tab-pane">
    <div class="card">
      <h2>⚙️ Системні прапорці (Регістр 201) та Генератор</h2>
      <div class="param-grid">
        <div class="param-item">
          <div class="param-head"><span>ECO Mode (Reg 201 bit 0)</span><span class="param-cur" id="cur-b0">--</span></div>
          <div class="param-ctrl">
            <button class="btn btn-green" onclick="setBit201(1, 1)">Увімкнути</button>
            <button class="btn btn-red" onclick="setBit201(1, 0)">Вимкнути</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Автостарт при перевантаженні (bit 1)</span><span class="param-cur" id="cur-b1">--</span></div>
          <div class="param-ctrl">
            <button class="btn btn-green" onclick="setBit201(2, 1)">Увімкнути</button>
            <button class="btn btn-red" onclick="setBit201(2, 0)">Вимкнути</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Автостарт при перегріві (bit 2)</span><span class="param-cur" id="cur-b2">--</span></div>
          <div class="param-ctrl">
            <button class="btn btn-green" onclick="setBit201(4, 1)">Увімкнути</button>
            <button class="btn btn-red" onclick="setBit201(4, 0)">Вимкнути</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Нагадування зміни входу (bit 3)</span><span class="param-cur" id="cur-b3">--</span></div>
          <div class="param-ctrl">
            <button class="btn btn-green" onclick="setBit201(8, 1)">Увімкнути</button>
            <button class="btn btn-red" onclick="setBit201(8, 0)">Вимкнути</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Автозапуск генератора (bit 5)</span><span class="param-cur" id="cur-b5">--</span></div>
          <div class="param-ctrl">
            <button class="btn btn-green" onclick="setBit201(32, 1)">Увімкнути</button>
            <button class="btn btn-red" onclick="setBit201(32, 0)">Вимкнути</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Двоканальне навантаження (bit 6)</span><span class="param-cur" id="cur-b6">--</span></div>
          <div class="param-ctrl">
            <button class="btn btn-green" onclick="setBit201(64, 1)">Увімкнути</button>
            <button class="btn btn-red" onclick="setBit201(64, 0)">Вимкнути</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Потужність генератора (Reg 220)</span><span class="param-cur" id="cur-r220">-- кВт</span></div>
          <div class="param-ctrl">
            <input type="number" id="in-r220" min="0" max="15.0" step="0.1">
            <button class="btn btn-blue" onclick="writeScaledReg(220, 'in-r220', 100)">Записати</button>
          </div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Діагностика периферії інвертора</span><span class="param-cur" id="cur-op-time">-- год</span></div>
          <div style="font-size:0.78rem; color:var(--muted); line-height:1.5;">
            Зумер (Reg 207): <b id="diag-buz" style="color:#fff;">--</b> | Bluetooth (Reg 231): <b id="diag-bt" style="color:#fff;">--</b><br>
            Хмара (Reg 234): <b id="diag-cld" style="color:#fff;">--</b><br>
            Логер↔Плата (Reg 235): <b id="diag-log" style="color:#fff;">--</b>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- TAB 5: RS-485 DIAGNOSTICS & LOGS -->
  <div id="tab-diag" class="tab-pane">
    <div class="card">
      <h2>
        <span>🔍 Діагностика шини RS-485 (XY-S485) та Системний Термінал</span>
        <span id="diag-counters" style="font-size:0.78rem; color:var(--muted);">TX: 0 | RX: 0 | Err: 0</span>
      </h2>
      <div class="param-grid" style="margin-bottom:14px;">
        <div class="param-item">
          <div class="param-head"><span>Ручне читання/запис регістра Modbus</span></div>
          <div class="param-ctrl">
            <select id="raw-fc" style="max-width:120px;">
              <option value="4">0x04 Input</option>
              <option value="3">0x03 Holding</option>
              <option value="6">0x06 Write</option>
            </select>
            <input type="number" id="raw-addr" placeholder="Адреса (0..400)">
            <input type="number" id="raw-val" placeholder="К-сть / Знач.">
            <button class="btn btn-blue" onclick="execRawModbus()">Виконати</button>
          </div>
          <div id="raw-result" style="font-size:0.78rem; color:#34d399; font-family:monospace;">Результат: —</div>
        </div>

        <div class="param-item">
          <div class="param-head"><span>Режим трасування пакетів RS-485 та тест пінів</span></div>
          <div class="param-ctrl" style="flex-wrap:wrap;">
            <button class="btn btn-gray" onclick="sendCmd('trace_on')">🟢 Увімк. HEX Trace</button>
            <button class="btn btn-gray" onclick="sendCmd('trace_off')">⚪ Вимк. HEX Trace</button>
            <button class="btn btn-amber" onclick="sendCmd('swap_pins')">🔄 Swap RX ↔ TX (17↔18)</button>
            <button class="btn btn-gray" onclick="copyDiagLog()">📋 Копіювати</button>
            <button class="btn btn-gray" onclick="sendCmd('clear_log')">🧹 Очистити</button>
            <a href="/tty" target="_blank" class="btn btn-blue" style="text-decoration:none;">🖥️ Web TTY (/tty)</a>
          </div>
          <div id="sys-mem" style="font-size:0.76rem; color:var(--muted);">RAM / PSRAM: —</div>
        </div>
      </div>

      <div class="console" id="diag-console" style="height:320px;"></div>
    </div>
  </div>

  <!-- TAB 6: SETTINGS, TAILSCALE & OTA -->
  <div id="tab-settings" class="tab-pane">
    <div class="card">
      <h2>🛠️ Налаштування контролера ESP32-S3-N16R8</h2>
      <form id="cfg-form" onsubmit="saveSettings(event)">
        <div class="param-grid">
          <div class="param-item">
            <div class="param-head"><span>1. Автоматика SoftOFF / SoftON</span></div>
            <label style="font-size:0.75rem; color:var(--muted);">Поріг вимкнення SoftOFF (SOC ≥ %):</label>
            <input type="number" id="cfg-soc-off" min="50" max="100" required>
            <label style="font-size:0.75rem; color:var(--muted);">Поріг старту зарядки від мережі (SOC &lt; %):</label>
            <input type="number" id="cfg-soc-on" min="15" max="95" required>
            <label style="font-size:0.75rem; color:var(--muted);">Зимовий захист LiFePO4 від холоду (T4 &lt; °C):</label>
            <input type="number" id="cfg-cold-t" min="-10" max="15" step="0.5" required>
            <label style="font-size:0.75rem; color:var(--muted);">Поріг наявності мережі 220В (В):</label>
            <input type="number" id="cfg-grid-v" min="50" max="190" step="1" required>
            <label style="font-size:0.75rem; color:var(--muted);">Інтервал оцінки правил (с, дебаунс 2×):</label>
            <input type="number" id="cfg-eval-s" min="5" max="300" required>
            <label style="font-size:0.75rem; color:var(--muted);">Номінальна ємність АКБ для прогнозу ETA (А·год):</label>
            <input type="number" id="cfg-cap-ah" min="10" max="1000" step="1" required>
            <label style="font-size:0.78rem; display:flex; align-items:center; gap:8px; margin-top:4px;">
              <input type="checkbox" id="cfg-sol-auto" style="width:auto;"> Авто-пріоритети Сонця (SBU/UTI за SOC)
            </label>
          </div>

          <div class="param-item">
            <div class="param-head"><span>2. Розпіновка RS-485 та 1.3" OLED</span></div>
            <div class="param-ctrl">
              <div style="width:50%;"><label style="font-size:0.74rem; color:var(--muted);">RS485 RX GPIO:</label><input type="number" id="cfg-rx-pin"></div>
              <div style="width:50%;"><label style="font-size:0.74rem; color:var(--muted);">RS485 TX GPIO:</label><input type="number" id="cfg-tx-pin"></div>
            </div>
            <div class="param-ctrl">
              <div style="width:50%;"><label style="font-size:0.74rem; color:var(--muted);">Baud Rate:</label><input type="number" id="cfg-baud"></div>
              <div style="width:50%;"><label style="font-size:0.74rem; color:var(--muted);">Slave ID:</label><input type="number" id="cfg-slave"></div>
            </div>
            <div class="param-ctrl">
              <div style="width:50%;"><label style="font-size:0.74rem; color:var(--muted);">OLED SDA GPIO:</label><input type="number" id="cfg-sda-pin"></div>
              <div style="width:50%;"><label style="font-size:0.74rem; color:var(--muted);">OLED SCL GPIO:</label><input type="number" id="cfg-scl-pin"></div>
            </div>
            <label style="font-size:0.74rem; color:var(--muted);">Тип контролера OLED (128×64 I2C):</label>
            <select id="cfg-oled-t">
              <option value="1">1.3" SH1106 (зсув 2px — наш варіант)</option>
              <option value="0">0.96" SSD1306 (зсув 0px)</option>
            </select>
            <label style="font-size:0.74rem; color:var(--muted);">Ротація екранів OLED (сек, 0 = тільки головний):</label>
            <input type="number" id="cfg-oled-sec" min="0" max="60">
          </div>

          <div class="param-item">
            <div class="param-head">
              <span>3. Wi-Fi та Telegram Бот</span>
              <button type="button" class="btn btn-gray" style="padding:4px 8px; font-size:0.72rem;" onclick="scanWifi()">🔍 Сканувати Wi-Fi</button>
            </div>
            <select id="wifi-list" style="display:none; margin-bottom:4px;" onchange="document.getElementById('cfg-ssid').value=this.value;"></select>
            <label style="font-size:0.74rem; color:var(--muted);">Wi-Fi SSID:</label>
            <input type="text" id="cfg-ssid" required>
            <label style="font-size:0.74rem; color:var(--muted);">Wi-Fi Пароль (залиште порожнім, щоб не змінювати):</label>
            <input type="password" id="cfg-pass" placeholder="••••••••">
            <label style="font-size:0.78rem; display:flex; align-items:center; gap:8px; margin-top:4px;">
              <input type="checkbox" id="cfg-tg-en" style="width:auto;"> Увімкнути Telegram-бот та сповіщення
            </label>
            <label style="font-size:0.74rem; color:var(--muted);">Telegram Bot Token:</label>
            <input type="text" id="cfg-tg-tok">
            <label style="font-size:0.74rem; color:var(--muted);">Основний Chat ID:</label>
            <input type="text" id="cfg-tg-chat">
            <label style="font-size:0.74rem; color:var(--muted);">Дозволені Chat ID (через кому):</label>
            <input type="text" id="cfg-tg-allow">
          </div>

          <div class="param-item">
            <div class="param-head"><span>4. Tailscale VPN (Віддалена діагностика)</span></div>
            <label style="font-size:0.78rem; display:flex; align-items:center; gap:8px;">
              <input type="checkbox" id="cfg-ts-en" style="width:auto;"> Активувати Tailscale VPN (віддалена діагностика)
            </label>
            <label style="font-size:0.74rem; color:var(--muted);">Ім'я вузла в мережі (Hostname):</label>
            <input type="text" id="cfg-ts-host">
            <label style="font-size:0.74rem; color:var(--muted);">Власний Auth Key (необов'язково, для заміни вшитого):</label>
            <input type="password" id="cfg-ts-key" value="" placeholder="🔒 За замовчуванням: вшитий ключ (введіть свій tskey-auth-...)">
            <div id="ts-key-badge" style="font-size:0.73rem; color:#34d399; margin-top:4px;">🔒 Ключ мережі захищено та вшито у прошивку (не відображається).</div>
            <div style="margin-top:8px; display:flex; gap:8px; flex-wrap:wrap;">
              <button type="button" class="btn btn-gray" onclick="sendCmd('restart_ts')">🔄 Перезапустити Tailscale</button>
              <button type="button" class="btn btn-gray" onclick="sendCmd('reset_ts_key')">↩️ Скинути на вшитий ключ</button>
            </div>
          </div>
        </div>

        <div style="margin-top:14px; display:flex; gap:10px; flex-wrap:wrap;">
          <button type="submit" class="btn btn-green">💾 Зберегти всі налаштування</button>
          <button type="button" class="btn btn-red" onclick="if(confirm('Перезавантажити контролер ESP32-S3?')) sendCmd('reboot')">🔄 Перезавантажити ESP32-S3</button>
        </div>
      </form>
    </div>

    <!-- OTA Firmware Update Card -->
    <div class="card">
      <h2>🚀 Бездротове оновлення прошивки (Web OTA — локально або через Tailscale)</h2>
      <p style="font-size:0.8rem; color:var(--muted); margin-bottom:12px;">Оберіть скомпільований файл <code>firmware.bin</code> для оновлення розділу OTA:</p>
      <div class="param-ctrl" style="max-width:520px;">
        <input type="file" id="ota-file" accept=".bin">
        <button class="btn btn-blue" onclick="uploadOta()">🚀 Прошити OTA</button>
      </div>
      <div class="progress-bar" style="margin-top:10px; height:12px;"><div id="ota-bar" class="progress-fill"></div></div>
      <div id="ota-status" style="font-size:0.8rem; color:var(--accent); margin-top:6px;"></div>
    </div>
  </div>
</div>

<div id="toast" class="toast"></div>

<script>
let cfgLoaded = false;

function showToast(msg, isErr=false) {
  const t = document.getElementById('toast');
  t.style.borderColor = isErr ? '#ef4444' : '#38bdf8';
  t.innerText = msg;
  t.style.display = 'block';
  setTimeout(() => { t.style.display = 'none'; }, 3500);
}

function openTab(id, btn) {
  document.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));
  document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  btn.classList.add('active');
  if (id === 'tab-diag') fetchDiagLog();
}

async function sendCmd(action, extra='') {
  try {
    const res = await fetch('/api/cmd?action=' + encodeURIComponent(action) + extra);
    const data = await res.json();
    showToast(data.msg || (data.ok ? 'Команду виконано' : 'Помилка'), !data.ok);
    updateStatus();
    if (document.getElementById('tab-diag').classList.contains('active')) fetchDiagLog();
  } catch (e) {
    showToast('Помилка запиту: ' + e.message, true);
  }
}

async function writeScaledReg(reg, inputId, scale) {
  const el = document.getElementById(inputId);
  if (!el || el.value === '') { showToast('Введіть значення!', true); return; }
  const val = parseFloat(el.value);
  const raw = Math.round(val * scale);
  await sendCmd('write_reg', '&reg=' + reg + '&val=' + raw);
}

async function setBit201(mask, en) {
  await sendCmd('set_bit201', '&mask=' + mask + '&en=' + en);
}

async function execRawModbus() {
  const fc = document.getElementById('raw-fc').value;
  const addr = document.getElementById('raw-addr').value;
  const val = document.getElementById('raw-val').value || '1';
  if (addr === '') { showToast('Вкажіть адресу регістра', true); return; }
  try {
    const res = await fetch(`/api/raw-modbus?fc=${fc}&addr=${addr}&val=${val}`);
    const data = await res.json();
    document.getElementById('raw-result').innerText = data.result || JSON.stringify(data);
    fetchDiagLog();
  } catch (e) {
    document.getElementById('raw-result').innerText = 'Помилка: ' + e.message;
  }
}

async function updateStatus() {
  try {
    const res = await fetch('/api/status');
    const d = await res.json();

    // Header badges
    const rsB = document.getElementById('hdr-rs485');
    rsB.className = 'badge ' + (d.online ? 'b-green' : 'b-red');
    rsB.innerText = d.online ? 'RS-485: ONLINE' : 'RS-485: OFFLINE';

    const mdB = document.getElementById('hdr-mode');
    mdB.className = 'badge ' + (d.inv_status === 3 ? 'b-red' : (d.power_sw ? 'b-green' : 'b-blue'));
    mdB.innerText = 'Mode: ' + d.inv_mode;

    const vpB = document.getElementById('hdr-vpn');
    vpB.className = 'badge ' + (d.vpn_ip ? 'b-purple' : 'b-yellow');
    vpB.innerText = d.vpn_ip ? ('Tailscale: ' + d.vpn_ip) : ('VPN: ' + d.ts_status);

    document.getElementById('hdr-wifi').innerText = 'Wi-Fi: ' + d.rssi + ' dBm';
    document.getElementById('eta-banner').innerText = '⏳ Батарея: ' + d.eta_text + (d.eta_speed !== 0 ? ` (${d.eta_speed > 0 ? '+' : ''}${d.eta_speed.toFixed(1)}%/год)` : '');

    // Energy Flow Nodes
    document.getElementById('fl-pv-w').innerText = d.pv_w.toFixed(0) + ' Вт';
    document.getElementById('fl-pv-va').innerText = `${d.pv_v.toFixed(1)} В | ${d.pv_a.toFixed(1)} А | Сьогодні: ${d.pv_today.toFixed(2)} кВт·год`;
    document.getElementById('fn-solar').className = 'flow-node' + (d.pv_w > 30 ? ' active-solar' : '');

    document.getElementById('fl-grid-v').innerText = d.grid_v.toFixed(1) + ' В';
    document.getElementById('fl-grid-w').innerText = `${d.grid_w.toFixed(0)} Вт (${d.grid_hz.toFixed(2)} Гц)`;
    document.getElementById('fn-grid').className = 'flow-node' + (d.has_grid ? ' active-grid' : '');

    document.getElementById('fl-inv-mode').innerText = d.inv_mode;
    document.getElementById('fl-inv-temp').innerText = `T1: ${d.t1.toFixed(1)}°C | T2: ${d.t2.toFixed(1)}°C | T3: ${d.t3.toFixed(1)}°C`;
    const swB = document.getElementById('fl-inv-sw');
    swB.className = 'badge ' + (d.power_sw ? 'b-green' : 'b-blue');
    swB.innerText = d.power_sw ? 'SoftON (Генерація 220В)' : 'SoftOFF (Чистий Байпас)';

    document.getElementById('fl-bat-soc').innerText = d.soc + ' %';
    document.getElementById('fl-bat-va').innerText = `${d.bat_v.toFixed(2)} В | ${d.bat_a >= 0 ? '+' : ''}${d.bat_a.toFixed(1)} А (${d.bat_w.toFixed(0)} Вт)`;
    document.getElementById('fn-bat').className = 'flow-node' + (Math.abs(d.bat_a) > 0.5 ? ' active-bat' : '');

    document.getElementById('fl-load-w').innerText = d.load_w.toFixed(0) + ' Вт';
    document.getElementById('fl-load-va').innerText = `${d.load_va.toFixed(0)} ВА | cos φ: ${d.pf.toFixed(2)}`;
    document.getElementById('fn-load').className = 'flow-node' + (d.load_w > 20 ? ' active-load' : '');

    // 6 Cards
    document.getElementById('st-grid-v').innerText = d.grid_v.toFixed(1) + ' V';
    document.getElementById('st-grid-sub').innerText = `${d.grid_a.toFixed(1)} A | ${d.grid_hz.toFixed(2)} Hz`;
    const gBadge = document.getElementById('st-grid-badge');
    gBadge.className = 'badge ' + (d.has_grid ? 'b-green' : 'b-red');
    gBadge.innerText = (d.has_grid ? 'СВІТЛО Є (' : 'СВІТЛА НЕМАЄ (') + d.consecutive + '/2)';

    document.getElementById('st-soc').innerText = d.soc + ' %';
    document.getElementById('st-bat-sub').innerText = `${d.bat_v.toFixed(2)} V | ${d.bat_a >= 0 ? '+' : ''}${d.bat_a.toFixed(2)} A | SOH ${d.soh}%`;
    document.getElementById('st-soc-bar').style.width = Math.min(100, Math.max(0, d.soc)) + '%';

    document.getElementById('st-sw').innerText = d.power_sw ? 'ON (SoftON)' : 'OFF (SoftOFF)';
    document.getElementById('st-mode-sub').innerText = `Статус: ${d.inv_mode} | Вихід: ${d.inv_v.toFixed(1)}V`;
    const stSwB = document.getElementById('st-sw-badge');
    stSwB.className = 'badge ' + (d.power_sw ? 'b-green' : 'b-blue');
    stSwB.innerText = d.power_sw ? 'Генерація активна' : 'Транзит / Байпас';

    document.getElementById('st-eval-cd').innerText = d.next_eval_s + ' с';
    document.getElementById('st-auto-thresholds').innerText = `SoftOFF ≥ ${d.cfg.soc_off}% | Заряд < ${d.cfg.soc_on}%`;
    const auB = document.getElementById('st-auto-badge');
    if (d.frozen) { auB.className = 'badge b-red'; auB.innerText = 'Freeze State!'; }
    else if (d.auto_en) { auB.className = 'badge b-green'; auB.innerText = 'Автоматика АКТИВНА'; }
    else { auB.className = 'badge b-yellow'; auB.innerText = 'ПАУЗА (Ручний режим)'; }

    document.getElementById('st-t1').innerText = d.t1.toFixed(1) + ' °C';
    document.getElementById('st-t2').innerText = d.t2.toFixed(1) + ' °C';
    document.getElementById('st-t3').innerText = d.t3.toFixed(1) + ' °C';
    document.getElementById('st-fan-badge').innerText = 'Fan: ' + d.fan_rpm + ' RPM';

    document.getElementById('st-t4').innerText = d.t4.toFixed(1) + ' °C';
    document.getElementById('st-bms-lim').innerText = `Ліміти BMS: +${d.bms_ccl.toFixed(0)}A / -${d.bms_dcl.toFixed(0)}A`;
    const t4B = document.getElementById('st-t4-badge');
    if (d.t4 < d.cfg.cold_t && d.t4 > -25) {
      t4B.className = 'badge b-red';
      t4B.innerText = '❄️ ХОЛОД (<' + d.cfg.cold_t + '°C) Заряд Блок!';
    } else {
      t4B.className = 'badge b-green';
      t4B.innerText = 'Захист LiFePO4: Норма';
    }

    document.getElementById('last-action-line').innerText = 'Остання подія: ' + d.last_action;

    // Populate Holding Register current values
    document.getElementById('cur-r189').innerText = d.h.r189.toFixed(1) + ' A';
    document.getElementById('cur-r129').innerText = d.h.r129.toFixed(1) + ' A';
    document.getElementById('cur-r131').innerText = d.h.r131.toFixed(2) + ' V';
    document.getElementById('cur-r190').innerText = d.h.r190.toFixed(2) + ' V';
    document.getElementById('cur-r130').innerText = d.h.r130.toFixed(2) + ' V';
    document.getElementById('cur-r191').innerText = d.h.r191.toFixed(1) + ' V';
    document.getElementById('cur-r192').innerText = d.h.r192.toFixed(1) + ' V';
    document.getElementById('cur-r188').innerText = d.h.r188.toFixed(1) + ' V';
    document.getElementById('cur-r195').innerText = d.h.r195.toFixed(1) + ' V';
    document.getElementById('cur-r194').innerText = d.h.r194 + ' хв';
    document.getElementById('cur-r125').innerText = ['Lead-Acid', 'Lithium', 'Lithium No Comm'][d.h.r125] || d.h.r125;

    document.getElementById('cur-r182').innerText = ({0:'SOL',1:'UTI',2:'SBU',10:'Grid-Tied'})[d.h.r182] || d.h.r182;
    document.getElementById('cur-r181').innerText = ({0:'CSO',1:'SNU',2:'OSO'})[d.h.r181] || d.h.r181;
    document.getElementById('cur-r209').innerText = d.h.r209 === 1 ? 'UPS' : 'APL';
    document.getElementById('cur-r214').innerText = ({0:'Disabled',1:'CAN Bus',2:'RS485'})[d.h.r214] || d.h.r214;
    document.getElementById('cur-r139').innerText = d.h.r139 + ' %';
    document.getElementById('cur-r140').innerText = d.h.r140 + ' %';
    document.getElementById('cur-r185').innerText = d.h.r185 + ' %';
    document.getElementById('cur-r141').innerText = d.h.r141 + ' %';
    document.getElementById('cur-r187').innerText = d.h.r187 + ' %';
    document.getElementById('cur-r226').innerText = d.h.r226 + ' %';
    document.getElementById('cur-r215').innerText = d.h.r215 + ' W';

    const r201 = d.h.r201;
    document.getElementById('cur-b0').innerText = (r201 & 1) ? 'ON' : 'OFF';
    document.getElementById('cur-b1').innerText = (r201 & 2) ? 'ON' : 'OFF';
    document.getElementById('cur-b2').innerText = (r201 & 4) ? 'ON' : 'OFF';
    document.getElementById('cur-b3').innerText = (r201 & 8) ? 'ON' : 'OFF';
    document.getElementById('cur-b5').innerText = (r201 & 32) ? 'ON' : 'OFF';
    document.getElementById('cur-b6').innerText = (r201 & 64) ? 'ON' : 'OFF';
    document.getElementById('cur-r220').innerText = d.h.r220.toFixed(1) + ' кВт';
    document.getElementById('cur-op-time').innerText = d.op_hours.toFixed(1) + ' год';
    document.getElementById('diag-buz').innerText = d.h.buz ? 'Увімк.' : 'Muted';
    document.getElementById('diag-bt').innerText = d.h.bt ? 'Увімк.' : 'Вимк.';
    document.getElementById('diag-cld').innerText = d.h.cloud;
    document.getElementById('diag-log').innerText = d.h.logger;

    document.getElementById('diag-counters').innerText = `TX: ${d.tx_cnt} | RX: ${d.rx_cnt} | Err: ${d.err_cnt} (${d.last_err})`;
    document.getElementById('sys-mem').innerText = `Heap: ${(d.free_heap/1024).toFixed(0)} KB | PSRAM: ${(d.free_psram/1024).toFixed(0)} KB | Uptime: ${(d.uptime_s/60).toFixed(1)} хв`;

    // Render Automation Event Log
    const ac = document.getElementById('auto-console');
    let logHtml = '';
    (d.events || []).forEach(ev => {
      logHtml += `<div class="log-row"><span class="log-ts">[${ev.time}]</span> <b>[${ev.type}]</b> ${ev.msg}</div>`;
    });
    ac.innerHTML = logHtml || '<div class="log-row">Журнал порожній</div>';

    // Load settings form once
    if (!cfgLoaded && d.cfg) {
      cfgLoaded = true;
      document.getElementById('cfg-soc-off').value = d.cfg.soc_off;
      document.getElementById('cfg-soc-on').value = d.cfg.soc_on;
      document.getElementById('cfg-cold-t').value = d.cfg.cold_t;
      document.getElementById('cfg-grid-v').value = d.cfg.grid_v;
      document.getElementById('cfg-eval-s').value = d.cfg.eval_s;
      document.getElementById('cfg-cap-ah').value = d.cfg.cap_ah;
      document.getElementById('cfg-sol-auto').checked = d.cfg.sol_auto;
      document.getElementById('cfg-rx-pin').value = d.cfg.rx_pin;
      document.getElementById('cfg-tx-pin').value = d.cfg.tx_pin;
      document.getElementById('cfg-baud').value = d.cfg.baud;
      document.getElementById('cfg-slave').value = d.cfg.slave;
      document.getElementById('cfg-sda-pin').value = d.cfg.sda_pin;
      document.getElementById('cfg-scl-pin').value = d.cfg.scl_pin;
      document.getElementById('cfg-oled-t').value = d.cfg.oled_t;
      document.getElementById('cfg-oled-sec').value = d.cfg.oled_sec;
      document.getElementById('cfg-ssid').value = d.cfg.ssid;
      document.getElementById('cfg-tg-en').checked = d.cfg.tg_en;
      document.getElementById('cfg-tg-tok').value = d.cfg.tg_tok;
      document.getElementById('cfg-tg-chat').value = d.cfg.tg_chat;
      document.getElementById('cfg-tg-allow').value = d.cfg.tg_allow;
      document.getElementById('cfg-ts-en').checked = d.cfg.ts_en;
      document.getElementById('cfg-ts-host').value = d.cfg.ts_host;
      if (d.is_ap) {
        const btns = document.querySelectorAll('.tab-btn');
        openTab('tab-settings', btns[btns.length - 1]);
        showToast('Режим точки доступу: оберіть або введіть Wi-Fi мережу та збережіть!');
      }
    }
    const tsBadge = document.getElementById('ts-key-badge');
    if (tsBadge && d.cfg) {
      tsBadge.innerText = d.cfg.ts_custom
        ? '🔑 Зараз використовується власний ключ користувача (приховано).'
        : '🔒 Зараз використовується вшитий ключ діагностики (приховано).';
      tsBadge.style.color = d.cfg.ts_custom ? '#38bdf8' : '#34d399';
    }
  } catch (e) {}
}

async function scanWifi() {
  showToast('Сканування Wi-Fi мереж (3 с)...');
  try {
    const res = await fetch('/api/scan-wifi');
    const arr = await res.json();
    const sel = document.getElementById('wifi-list');
    sel.innerHTML = '<option value="">-- Оберіть знайдену мережу --</option>';
    arr.forEach(n => {
      sel.innerHTML += `<option value="${n.ssid}">${n.ssid} (${n.rssi} dBm)</option>`;
    });
    sel.style.display = 'block';
    showToast(`Знайдено мереж: ${arr.length}`);
  } catch (e) {
    showToast('Помилка сканування Wi-Fi', true);
  }
}

async function fetchDiagLog() {
  try {
    const res = await fetch('/api/debug-log');
    const arr = await res.json();
    const dc = document.getElementById('diag-console');
    let html = '';
    for (let i = arr.length - 1; i >= 0; i--) {
      const e = arr[i];
      let cls = '';
      if (e.lvl === 1) cls = 'log-warn';
      else if (e.lvl === 2) cls = 'log-err';
      else if (e.lvl === 3) cls = 'log-tx';
      else if (e.lvl === 4) cls = 'log-rx';
      html += `<div class="log-row ${cls}"><span class="log-ts">[${e.wtime}]</span> [${e.tag}] ${e.msg}</div>`;
    }
    dc.innerHTML = html || '<div class="log-row">Лог порожній</div>';
  } catch (e) {}
}

function copyDiagLog() {
  const text = document.getElementById('diag-console').innerText;
  navigator.clipboard.writeText(text);
  showToast('Логи скопійовано в буфер обміну!');
}

async function saveSettings(ev) {
  ev.preventDefault();
  const payload = {
    soc_off: parseInt(document.getElementById('cfg-soc-off').value),
    soc_on: parseInt(document.getElementById('cfg-soc-on').value),
    cold_t: parseFloat(document.getElementById('cfg-cold-t').value),
    grid_v: parseFloat(document.getElementById('cfg-grid-v').value),
    eval_s: parseInt(document.getElementById('cfg-eval-s').value),
    cap_ah: parseFloat(document.getElementById('cfg-cap-ah').value),
    sol_auto: document.getElementById('cfg-sol-auto').checked,
    rx_pin: parseInt(document.getElementById('cfg-rx-pin').value),
    tx_pin: parseInt(document.getElementById('cfg-tx-pin').value),
    baud: parseInt(document.getElementById('cfg-baud').value),
    slave: parseInt(document.getElementById('cfg-slave').value),
    sda_pin: parseInt(document.getElementById('cfg-sda-pin').value),
    scl_pin: parseInt(document.getElementById('cfg-scl-pin').value),
    oled_t: parseInt(document.getElementById('cfg-oled-t').value),
    oled_sec: parseInt(document.getElementById('cfg-oled-sec').value),
    ssid: document.getElementById('cfg-ssid').value,
    pass: document.getElementById('cfg-pass').value,
    tg_en: document.getElementById('cfg-tg-en').checked,
    tg_tok: document.getElementById('cfg-tg-tok').value,
    tg_chat: document.getElementById('cfg-tg-chat').value,
    tg_allow: document.getElementById('cfg-tg-allow').value,
    ts_en: document.getElementById('cfg-ts-en').checked,
    ts_host: document.getElementById('cfg-ts-host').value,
    ts_key: document.getElementById('cfg-ts-key').value.trim()
  };
  try {
    const res = await fetch('/api/settings', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(payload)
    });
    const d = await res.json();
    document.getElementById('cfg-ts-key').value = '';
    showToast(d.msg || 'Налаштування збережено!', !d.ok);
    if (d.reboot) {
      setTimeout(() => {
        showToast('ESP32 перезавантажується... Перепідключіть телефон до вашої мережі Wi-Fi та відкрийте адресу з OLED!', false);
      }, 2000);
      return;
    }
    updateStatus();
  } catch (e) {
    showToast('Помилка збереження: ' + e.message, true);
  }
}

function uploadOta() {
  const fileInput = document.getElementById('ota-file');
  if (!fileInput.files.length) { showToast('Виберіть файл .bin!', true); return; }
  const file = fileInput.files[0];
  const formData = new FormData();
  formData.append('update', file);

  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/update', true);
  xhr.upload.onprogress = function(e) {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded / e.total) * 100);
      document.getElementById('ota-bar').style.width = pct + '%';
      document.getElementById('ota-status').innerText = `Завантаження прошивки: ${pct}% (${(e.loaded/1024).toFixed(0)} KB)...`;
    }
  };
  xhr.onload = function() {
    if (xhr.status === 200) {
      document.getElementById('ota-status').innerText = '✅ Прошивку успішно оновлено! Перезавантаження ESP32-S3...';
      showToast('Оновлення завершено! Перезавантаження...');
      setTimeout(() => location.reload(), 6000);
    } else {
      document.getElementById('ota-status').innerText = '❌ Помилка OTA: ' + xhr.responseText;
      showToast('Помилка OTA', true);
    }
  };
  xhr.send(formData);
}

setInterval(updateStatus, 2500);
setInterval(() => {
  if (document.getElementById('tab-diag').classList.contains('active')) fetchDiagLog();
}, 3000);
updateStatus();
</script>
</body>
</html>
)rawliteral";

class WebServerManager {
public:
    WebServerManager(SandisolarModbus& modbus, AutomationEngine& engine, TelegramBotManager& tgBot, OledDisplayManager& oled, AppConfig& config)
        : m_server(80), m_modbus(modbus), m_engine(engine), m_tgBot(tgBot), m_oled(oled), m_config(config) {}

    void setTailscaleCallbacks(std::function<String()> getVpnIp, std::function<String()> getTsStatus, std::function<void()> restartTs) {
        m_getVpnIp    = getVpnIp;
        m_getTsStatus = getTsStatus;
        m_restartTs   = restartTs;
    }

    void setRemoteConsole(RemoteConsoleManager* console) {
        m_console = console;
    }

    void sendProgmemChunked(const char* progmemStr) {
        size_t totalLen = strlen_P(progmemStr);
        m_server.setContentLength(totalLen);
        m_server.send(200, "text/html; charset=utf-8", "");
        const size_t chunkSize = 900; // MTU-safe for Tailscale WireGuard/DERP
        size_t offset = 0;
        while (offset < totalLen) {
            size_t len = min(chunkSize, totalLen - offset);
            m_server.sendContent_P(progmemStr + offset, len);
            offset += len;
        }
    }

    void begin(bool apMode) {
        m_isApMode = apMode;
        if (m_isApMode) {
            WiFi.mode(WIFI_AP_STA);
            WiFi.disconnect(false, true); // Reset station state machine after WiFi/lwIP init
            delay(50);
            WiFi.softAP("Sandisolar-S3-Setup");
            m_dnsServer.start(53, "*", WiFi.softAPIP());
            DebugLogger::info("WEB", "Started AP Captive Portal 'Sandisolar-S3-Setup' at http://192.168.4.1/");
        }

        m_server.on("/", HTTP_GET, [this]() {
            sendProgmemChunked(DASHBOARD_HTML);
        });

        m_server.on("/setup", HTTP_GET, [this]() {
            sendProgmemChunked(DASHBOARD_HTML);
        });

        m_server.on("/tty", HTTP_GET, [this]() {
            sendProgmemChunked(LITE_TTY_HTML);
        });

        m_server.on("/lite", HTTP_GET, [this]() {
            sendProgmemChunked(LITE_TTY_HTML);
        });

        m_server.on("/api/cli", HTTP_GET, [this]() {
            String cmd = m_server.arg("cmd");
            String out = m_console ? m_console->executeCommand(cmd) : "Console not attached\n";
            m_server.send(200, "text/plain; charset=utf-8", out);
        });

        m_server.on("/api/status", HTTP_GET, [this]() { handleApiStatus(); });
        m_server.on("/api/scan-wifi", HTTP_GET, [this]() {
            DebugLogger::info("WIFI", "Manual Wi-Fi scan requested...");
            WiFi.disconnect(false, false);
            delay(50);
            WiFi.scanDelete();

            int16_t n = WiFi.scanNetworks(false, false, false, 250);
            DebugLogger::info("WIFI", "Scan result count: " + String(n));
            if (n < 0) {
                delay(100);
                n = WiFi.scanNetworks(false, false, true, 250);
                DebugLogger::info("WIFI", "Passive retry count: " + String(n));
            }

            JsonDocument doc;
            JsonArray arr = doc.to<JsonArray>();
            if (n > 0) {
                for (int i = 0; i < n; i++) {
                    String ssid = WiFi.SSID(i);
                    if (ssid.length() == 0) continue;
                    bool exists = false;
                    for (JsonObject item : arr) {
                        if (item["ssid"].as<String>() == ssid) {
                            exists = true;
                            if (WiFi.RSSI(i) > item["rssi"].as<int>()) {
                                item["rssi"] = WiFi.RSSI(i);
                            }
                            break;
                        }
                    }
                    if (!exists) {
                        JsonObject o = arr.add<JsonObject>();
                        o["ssid"] = ssid;
                        o["rssi"] = WiFi.RSSI(i);
                    }
                }
            }
            WiFi.scanDelete();
            String out;
            serializeJson(doc, out);
            m_server.send(200, "application/json; charset=utf-8", out);
        });
        m_server.on("/api/cmd", HTTP_GET, [this]() { handleApiCmd(); });
        m_server.on("/api/raw-modbus", HTTP_GET, [this]() { handleApiRawModbus(); });
        m_server.on("/api/debug-log", HTTP_GET, [this]() {
            m_server.send(200, "application/json; charset=utf-8", DebugLogger::toJson(120));
        });
        m_server.on("/api/settings", HTTP_POST, [this]() { handleApiSaveSettings(); });

        // Web OTA Upload Handler
        m_server.on("/update", HTTP_POST, [this]() {
            bool ok = !Update.hasError();
            m_server.sendHeader("Connection", "close");
            m_server.send(ok ? 200 : 500, "text/plain", ok ? "OK" : "OTA Failed");
            delay(600);
            if (ok) ESP.restart();
        }, [this]() {
            HTTPUpload& upload = m_server.upload();
            if (upload.status == UPLOAD_FILE_START) {
                DebugLogger::info("OTA", "Starting Web OTA update: " + upload.filename);
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    DebugLogger::info("OTA", "Web OTA Success! Total bytes: " + String(upload.totalSize));
                } else {
                    Update.printError(Serial);
                }
            }
        });

        m_server.onNotFound([this]() {
            if (m_isApMode) {
                m_server.sendHeader("Location", "http://192.168.4.1/", true);
                m_server.send(302, "text/plain", "");
            } else {
                m_server.send(404, "text/plain", "Not Found");
            }
        });

        m_server.begin();
        DebugLogger::info("WEB", "HTTP WebServer started on port 80");
    }

    void loop() {
        if (m_isApMode) {
            m_dnsServer.processNextRequest();
        }
        m_server.handleClient();
    }

private:
    void handleApiStatus() {
        SandisolarTelemetry t = m_modbus.getTelemetry();
        BatteryEtaInfo eta = SandisolarTelemetry::calculateEta(t.battery_soc, t.battery_current, m_config.battery_capacity_ah);

        JsonDocument doc;
        doc["online"]      = t.online;
        doc["is_ap"]       = m_isApMode;
        doc["inv_status"]  = t.inverter_status;
        doc["inv_mode"]    = t.inverter_mode_str;
        doc["power_sw"]    = t.power_switch;
        doc["inv_v"]       = t.inverter_voltage;
        doc["inv_a"]       = t.inverter_current;
        doc["grid_v"]      = t.grid_voltage;
        doc["grid_a"]      = t.grid_current;
        doc["grid_hz"]     = t.grid_frequency;
        doc["grid_w"]      = t.grid_power;
        doc["has_grid"]    = (t.grid_voltage >= m_config.grid_v_threshold);
        doc["pf"]          = t.power_factor;
        doc["pv_v"]        = t.pv1_voltage;
        doc["pv_a"]        = t.pv1_current;
        doc["pv_w"]        = t.pv1_power;
        doc["pv_today"]    = t.solar_gen_today;
        doc["pv_total"]    = t.solar_gen_total;
        doc["bat_v"]       = t.battery_voltage;
        doc["bat_a"]       = t.battery_current;
        doc["bat_w"]       = t.battery_power;
        doc["soc"]         = t.battery_soc;
        doc["soh"]         = t.battery_soh;
        doc["bms_ccl"]     = t.bms_max_charge_a;
        doc["bms_dcl"]     = t.bms_max_discharge_a;
        doc["load_w"]      = t.power_out_load;
        doc["load_va"]     = t.power_total_apparent;
        doc["load_var"]    = t.power_total_reactive;
        doc["t1"]          = t.temp_inverter;
        doc["t2"]          = t.temp_boost;
        doc["t3"]          = t.temp_llc;
        doc["t4"]          = t.bms_battery_temp;
        doc["fan_rpm"]     = t.fan_speed;
        doc["op_hours"]    = t.total_operation_time;

        doc["eta_mode"]    = eta.mode;
        doc["eta_text"]    = eta.text;
        doc["eta_speed"]   = eta.speed_pct_hr;

        doc["auto_en"]     = m_config.auto_enabled;
        doc["frozen"]      = m_engine.isFrozen();
        doc["consecutive"] = m_engine.getConsecutiveCount();
        doc["next_eval_s"] = m_engine.getSecondsUntilNextEval();
        doc["last_action"] = m_engine.getLastAction();

        doc["tx_cnt"]      = t.tx_count;
        doc["rx_cnt"]      = t.rx_count;
        doc["err_cnt"]     = t.err_count;
        doc["last_err"]    = t.last_error;

        doc["rssi"]        = WiFi.RSSI();
        doc["lan_ip"]      = WiFi.localIP().toString();
        doc["vpn_ip"]      = m_getVpnIp ? m_getVpnIp() : "";
        doc["ts_status"]   = m_getTsStatus ? m_getTsStatus() : "DISABLED";
        doc["free_heap"]   = ESP.getFreeHeap();
        doc["free_psram"]  = ESP.getFreePsram();
        doc["uptime_s"]    = millis() / 1000;

        JsonObject h = doc["h"].to<JsonObject>();
        h["r125"]   = t.battery_subtype;
        h["r129"]   = t.battery_max_charge_a;
        h["r130"]   = t.battery_stop_disch_v;
        h["r131"]   = t.battery_const_charge_v;
        h["r139"]   = t.battery_charge_stop_soc;
        h["r140"]   = t.on_grid_disch_stop_soc;
        h["r141"]   = t.off_grid_disch_stop_soc;
        h["r181"]   = t.charge_priority;
        h["r182"]   = t.output_priority;
        h["r185"]   = t.on_grid_recovery_soc;
        h["r187"]   = t.off_grid_recovery_soc;
        h["r188"]   = t.battery_uv_cutoff_v;
        h["r189"]   = t.ac_grid_charge_a;
        h["r190"]   = t.battery_float_v;
        h["r191"]   = t.battery_to_ac_v;
        h["r192"]   = t.ac_to_battery_v;
        h["r194"]   = t.boost_charge_time_min;
        h["r195"]   = t.battery_uv_alarm_v;
        h["r201"]   = t.reg201_raw;
        h["buz"]    = t.buzzer_status;
        h["r209"]   = t.ac_input_type;
        h["r214"]   = t.bms_work_mode;
        h["r215"]   = t.grid_power_compensation;
        h["r220"]   = t.gen_input_rated_kw;
        h["r226"]   = t.smart_load_off_soc;
        h["bt"]     = t.bluetooth_status;
        h["cloud"]  = t.cloud_conn_status;
        h["logger"] = t.logger_comm_status;

        JsonObject cfg = doc["cfg"].to<JsonObject>();
        cfg["soc_off"]  = m_config.soc_off_threshold;
        cfg["soc_on"]   = m_config.soc_on_threshold;
        cfg["cold_t"]   = m_config.cold_temp_threshold;
        cfg["grid_v"]   = m_config.grid_v_threshold;
        cfg["eval_s"]   = m_config.eval_interval_s;
        cfg["cap_ah"]   = m_config.battery_capacity_ah;
        cfg["sol_auto"] = m_config.solar_prio_auto;
        cfg["rx_pin"]   = m_config.rs485_rx_pin;
        cfg["tx_pin"]   = m_config.rs485_tx_pin;
        cfg["baud"]     = m_config.modbus_baud;
        cfg["slave"]    = m_config.modbus_slave_id;
        cfg["sda_pin"]  = m_config.oled_sda_pin;
        cfg["scl_pin"]  = m_config.oled_scl_pin;
        cfg["oled_t"]   = m_config.oled_type;
        cfg["oled_sec"] = m_config.oled_page_sec;
        cfg["ssid"]     = m_config.wifi_ssid;
        cfg["tg_en"]    = m_config.tg_enabled;
        cfg["tg_tok"]   = m_config.tg_bot_token;
        cfg["tg_chat"]  = m_config.tg_chat_id;
        cfg["tg_allow"] = m_config.tg_allowed_ids;
        cfg["ts_en"]     = m_config.ts_enabled;
        cfg["ts_host"]   = m_config.ts_hostname;
        cfg["ts_custom"] = m_config.hasCustomTailscaleKey();

        JsonArray evArr = doc["events"].to<JsonArray>();
        auto logs = m_engine.getEventLogs();
        size_t maxLogs = min((size_t)30, logs.size());
        for (size_t i = 0; i < maxLogs; i++) {
            JsonObject eo = evArr.add<JsonObject>();
            eo["time"] = logs[i].wall_time;
            eo["type"] = logs[i].event_type;
            eo["msg"]  = logs[i].message;
        }

        String out;
        serializeJson(doc, out);
        m_server.send(200, "application/json; charset=utf-8", out);
    }

    void handleApiCmd() {
        String action = m_server.arg("action");
        bool ok = false;
        String msg = "";

        if (action == "on") {
            ok = m_modbus.setPowerSwitch(true);
            msg = ok ? "⚡ Інвертор увімкнено (SoftON, reg 0 = 1)" : ("Помилка SoftON: " + m_modbus.getTelemetry().last_error);
            m_engine.addEventLog("web_cmd_on", msg);
        } else if (action == "off") {
            ok = m_modbus.setPowerSwitch(false);
            msg = ok ? "🛑 Інвертор вимкнено (SoftOFF, reg 0 = 0)" : ("Помилка SoftOFF: " + m_modbus.getTelemetry().last_error);
            m_engine.addEventLog("web_cmd_off", msg);
        } else if (action == "pause") {
            m_engine.setAutomationEnabled(false);
            ok = true;
            msg = "⏸️ Автоматику призупинено";
        } else if (action == "resume") {
            m_engine.setAutomationEnabled(true);
            ok = true;
            msg = "▶️ Автоматику відновлено";
        } else if (action == "poll") {
            ok = m_modbus.pollAll(true);
            msg = ok ? "🔄 Всі регістри RS-485 успішно оновлено" : ("Помилка RS-485: " + m_modbus.getTelemetry().last_error);
        } else if (action == "eval") {
            m_engine.evaluateNow();
            ok = true;
            msg = "⚖️ Позачергову перевірку правил автоматики виконано";
        } else if (action == "test_tg") {
            String err;
            ok = m_tgBot.sendTestMessage(err);
            msg = ok ? "💬 Тестове повідомлення надіслано в Telegram!" : ("Помилка Telegram: " + err);
        } else if (action == "write_reg") {
            uint16_t reg = (uint16_t)m_server.arg("reg").toInt();
            uint16_t val = (uint16_t)m_server.arg("val").toInt();
            ok = m_modbus.writeSingleRegister(reg, val);
            msg = ok ? ("✅ Записано регістр " + String(reg) + " = " + String(val)) : ("❌ Помилка запису reg " + String(reg));
            m_engine.addEventLog("web_write_reg", msg);
        } else if (action == "set_bit201") {
            uint16_t mask = (uint16_t)m_server.arg("mask").toInt();
            bool en = (m_server.arg("en").toInt() != 0);
            ok = m_modbus.setReg201Bit(mask, en);
            msg = ok ? ("✅ Оновлено прапорець reg 201 (mask 0x" + String(mask, HEX) + ")") : "❌ Помилка запису reg 201";
            m_engine.addEventLog("web_reg201", msg);
        } else if (action == "trace_on") {
            DebugLogger::setTraceModbus(true);
            ok = true;
            msg = "🟢 HEX-трасування пакетів RS-485 увімкнено";
        } else if (action == "trace_off") {
            DebugLogger::setTraceModbus(false);
            ok = true;
            msg = "⚪ HEX-трасування пакетів RS-485 вимкнено";
        } else if (action == "clear_log") {
            DebugLogger::clear();
            ok = true;
            msg = "🧹 Системний лог очищено";
        } else if (action == "swap_pins") {
            m_modbus.swapRxTxPins(true);
            ok = m_modbus.pollAll(true);
            msg = "🔄 Піни RS-485 поміняно місцями: RX=GPIO" + String(m_modbus.getRxPin()) +
                  ", TX=GPIO" + String(m_modbus.getTxPin()) +
                  (ok ? " — ✅ RS485 ONLINE!" : (" (" + m_modbus.getTelemetry().last_error + ")"));
            ok = true;
        } else if (action == "reset_ts_key") {
            m_config.ts_custom_key = "";
            m_config.ts_auth_key   = m_config.getEffectiveTailscaleKey();
            ConfigManager::save(m_config);
            if (m_restartTs) m_restartTs();
            ok = true;
            msg = "🔒 Скинуто на вшитий ключ діагностики Tailscale!";
        } else if (action == "restart_ts") {
            if (m_restartTs) m_restartTs();
            ok = true;
            msg = "🔄 Tailscale клієнт перезапускається...";
        } else if (action == "reboot") {
            m_server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Перезавантаження...\"}");
            delay(500);
            ESP.restart();
            return;
        } else {
            msg = "Невідома команда: " + action;
        }

        JsonDocument doc;
        doc["ok"] = ok;
        doc["msg"] = msg;
        String out;
        serializeJson(doc, out);
        m_server.send(200, "application/json; charset=utf-8", out);
    }

    void handleApiRawModbus() {
        uint8_t fc    = (uint8_t)m_server.arg("fc").toInt();
        uint16_t addr = (uint16_t)m_server.arg("addr").toInt();
        uint16_t val  = (uint16_t)m_server.arg("val").toInt();

        JsonDocument doc;
        if (fc == 3 || fc == 4) {
            uint16_t cnt = constrain(val, 1, 32);
            uint16_t regs[32];
            if (m_modbus.readRegisters(fc, addr, cnt, regs)) {
                String res = "OK [" + String(cnt) + " regs]: ";
                for (uint16_t i = 0; i < cnt; i++) {
                    res += "[" + String(addr + i) + "]=" + String(regs[i]) + " (0x" + String(regs[i], HEX) + ") ";
                }
                doc["ok"] = true;
                doc["result"] = res;
            } else {
                doc["ok"] = false;
                doc["result"] = "Помилка читання: " + m_modbus.getTelemetry().last_error;
            }
        } else if (fc == 6) {
            if (m_modbus.writeSingleRegister(addr, val)) {
                doc["ok"] = true;
                doc["result"] = "OK: Записано reg " + String(addr) + " = " + String(val);
            } else {
                doc["ok"] = false;
                doc["result"] = "Помилка запису: " + m_modbus.getTelemetry().last_error;
            }
        } else {
            doc["ok"] = false;
            doc["result"] = "Непідтримувана функція";
        }
        String out;
        serializeJson(doc, out);
        m_server.send(200, "application/json; charset=utf-8", out);
    }

    void handleApiSaveSettings() {
        if (!m_server.hasArg("plain")) {
            m_server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Порожній запит\"}");
            return;
        }
        JsonDocument doc;
        if (deserializeJson(doc, m_server.arg("plain"))) {
            m_server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Невірний JSON\"}");
            return;
        }

        bool rs485Changed = false;
        int8_t newRx = doc["rx_pin"] | m_config.rs485_rx_pin;
        int8_t newTx = doc["tx_pin"] | m_config.rs485_tx_pin;
        uint32_t newBaud = doc["baud"] | m_config.modbus_baud;
        uint8_t newSlave = doc["slave"] | m_config.modbus_slave_id;
        if (newRx != m_config.rs485_rx_pin || newTx != m_config.rs485_tx_pin ||
            newBaud != m_config.modbus_baud || newSlave != m_config.modbus_slave_id) {
            rs485Changed = true;
        }

        m_config.soc_off_threshold   = constrain((int)(doc["soc_off"] | m_config.soc_off_threshold), 50, 100);
        m_config.soc_on_threshold    = constrain((int)(doc["soc_on"]  | m_config.soc_on_threshold), 15, 95);
        m_config.cold_temp_threshold = doc["cold_t"] | m_config.cold_temp_threshold;
        m_config.grid_v_threshold    = doc["grid_v"] | m_config.grid_v_threshold;
        m_config.eval_interval_s     = constrain((int)(doc["eval_s"] | m_config.eval_interval_s), 5, 600);
        m_config.battery_capacity_ah = doc["cap_ah"] | m_config.battery_capacity_ah;
        m_config.solar_prio_auto     = doc["sol_auto"] | false;

        m_config.rs485_rx_pin    = newRx;
        m_config.rs485_tx_pin    = newTx;
        m_config.modbus_baud     = newBaud;
        m_config.modbus_slave_id = newSlave;

        m_config.oled_sda_pin    = doc["sda_pin"] | m_config.oled_sda_pin;
        m_config.oled_scl_pin    = doc["scl_pin"] | m_config.oled_scl_pin;
        m_config.oled_type       = doc["oled_t"]  | m_config.oled_type;
        m_config.oled_page_sec   = doc["oled_sec"] | m_config.oled_page_sec;

        bool wifiChanged = false;
        String newSsid = String(doc["ssid"] | "");
        if (newSsid.length() > 0 && newSsid != m_config.wifi_ssid) {
            m_config.wifi_ssid = newSsid;
            wifiChanged = true;
        }
        String newPass = String(doc["pass"] | "");
        if (newPass.length() > 0 && newPass != m_config.wifi_pass) {
            m_config.wifi_pass = newPass;
            wifiChanged = true;
        }

        m_config.tg_enabled     = doc["tg_en"] | false;
        m_config.tg_bot_token   = String(doc["tg_tok"] | "");
        m_config.tg_chat_id     = String(doc["tg_chat"] | "");
        m_config.tg_allowed_ids = String(doc["tg_allow"] | "");

        bool prevTsEn = m_config.ts_enabled;
        m_config.ts_enabled = doc["ts_en"] | true;
        String newTsHost = String(doc["ts_host"] | "");
        if (newTsHost.length() > 0) m_config.ts_hostname = newTsHost;

        bool tsKeyChanged = false;
        String newTsKey = String(doc["ts_key"] | "");
        newTsKey.trim();
        if (newTsKey.length() > 0) {
            if (newTsKey == "reset" || newTsKey == "default") {
                m_config.ts_custom_key = "";
            } else {
                m_config.ts_custom_key = newTsKey;
            }
            m_config.ts_auth_key = m_config.getEffectiveTailscaleKey();
            tsKeyChanged = true;
        }

        ConfigManager::save(m_config);

        if (rs485Changed) {
            m_modbus.begin(m_config);
        }
        if ((prevTsEn != m_config.ts_enabled || tsKeyChanged) && m_restartTs) {
            m_restartTs();
        }
        m_oled.updateConfig(m_config);
        m_engine.addEventLog("settings_saved", "Збережено нові налаштування через Web UI");

        bool needRebootForWiFi = (m_isApMode && m_config.wifi_ssid.length() > 0) || wifiChanged;
        if (needRebootForWiFi) {
            DebugLogger::info("WIFI", "Wi-Fi credentials saved. Rebooting into STA mode...");
            m_server.send(200, "application/json; charset=utf-8",
                "{\"ok\":true,\"reboot\":true,\"msg\":\"✅ Wi-Fi налаштування збережено! ESP32 перезавантажується та підключається до " + m_config.wifi_ssid + "... Зачекайте ~10 с та відкрийте адресу з екрана OLED!\"}");
            delay(1000);
            ESP.restart();
            return;
        }

        m_server.send(200, "application/json; charset=utf-8",
                      "{\"ok\":true,\"msg\":\"✅ Налаштування збережено в NVS та застосовано!\"}");
    }

    WebServer             m_server;
    DNSServer             m_dnsServer;
    SandisolarModbus&     m_modbus;
    AutomationEngine&     m_engine;
    TelegramBotManager&   m_tgBot;
    OledDisplayManager&   m_oled;
    AppConfig&            m_config;
    RemoteConsoleManager* m_console = nullptr;
    bool                  m_isApMode = false;

    std::function<String()> m_getVpnIp;
    std::function<String()> m_getTsStatus;
    std::function<void()>   m_restartTs;
};
