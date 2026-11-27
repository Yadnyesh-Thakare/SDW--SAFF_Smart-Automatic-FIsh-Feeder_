#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <I2C_RTC.h>
#include <Servo.h>
#include <EEPROM.h>

// 1. SoftAP Wi-Fi Credentials
const char* apSSID = "RTC_Calibrator";
const char* apPassword = "99999999";

// 2. Web Server, DNS, and RTC
ESP8266WebServer server(80);
DNSServer dnsServer;
static DS3231 RTC;

// 3. Hardware Pin Definitions
const int servoPin = D7; // GPIO 13
const int relayPin = D6; // GPIO 12 (Active-LOW)
const int ledPin   = D5; // GPIO 14
const int ldrPin   = A0; // A0

// 4. Control Objects & Variables
Servo myServo;
bool relayState = false;
bool manualRelayOverride = false;

// Relay Schedule Variables (24-hr format "HH:MM")
String relayStartTime = "08:00";
String relayStopTime  = "18:00";

// Feeding Schedule Structures
struct FeedingSchedule {
  int hour;
  int minute;
  int durationSec;
  int angle;
  bool triggeredThisMinute;
  int completedDay; // Day of month (1-31) when last completed, -1 if not yet today
};

const int MAX_FEEDINGS = 5;
FeedingSchedule feedings[MAX_FEEDINGS];
int feedingCount = 0;

// Track manual test feeds completed today
int manualFeedsToday = 0;
int lastTrackedDay = -1;
char lastManualTime[6] = "--:--";

// EEPROM Persistence Structure
struct StoredConfig {
  uint32_t magic;
  char relayStart[6];
  char relayStop[6];
  int count;
  FeedingSchedule savedFeedings[MAX_FEEDINGS];
};
const uint32_t EEPROM_MAGIC = 0xCAFE55AB;

// Servo feeding active state tracking
bool isFeedingActive = false;
unsigned long feedingStartTime = 0;
unsigned long feedingDurationMs = 0;

// LED Blink tracking variables
unsigned long lastLedBlinkTime = 0;
bool ledBlinkState = false;

// Save Schedules to Flash EEPROM
void saveConfig() {
  StoredConfig cfg;
  cfg.magic = EEPROM_MAGIC;
  strncpy(cfg.relayStart, relayStartTime.c_str(), 5);
  cfg.relayStart[5] = '\0';
  strncpy(cfg.relayStop, relayStopTime.c_str(), 5);
  cfg.relayStop[5] = '\0';
  cfg.count = feedingCount;
  for (int i = 0; i < MAX_FEEDINGS; i++) {
    cfg.savedFeedings[i] = feedings[i];
    cfg.savedFeedings[i].triggeredThisMinute = false;
  }
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

// Load Schedules from Flash EEPROM
void loadConfig() {
  StoredConfig cfg;
  EEPROM.get(0, cfg);
  if (cfg.magic == EEPROM_MAGIC) {
    relayStartTime = String(cfg.relayStart);
    relayStopTime  = String(cfg.relayStop);
    feedingCount   = constrain(cfg.count, 0, MAX_FEEDINGS);
    for (int i = 0; i < feedingCount; i++) {
      feedings[i] = cfg.savedFeedings[i];
      feedings[i].triggeredThisMinute = false;
    }
  } else {
    saveConfig();
  }
}

// Helper to trigger servo feeding cycle
void triggerFeedingCycle(int angle, int durationSec) {
  isFeedingActive = true;
  feedingStartTime = millis();
  feedingDurationMs = (unsigned long)constrain(durationSec, 1, 60) * 1000UL;
  myServo.write(constrain(angle, 0, 180));
}

// HTML Web Page with Frutiger Aero Theme & Upcoming/Completed Tabs
const char HTML_PAGE[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP8266 Automation & RTC Monitor</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    html, body { min-height: 100%; }

    body {
      font-family: "Segoe UI", "Frutiger", "Trebuchet MS", Arial, sans-serif;
      text-align: center;
      padding: 24px 12px 40px;
      background: linear-gradient(180deg, #cdeeff 0%, #7fd4f0 22%, #2bb3d4 55%, #0b7f9e 85%, #065a73 100%) fixed;
      overflow-x: hidden;
    }

    .bubble {
      position: fixed;
      bottom: -140px;
      border-radius: 50%;
      background: radial-gradient(circle at 30% 30%, rgba(255,255,255,0.85) 0%, rgba(255,255,255,0.25) 40%, rgba(255,255,255,0.05) 70%);
      box-shadow: inset -3px -3px 8px rgba(255,255,255,0.5);
      animation: rise 11s linear infinite;
      z-index: 0;
      pointer-events: none;
    }
    .bubble:nth-child(1) { left: 8%;  width: 34px; height: 34px; animation-duration: 9s; }
    .bubble:nth-child(2) { left: 24%; width: 16px; height: 16px; animation-duration: 12s; animation-delay: 2s; }
    .bubble:nth-child(3) { left: 45%; width: 46px; height: 46px; animation-duration: 8s;  animation-delay: 1s; }
    .bubble:nth-child(4) { left: 68%; width: 22px; height: 22px; animation-duration: 13s; animation-delay: 3s; }
    .bubble:nth-child(5) { left: 86%; width: 30px; height: 30px; animation-duration: 10s; animation-delay: 5s; }
    @keyframes rise {
      0%   { transform: translateY(0) scale(1); opacity: 0; }
      8%   { opacity: 0.9; }
      50%  { transform: translateY(-45vh) scale(1.05); }
      100% { transform: translateY(-110vh) scale(0.9); opacity: 0; }
    }

    .card {
      position: relative;
      z-index: 1;
      max-width: 420px;
      margin: 0 auto 18px auto;
      padding: 22px 20px 24px;
      border-radius: 22px;
      background: linear-gradient(180deg, rgba(255,255,255,0.68) 0%, rgba(255,255,255,0.38) 45%, rgba(255,255,255,0.48) 100%);
      border: 1px solid rgba(255,255,255,0.75);
      box-shadow: 0 12px 32px rgba(4, 60, 82, 0.35), inset 0 1px 0 rgba(255,255,255,0.9);
      overflow: hidden;
    }
    .card::before {
      content: "";
      position: absolute;
      top: -55%; left: -10%;
      width: 120%; height: 95%;
      background: linear-gradient(180deg, rgba(255,255,255,0.55), rgba(255,255,255,0));
      border-radius: 0 0 50% 50%;
      pointer-events: none;
    }

    h2 {
      color: #054d63;
      text-shadow: 0 1px 0 rgba(255,255,255,0.9);
      margin-bottom: 8px;
      font-size: 20px;
    }
    .card p {
      color: #15596e;
      font-size: 13.5px;
      margin-bottom: 6px;
      text-align: left;
      font-weight: 500;
    }

    .time-display {
      font-size: 38px;
      font-weight: bold;
      letter-spacing: 1px;
      margin: 6px 0 2px;
      background: linear-gradient(180deg, #ffffff 0%, #eafaff 40%, #8fd3f2 58%, #d8f2ff 100%);
      -webkit-background-clip: text;
      background-clip: text;
      color: transparent;
      -webkit-text-stroke: 1.5px #14628f;
      filter: drop-shadow(0 2px 3px rgba(8, 55, 82, 0.55));
    }
    .date-display {
      font-size: 17px;
      color: #16627a;
      font-weight: 600;
      text-shadow: 0 1px 0 rgba(255,255,255,0.7);
      margin-bottom: 12px;
    }

    .telemetry-row {
      display: flex;
      justify-content: center;
      gap: 8px;
      flex-wrap: wrap;
      margin-bottom: 12px;
    }
    .badge {
      display: inline-block;
      padding: 4px 11px;
      border-radius: 999px;
      font-size: 11.5px;
      font-weight: 700;
      color: #fff;
      text-shadow: 0 -1px 1px rgba(0,0,0,0.3);
      box-shadow: inset 0 1px 1px rgba(255,255,255,0.6), 0 2px 5px rgba(0,0,0,0.15);
    }
    .badge-green  { background: linear-gradient(180deg, #8ce052, #3c9422); border: 1px solid #2b6e17; }
    .badge-blue   { background: linear-gradient(180deg, #5ec8f8, #1477b8); border: 1px solid #0e588a; }
    .badge-amber  { background: linear-gradient(180deg, #ffca57, #d9820b); border: 1px solid #a66005; }
    .badge-gray   { background: linear-gradient(180deg, #9eb4bd, #5f7680); border: 1px solid #485b63; }

    .status-bar {
      display: flex;
      justify-content: space-between;
      align-items: center;
      background: rgba(255,255,255,0.45);
      padding: 8px 12px;
      border-radius: 12px;
      margin-bottom: 10px;
      font-size: 13.5px;
      color: #054d63;
      font-weight: 600;
    }

    button {
      position: relative;
      width: 100%;
      padding: 11px 16px;
      font-size: 14.5px;
      font-weight: 600;
      font-family: inherit;
      color: #ffffff;
      text-shadow: 0 -1px 1px rgba(0,0,0,0.35);
      border: 1px solid #2e7d32;
      border-radius: 999px;
      cursor: pointer;
      background: linear-gradient(180deg, #a8e063 0%, #56ab2f 49%, #3e8e26 51%, #6cc24a 100%);
      box-shadow: 0 6px 14px rgba(4, 60, 82, 0.3), inset 0 2px 2px rgba(255,255,255,0.6), inset 0 -3px 5px rgba(0,0,0,0.25);
      transition: transform 0.1s ease, filter 0.2s ease;
      margin-top: 8px;
    }
    button:hover  { filter: brightness(1.07); }
    button:active { transform: scale(0.97); filter: brightness(0.95); }
    button:disabled { opacity: 0.55; cursor: not-allowed; filter: grayscale(0.4); }

    .btn-aqua {
      border: 1px solid #0e628a;
      background: linear-gradient(180deg, #7be0ff 0%, #1ea3db 49%, #1280b3 51%, #47c3f5 100%);
    }

    .btn-row {
      display: flex;
      gap: 8px;
      margin-top: 6px;
    }
    .btn-row button { margin-top: 4px; }

    .btn-danger {
      border: 1px solid #a32c2c;
      background: linear-gradient(180deg, #ff8b8b 0%, #d93838 49%, #b52a2a 51%, #e65c5c 100%);
      width: auto;
      padding: 4px 10px;
      font-size: 12px;
      margin-top: 0;
    }

    /* Frutiger Aero Segmented Tabs */
    .tab-bar {
      display: flex;
      background: rgba(5, 77, 99, 0.18);
      padding: 4px;
      border-radius: 999px;
      border: 1px solid rgba(255,255,255,0.65);
      box-shadow: inset 0 2px 4px rgba(4, 60, 82, 0.2);
      margin-top: 6px;
      margin-bottom: 8px;
      gap: 4px;
    }
    .tab-btn {
      flex: 1;
      margin-top: 0;
      padding: 7px 10px;
      font-size: 13px;
      border-radius: 999px;
      border: 1px solid transparent;
      background: transparent;
      color: #054d63;
      text-shadow: 0 1px 0 rgba(255,255,255,0.8);
      box-shadow: none;
    }
    .tab-btn.active {
      color: #ffffff;
      text-shadow: 0 -1px 1px rgba(0,0,0,0.35);
      border: 1px solid #0e628a;
      background: linear-gradient(180deg, #7be0ff 0%, #1ea3db 49%, #1280b3 51%, #47c3f5 100%);
      box-shadow: 0 3px 8px rgba(4, 60, 82, 0.25), inset 0 1px 1px rgba(255,255,255,0.7);
    }

    .grid-2 {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
    }

    input[type=time], input[type=number] {
      width: 100%;
      padding: 8px 10px;
      margin-bottom: 8px;
      border-radius: 10px;
      border: 1px solid #14628f;
      background: rgba(255,255,255,0.88);
      font-size: 14px;
      color: #053b4d;
      font-weight: 600;
    }

    hr { border: 0; border-top: 1px solid rgba(255,255,255,0.6); margin: 12px 0; }

    .feeding-item {
      display: flex;
      justify-content: space-between;
      align-items: center;
      background: rgba(255,255,255,0.62);
      padding: 8px 12px;
      border-radius: 12px;
      font-size: 13px;
      font-weight: 600;
      color: #054d63;
      margin-top: 6px;
      border: 1px solid rgba(255,255,255,0.85);
      text-align: left;
    }
    .feeding-item.completed {
      background: rgba(232, 255, 232, 0.65);
      border-color: rgba(140, 224, 82, 0.7);
    }
    .item-meta {
      display: flex;
      align-items: center;
      gap: 6px;
      flex-wrap: wrap;
    }

    .toast {
      margin-top: 10px;
      font-weight: 700;
      font-size: 13px;
      color: #165e10;
      min-height: 18px;
      text-shadow: 0 1px 0 rgba(255,255,255,0.8);
    }
  </style>
</head>
<body>
  <div class="bubble"></div>
  <div class="bubble"></div>
  <div class="bubble"></div>
  <div class="bubble"></div>
  <div class="bubble"></div>

  <!-- RTC & Environment Monitor Card -->
  <div class="card">
    <h2>RTC &amp; System Monitor</h2>
    <div id="rtc-time" class="time-display">Syncing...</div>
    <div id="rtc-date" class="date-display">Connecting to DS3231</div>
    <div class="telemetry-row">
      <span id="ldr-badge" class="badge badge-blue">Light: --</span>
      <span id="feeder-badge" class="badge badge-gray">Feeder: Idle</span>
    </div>
    <button onclick="syncTime()">Sync Phone/PC Time to RTC (IST)</button>
    <div id="rtc-toast" class="toast"></div>
  </div>

  <!-- Relay Timer & Manual Control Card -->
  <div class="card">
    <h2>Relay Timer (24-hr)</h2>
    <div class="status-bar">
      <span>Relay Output: <span id="relay-badge" class="badge badge-gray">--</span></span>
      <span id="relay-window" style="font-size:12px;">Window: --</span>
    </div>
    <div class="grid-2">
      <div>
        <p>Start Time:</p>
        <input type="time" id="relayStart">
      </div>
      <div>
        <p>Stop Time:</p>
        <input type="time" id="relayStop">
      </div>
    </div>
    <div class="btn-row">
      <button onclick="saveRelaySchedule()">Save Schedule</button>
      <button class="btn-aqua" onclick="toggleRelayManual()">Toggle Now</button>
    </div>
    <div id="relay-toast" class="toast"></div>
  </div>

  <!-- Feeding Schedule Card -->
  <div class="card">
    <h2>Feeding Schedule (Servo)</h2>
    <p>Feeding Time (24-hr):</p>
    <input type="time" id="feedTime">
    <div class="grid-2">
      <div>
        <p>Duration (1-60s):</p>
        <input type="number" id="feedDuration" min="1" max="60" value="5">
      </div>
      <div>
        <p>Angle (0-180°):</p>
        <input type="number" id="feedAngle" min="0" max="180" value="90">
      </div>
    </div>
    <div class="btn-row">
      <button id="addFeedBtn" onclick="addFeedingSchedule()">Add Schedule</button>
      <button class="btn-aqua" onclick="testFeedNow()">Test Feed Now</button>
    </div>
    <div id="feed-toast" class="toast"></div>

    <hr>
    <p><strong>Daily Feeding Tracker (<span id="feedCountLabel">0</span> / 5 Slots):</strong></p>
    <div class="tab-bar">
      <button id="tab-upcoming" class="tab-btn active" onclick="switchTab('upcoming')">
        Upcoming (<span id="upcomingCount">0</span>)
      </button>
      <button id="tab-completed" class="tab-btn" onclick="switchTab('completed')">
        Completed (<span id="completedCount">0</span>)
      </button>
    </div>
    <div id="feedingsList" style="font-size:13px; color:#15596e; margin-top:6px;">No upcoming feedings</div>
  </div>

  <script>
    let currentTab = 'upcoming';
    let latestData = null;

    function switchTab(tab) {
      currentTab = tab;
      document.getElementById('tab-upcoming').classList.toggle('active', tab === 'upcoming');
      document.getElementById('tab-completed').classList.toggle('active', tab === 'completed');
      if (latestData) renderFeedingTabs(latestData);
    }

    function showToast(id, msg, isError = false) {
      const el = document.getElementById(id);
      el.style.color = isError ? '#9e1b1b' : '#145c0e';
      el.innerText = msg;
      setTimeout(() => { if (el.innerText === msg) el.innerText = ''; }, 3500);
    }

    function renderFeedingTabs(data) {
      const all = data.feedings || [];
      document.getElementById('feedCountLabel').innerText = all.length;
      document.getElementById('addFeedBtn').disabled = (all.length >= 5);

      // Sort chronologically by time
      const sorted = [...all].sort((a, b) => a.time.localeCompare(b.time));
      const upcoming = sorted.filter(f => !f.completed);
      const completed = sorted.filter(f => f.completed);

      const manualCount = data.manualFeedsToday || 0;
      document.getElementById('upcomingCount').innerText = upcoming.length;
      document.getElementById('completedCount').innerText = completed.length + manualCount;

      let listHTML = "";
      if (currentTab === 'upcoming') {
        if (upcoming.length > 0) {
          upcoming.forEach((f, i) => {
            const nextTag = (i === 0) ? `<span class="badge badge-blue">Next</span>` : '';
            listHTML += `<div class="feeding-item">
              <div class="item-meta">${nextTag}<span>${f.time} &bull; ${f.angle}° &bull; ${f.duration}s</span></div>
              <button class="btn-danger" onclick="deleteFeeding(${f.index})">Delete</button>
            </div>`;
          });
        } else {
          listHTML = "No upcoming feedings left today";
        }
      } else {
        if (completed.length > 0 || manualCount > 0) {
          completed.forEach(f => {
            listHTML += `<div class="feeding-item completed">
              <div class="item-meta"><span class="badge badge-green">Done</span><span>${f.time} &bull; ${f.angle}° &bull; ${f.duration}s</span></div>
              <button class="btn-danger" onclick="deleteFeeding(${f.index})">Delete</button>
            </div>`;
          });
          if (manualCount > 0) {
            listHTML += `<div class="feeding-item completed">
              <div class="item-meta"><span class="badge badge-amber">Manual</span><span>Test Feeds Today: ${manualCount}x (Last: ${data.lastManualTime})</span></div>
            </div>`;
          }
        } else {
          listHTML = "No feedings completed yet today";
        }
      }
      document.getElementById('feedingsList').innerHTML = listHTML;
    }

    function fetchStatus() {
      fetch('/status')
        .then(r => r.json())
        .then(data => {
          latestData = data;
          document.getElementById('rtc-time').innerText = data.time;
          document.getElementById('rtc-date').innerText = data.date;

          // LDR Badge
          const ldrBadge = document.getElementById('ldr-badge');
          if (data.ldr < 100) {
            ldrBadge.className = 'badge badge-amber';
            ldrBadge.innerText = `Night Mode (LDR: ${data.ldr})`;
          } else {
            ldrBadge.className = 'badge badge-blue';
            ldrBadge.innerText = `Daylight (LDR: ${data.ldr})`;
          }

          // Feeder Badge
          const feederBadge = document.getElementById('feeder-badge');
          if (data.feeding) {
            feederBadge.className = 'badge badge-green';
            feederBadge.innerText = 'Feeder: Dispensing!';
          } else {
            feederBadge.className = 'badge badge-gray';
            feederBadge.innerText = 'Feeder: Idle';
          }

          // Relay Status & Window
          const relayBadge = document.getElementById('relay-badge');
          relayBadge.className = data.relay === 'ON' ? 'badge badge-green' : 'badge badge-gray';
          relayBadge.innerText = data.relay + (data.manualOverride ? ' (Manual)' : '');
          document.getElementById('relay-window').innerText = `${data.relayStart} → ${data.relayStop}`;

          if (data.relayStart && !document.getElementById('relayStart').value) {
            document.getElementById('relayStart').value = data.relayStart;
          }
          if (data.relayStop && !document.getElementById('relayStop').value) {
            document.getElementById('relayStop').value = data.relayStop;
          }

          renderFeedingTabs(data);
        })
        .catch(err => console.error('Status fetch error:', err));
    }

    function syncTime() {
      const epochTime = Math.floor(Date.now() / 1000);
      fetch('/settime?epoch=' + epochTime)
        .then(r => r.text())
        .then(msg => { showToast('rtc-toast', msg); fetchStatus(); })
        .catch(() => showToast('rtc-toast', 'Sync failed.', true));
    }

    function saveRelaySchedule() {
      const start = document.getElementById('relayStart').value;
      const stop = document.getElementById('relayStop').value;
      if (!start || !stop) { showToast('relay-toast', 'Select both start and stop times.', true); return; }
      fetch(`/setrelay?start=${start}&stop=${stop}`)
        .then(r => r.text())
        .then(msg => { showToast('relay-toast', msg); fetchStatus(); });
    }

    function toggleRelayManual() {
      fetch('/togglerelay')
        .then(r => r.text())
        .then(msg => { showToast('relay-toast', msg); fetchStatus(); });
    }

    function addFeedingSchedule() {
      const time = document.getElementById('feedTime').value;
      let duration = parseInt(document.getElementById('feedDuration').value) || 5;
      let angle = parseInt(document.getElementById('feedAngle').value);
      if (isNaN(angle)) angle = 90;

      duration = Math.max(1, Math.min(60, duration));
      angle = Math.max(0, Math.min(180, angle));
      document.getElementById('feedDuration').value = duration;
      document.getElementById('feedAngle').value = angle;

      if (!time) { showToast('feed-toast', 'Please pick a feeding time.', true); return; }
      fetch(`/addfeeding?time=${time}&duration=${duration}&angle=${angle}`)
        .then(async r => {
          const msg = await r.text();
          showToast('feed-toast', msg, !r.ok);
          fetchStatus();
        });
    }

    function testFeedNow() {
      let duration = Math.max(1, Math.min(60, parseInt(document.getElementById('feedDuration').value) || 5));
      let angle = Math.max(0, Math.min(180, parseInt(document.getElementById('feedAngle').value) || 90));
      fetch(`/testfeed?duration=${duration}&angle=${angle}`)
        .then(r => r.text())
        .then(msg => { showToast('feed-toast', msg); fetchStatus(); });
    }

    function deleteFeeding(index) {
      fetch(`/delfeeding?index=${index}`)
        .then(r => r.text())
        .then(msg => { showToast('feed-toast', msg); fetchStatus(); });
    }

    setInterval(fetchStatus, 1000);
    fetchStatus();
  </script>
</body>
</html>
)=====";

void handleRoot() {
  server.send(200, "text/html", HTML_PAGE);
}

// HTTP 302 Redirect for OS Captive Portal Detection & Unknown Hosts
void handleCaptive() {
  if (server.hostHeader() != "192.168.4.1") {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(200, "text/html", HTML_PAGE);
}

void handleStatus() {
  int ldrValue = analogRead(ldrPin);
  int currentH = RTC.getHours();
  int currentM = RTC.getMinutes();
  int currentDay = RTC.getDay();
  int nowMinutes = currentH * 60 + currentM;

  char timeBuf[16];
  char dateBuf[16];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", currentH, currentM, RTC.getSeconds());
  snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", RTC.getYear(), RTC.getMonth(), currentDay);

  String json = "{";
  json += "\"time\":\"" + String(timeBuf) + "\",";
  json += "\"date\":\"" + String(dateBuf) + "\",";
  json += "\"ldr\":" + String(ldrValue) + ",";
  json += "\"feeding\":" + String(isFeedingActive ? "true" : "false") + ",";
  json += "\"relay\":\"" + String(relayState ? "ON" : "OFF") + "\",";
  json += "\"manualOverride\":" + String(manualRelayOverride ? "true" : "false") + ",";
  json += "\"relayStart\":\"" + relayStartTime + "\",";
  json += "\"relayStop\":\"" + relayStopTime + "\",";
  json += "\"manualFeedsToday\":" + String(manualFeedsToday) + ",";
  json += "\"lastManualTime\":\"" + String(lastManualTime) + "\",";
  json += "\"feedings\":[";
  for (int i = 0; i < feedingCount; i++) {
    if (i > 0) json += ",";
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", feedings[i].hour, feedings[i].minute);
    int schedMinutes = feedings[i].hour * 60 + feedings[i].minute;
    bool isCompleted = (feedings[i].completedDay == currentDay) || (schedMinutes < nowMinutes);

    json += "{\"index\":" + String(i) + ",";
    json += "\"time\":\"" + String(timeStr) + "\",";
    json += "\"duration\":" + String(feedings[i].durationSec) + ",";
    json += "\"angle\":" + String(feedings[i].angle) + ",";
    json += "\"completed\":" + String(isCompleted ? "true" : "false") + "}";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void handleSetRelay() {
  if (server.hasArg("start") && server.hasArg("stop")) {
    relayStartTime = server.arg("start");
    relayStopTime  = server.arg("stop");
    manualRelayOverride = false;
    saveConfig();
    server.send(200, "text/plain", "Relay schedule saved to flash!");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleToggleRelay() {
  manualRelayOverride = true;
  relayState = !relayState;
  digitalWrite(relayPin, relayState ? LOW : HIGH);
  server.send(200, "text/plain", relayState ? "Relay turned ON (Manual)" : "Relay turned OFF (Manual)");
}

void handleAddFeeding() {
  if (server.hasArg("time") && server.hasArg("duration") && server.hasArg("angle")) {
    String timeStr = server.arg("time");
    int h = timeStr.substring(0, 2).toInt();
    int m = timeStr.substring(3, 5).toInt();
    int dur = constrain(server.arg("duration").toInt(), 1, 60);
    int ang = constrain(server.arg("angle").toInt(), 0, 180);

    if (feedingCount < MAX_FEEDINGS) {
      feedings[feedingCount] = {h, m, dur, ang, false, -1};
      feedingCount++;
      saveConfig();
      server.send(200, "text/plain", "Feeding schedule saved!");
    } else {
      server.send(400, "text/plain", "Max 5 schedules reached!");
    }
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleTestFeed() {
  int dur = server.hasArg("duration") ? server.arg("duration").toInt() : 5;
  int ang = server.hasArg("angle") ? server.arg("angle").toInt() : 90;
  triggerFeedingCycle(ang, dur);
  manualFeedsToday++;
  snprintf(lastManualTime, sizeof(lastManualTime), "%02d:%02d", RTC.getHours(), RTC.getMinutes());
  server.send(200, "text/plain", "Dispensing now (" + String(ang) + "° for " + String(dur) + "s)!");
}

void handleDeleteFeeding() {
  if (server.hasArg("index")) {
    int idx = server.arg("index").toInt();
    if (idx >= 0 && idx < feedingCount) {
      for (int i = idx; i < feedingCount - 1; i++) {
        feedings[i] = feedings[i + 1];
      }
      feedingCount--;
      saveConfig();
      server.send(200, "text/plain", "Schedule deleted.");
    } else {
      server.send(400, "text/plain", "Invalid index!");
    }
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleSetTime() {
  if (server.hasArg("epoch")) {
    unsigned long rawUTCEpoch = strtoul(server.arg("epoch").c_str(), NULL, 10);
    long timezoneOffsetSeconds = 19800; // IST (+5:30)
    unsigned long localEpoch = rawUTCEpoch + timezoneOffsetSeconds;

    RTC.setEpoch(localEpoch);
    // Reset today's completion flags when clock is recalibrated
    for (int i = 0; i < feedingCount; i++) {
      feedings[i].completedDay = -1;
    }
    server.send(200, "text/plain", "RTC calibrated to IST!");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  loadConfig();

  Wire.begin();
  RTC.begin();
  RTC.setHourMode(CLOCK_H24);

  // Configure hardware pins
  myServo.attach(servoPin);
  myServo.write(0);

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH); // Off initially (Active-LOW)

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  // WiFi SoftAP + Captive Portal DNS
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSSID, apPassword);

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", IPAddress(192, 168, 4, 1));

  // Web Server Routes + OS Captive Portal Endpoints
  server.on("/", handleRoot);
  server.on("/generate_204", handleCaptive);        // Android captive check
  server.on("/gen_204", handleCaptive);             // Android fallback
  server.on("/hotspot-detect.html", handleCaptive); // Apple iOS/macOS check
  server.on("/connecttest.txt", handleCaptive);     // Windows check
  server.on("/fwlink", handleCaptive);              // Windows redirect check
  server.on("/status", handleStatus);
  server.on("/setrelay", handleSetRelay);
  server.on("/togglerelay", handleToggleRelay);
  server.on("/addfeeding", handleAddFeeding);
  server.on("/testfeed", handleTestFeed);
  server.on("/delfeeding", handleDeleteFeeding);
  server.on("/settime", handleSetTime);
  server.onNotFound(handleCaptive);

  server.begin();
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck >= 1000) {
    lastCheck = millis();

    int currentH = RTC.getHours();
    int currentM = RTC.getMinutes();
    int currentDay = RTC.getDay();

    // Reset daily manual counter at midnight
    if (lastTrackedDay != currentDay) {
      lastTrackedDay = currentDay;
      manualFeedsToday = 0;
      strcpy(lastManualTime, "--:--");
    }

    // 1. Evaluate Relay Schedule (unless manually overridden)
    if (!manualRelayOverride) {
      int startH = relayStartTime.substring(0, 2).toInt();
      int startM = relayStartTime.substring(3, 5).toInt();
      int stopH  = relayStopTime.substring(0, 2).toInt();
      int stopM  = relayStopTime.substring(3, 5).toInt();

      int currentTimeTotalM = currentH * 60 + currentM;
      int startTimeTotalM   = startH * 60 + startM;
      int stopTimeTotalM    = stopH * 60 + stopM;

      bool shouldBeOn = false;
      if (startTimeTotalM <= stopTimeTotalM) {
        shouldBeOn = (currentTimeTotalM >= startTimeTotalM && currentTimeTotalM < stopTimeTotalM);
      } else {
        shouldBeOn = (currentTimeTotalM >= startTimeTotalM || currentTimeTotalM < stopTimeTotalM);
      }

      if (relayState != shouldBeOn) {
        relayState = shouldBeOn;
        digitalWrite(relayPin, relayState ? LOW : HIGH);
      }
    }

    // 2. Evaluate Feeding Schedules
    if (!isFeedingActive) {
      for (int i = 0; i < feedingCount; i++) {
        if (currentH == feedings[i].hour && currentM == feedings[i].minute) {
          if (!feedings[i].triggeredThisMinute) {
            feedings[i].triggeredThisMinute = true;
            feedings[i].completedDay = currentDay;
            triggerFeedingCycle(feedings[i].angle, feedings[i].durationSec);
            Serial.printf("Feeding Triggered! Servo %d° for %ds.\n", feedings[i].angle, feedings[i].durationSec);
          }
        } else {
          feedings[i].triggeredThisMinute = false;
        }
      }
    }
  }

  // Check if active feeding duration has elapsed
  if (isFeedingActive && (millis() - feedingStartTime >= feedingDurationMs)) {
    myServo.write(0);
    isFeedingActive = false;
    Serial.println("Feeding finished. Servo returned to 0°.");
  }

  // 3. LED Behavior Control (Non-blocking blink logic)
  int currentLdrVal = analogRead(ldrPin);
  unsigned long currentMillis = millis();

  if (isFeedingActive) {
    if (currentMillis - lastLedBlinkTime >= 200) {
      lastLedBlinkTime = currentMillis;
      ledBlinkState = !ledBlinkState;
      digitalWrite(ledPin, ledBlinkState ? HIGH : LOW);
    }
  } else if (currentLdrVal < 100) {
    if (currentMillis - lastLedBlinkTime >= 1000) {
      lastLedBlinkTime = currentMillis;
      ledBlinkState = !ledBlinkState;
      digitalWrite(ledPin, ledBlinkState ? HIGH : LOW);
    }
  } else {
    digitalWrite(ledPin, LOW);
    ledBlinkState = false;
  }
}
