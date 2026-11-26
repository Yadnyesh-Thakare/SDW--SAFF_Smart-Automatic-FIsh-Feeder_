#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <I2C_RTC.h>
#include <Servo.h>

// 1. Set the Name (SSID) and Password for the ESP8266 Wi-Fi network
const char* apSSID = "RTC_Calibrator";
const char* apPassword = "99999999";

// 2. Initialize Web Server, DNS, and RTC
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
int servoAngle = 0;
bool relayState = false;

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
};

const int MAX_FEEDINGS = 5;
FeedingSchedule feedings[MAX_FEEDINGS];
int feedingCount = 0;

// Servo feeding active state tracking
bool isFeedingActive = false;
unsigned long feedingStartTime = 0;
unsigned long feedingDurationMs = 0;

// LED Blink tracking variables
unsigned long lastLedBlinkTime = 0;
bool ledBlinkState = false;

// HTML Web Page with Frutiger Aqua Theme, RTC Monitor, Relay Timer, and Feeding Schedules with Delete Option
const char HTML_PAGE[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP8266 Automation & RTC Monitor</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    html, body { min-height: 100%; }

    body {
      font-family: "Segoe UI", "Frutiger", "Trebuchet MS", Arial, sans-serif;
      text-align: center;
      padding: 30px 12px;
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
      margin: 0 auto 20px auto;
      padding: 24px 20px 28px;
      border-radius: 22px;
      background: linear-gradient(180deg, rgba(255,255,255,0.65) 0%, rgba(255,255,255,0.35) 45%, rgba(255,255,255,0.45) 100%);
      border: 1px solid rgba(255,255,255,0.7);
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
      margin-bottom: 6px;
      font-size: 20px;
    }
    .card p {
      color: #22697f;
      font-size: 14px;
      margin-bottom: 8px;
      text-align: left;
    }

    .time-display {
      font-size: 40px;
      font-weight: bold;
      font-family: "Segoe UI", Arial, sans-serif;
      letter-spacing: 1px;
      margin: 8px 0 2px;
      background: linear-gradient(180deg, #ffffff 0%, #eafaff 40%, #8fd3f2 58%, #d8f2ff 100%);
      -webkit-background-clip: text;
      background-clip: text;
      color: transparent;
      -webkit-text-stroke: 1.5px #14628f;
      filter: drop-shadow(0 2px 3px rgba(8, 55, 82, 0.55));
    }
    .date-display {
      font-size: 18px;
      color: #1d6f8a;
      font-weight: 500;
      text-shadow: 0 1px 0 rgba(255,255,255,0.7);
      margin-bottom: 16px;
    }

    button {
      position: relative;
      width: 100%;
      padding: 12px 18px;
      font-size: 15px;
      font-weight: 600;
      font-family: inherit;
      color: #ffffff;
      text-shadow: 0 -1px 1px rgba(0,0,0,0.35);
      border: 1px solid #2e7d32;
      border-radius: 999px;
      cursor: pointer;
      background: linear-gradient(180deg, #a8e063 0%, #56ab2f 49%, #3e8e26 51%, #6cc24a 100%);
      box-shadow: 0 8px 16px rgba(4, 60, 82, 0.35), inset 0 2px 2px rgba(255,255,255,0.6), inset 0 -3px 5px rgba(0,0,0,0.25);
      transition: transform 0.1s ease, filter 0.2s ease;
      margin-top: 10px;
    }
    button:hover  { filter: brightness(1.08); }
    button:active { transform: scale(0.97); filter: brightness(0.95); }

    .btn-danger {
      border: 1px solid #a32c2c;
      background: linear-gradient(180deg, #ff8b8b 0%, #d93838 49%, #b52a2a 51%, #e65c5c 100%);
      width: auto;
      padding: 4px 10px;
      font-size: 12px;
      margin-top: 0;
    }

    input[type=time], input[type=number] {
      width: 100%;
      padding: 8px;
      margin-bottom: 10px;
      border-radius: 8px;
      border: 1px solid #14628f;
      background: rgba(255,255,255,0.8);
      font-size: 14px;
    }

    .status-text { font-weight: bold; color: #054d63; }
    hr { border: 0; border-top: 1px solid rgba(255,255,255,0.5); margin: 16px 0; }
    
    .feeding-item {
      display: flex;
      justify-content: space-between;
      align-items: center;
      background: rgba(255,255,255,0.5);
      padding: 6px 10px;
      border-radius: 8px;
      font-size: 13px;
      margin-top: 6px;
    }

    #status {
      margin-top: 12px;
      font-weight: bold;
      font-size: 14px;
      color: #2e8b1f;
    }
  </style>
</head>
<body>
  <div class="bubble"></div>
  <div class="bubble"></div>
  <div class="bubble"></div>
  <div class="bubble"></div>
  <div class="bubble"></div>

  <!-- RTC Monitor Card -->
  <div class="card">
    <h2>RTC Monitor</h2>
    <div id="rtc-time" class="time-display">--:--:--</div>
    <div id="rtc-date" class="date-display">YYYY-MM-DD</div>
    <button onclick="syncTime()">Sync Browser Time to RTC (IST)</button>
    <div id="status"></div>
  </div>

  <!-- Relay Timer Card -->
  <div class="card">
    <h2>Relay Timer (24-hr)</h2>
    <p>Relay Status: <span id="relay-status" class="status-text">--</span></p>
    <hr>
    <p>Start Time:</p>
    <input type="time" id="relayStart">
    <p>Stop Time:</p>
    <input type="time" id="relayStop">
    <button onclick="saveRelaySchedule()">Save Relay Schedule</button>
  </div>

  <!-- Feeding Schedule Card -->
  <div class="card">
    <h2>Feeding Schedule (Servo)</h2>
    <p>Feeding Time (24-hr):</p>
    <input type="time" id="feedTime">
    <p>Duration (seconds):</p>
    <input type="number" id="feedDuration" min="1" value="5">
    <p>Angle (0 - 180°):</p>
    <input type="number" id="feedAngle" min="0" max="180" value="90">
    <button onclick="addFeedingSchedule()">Add Feeding Time</button>
    
    <p style="margin-top: 15px;"><strong>Active Feedings:</strong></p>
    <div id="feedingsList">None</div>
  </div>

  <script>
    function fetchStatus() {
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          document.getElementById('rtc-time').innerText = data.time;
          document.getElementById('rtc-date').innerText = data.date;
          document.getElementById('relay-status').innerText = data.relay;
          
          if(data.relayStart && !document.getElementById('relayStart').value) {
            document.getElementById('relayStart').value = data.relayStart;
          }
          if(data.relayStop && !document.getElementById('relayStop').value) {
            document.getElementById('relayStop').value = data.relayStop;
          }

          let listHTML = "";
          if (data.feedings && data.feedings.length > 0) {
            data.feedings.forEach((f, idx) => {
              listHTML += `<div class="feeding-item"><span>#${idx+1}: ${f.time} | ${f.angle}° | ${f.duration}s</span><button class="btn-danger" onclick="deleteFeeding(${idx})">Delete</button></div>`;
            });
          } else {
            listHTML = "None";
          }
          document.getElementById('feedingsList').innerHTML = listHTML;
        })
        .catch(err => console.error('Error fetching status:', err));
    }

    function syncTime() {
      var epochTime = Math.floor(Date.now() / 1000); 
      fetch('/settime?epoch=' + epochTime)
        .then(response => response.text())
        .then(data => {
          document.getElementById('status').innerText = data;
          fetchStatus();
        })
        .catch(err => {
          document.getElementById('status').innerText = 'Sync failed.';
        });
    }

    function saveRelaySchedule() {
      let start = document.getElementById('relayStart').value;
      let stop = document.getElementById('relayStop').value;
      fetch(`/setrelay?start=${start}&stop=${stop}`)
        .then(res => res.text())
        .then(msg => alert(msg));
    }

    function addFeedingSchedule() {
      let time = document.getElementById('feedTime').value;
      let duration = document.getElementById('feedDuration').value;
      let angle = document.getElementById('feedAngle').value;
      if(!time) { alert("Please select a valid time!"); return; }
      fetch(`/addfeeding?time=${time}&duration=${duration}&angle=${angle}`)
        .then(res => res.text())
        .then(msg => {
          fetchStatus();
        });
    }

    function deleteFeeding(index) {
      fetch(`/delfeeding?index=` + index)
        .then(res => res.text())
        .then(msg => {
          fetchStatus();
        });
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

void handleCaptive() {
  server.send(200, "text/html", HTML_PAGE);
}

void handleStatus() {
  int ldrValue = analogRead(ldrPin);
  Serial.print("LDR Value: ");
  Serial.println(ldrValue);

  char timeBuf[16];
  char dateBuf[16];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", RTC.getHours(), RTC.getMinutes(), RTC.getSeconds());
  snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", RTC.getYear(), RTC.getMonth(), RTC.getDay());

  String json = "{";
  json += "\"time\":\"" + String(timeBuf) + "\",";
  json += "\"date\":\"" + String(dateBuf) + "\",";
  json += "\"relay\":\"" + String(relayState ? "ON" : "OFF") + "\",";
  json += "\"relayStart\":\"" + relayStartTime + "\",";
  json += "\"relayStop\":\"" + relayStopTime + "\",";
  json += "\"feedings\":[";
  for (int i = 0; i < feedingCount; i++) {
    if (i > 0) json += ",";
    char timeStr[8];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", feedings[i].hour, feedings[i].minute);
    json += "{\"time\":\"" + String(timeStr) + "\",\"duration\":" + String(feedings[i].durationSec) + ",\"angle\":" + String(feedings[i].angle) + "}";
  }
  json += "]}";

  server.send(200, "application/json", json);
}

void handleSetRelay() {
  if (server.hasArg("start") && server.hasArg("stop")) {
    relayStartTime = server.arg("start");
    relayStopTime = server.arg("stop");
    server.send(200, "text/plain", "Relay schedule updated successfully!");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleAddFeeding() {
  if (server.hasArg("time") && server.hasArg("duration") && server.hasArg("angle")) {
    String timeStr = server.arg("time"); // "HH:MM"
    int h = timeStr.substring(0, 2).toInt();
    int m = timeStr.substring(3, 5).toInt();
    int dur = server.arg("duration").toInt();
    int ang = server.arg("angle").toInt();

    if (feedingCount < MAX_FEEDINGS) {
      feedings[feedingCount] = {h, m, dur, ang, false};
      feedingCount++;
      server.send(200, "text/plain", "Feeding added successfully!");
    } else {
      server.send(400, "text/plain", "Max feeding limit reached!");
    }
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleDeleteFeeding() {
  if (server.hasArg("index")) {
    int idx = server.arg("index").toInt();
    if (idx >= 0 && idx < feedingCount) {
      for (int i = idx; i < feedingCount - 1; i++) {
        feedings[i] = feedings[i + 1];
      }
      feedingCount--;
      server.send(200, "text/plain", "Feeding removed successfully!");
    } else {
      server.send(400, "text/plain", "Invalid index!");
    }
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleSetTime() {
  if (server.hasArg("epoch")) {
    unsigned long rawUTCEpoch = server.arg("epoch").toInt();
    long timezoneOffsetSeconds = 19800; // IST Offset (+5:30)
    unsigned long localEpoch = rawUTCEpoch + timezoneOffsetSeconds;
    
    RTC.setEpoch(localEpoch);
    server.send(200, "text/plain", "Success! RTC calibrated to IST.");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void setup() {
  Serial.begin(115200);
  
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

  // WiFi setup
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSSID, apPassword);
  
  IPAddress myIP = WiFi.softAPIP();
  dnsServer.start(53, "*", myIP);

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/setrelay", handleSetRelay);
  server.on("/addfeeding", handleAddFeeding);
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
    int currentS = RTC.getSeconds();

    // 1. Evaluate Relay Schedule
    int startH = relayStartTime.substring(0, 2).toInt();
    int startM = relayStartTime.substring(3, 5).toInt();
    int stopH  = relayStopTime.substring(0, 2).toInt();
    int stopM  = relayStopTime.substring(3, 5).toInt();

    int currentTimeTotalM = currentH * 60 + currentM;
    int startTimeTotalM  = startH * 60 + startM;
    int stopTimeTotalM   = stopH * 60 + stopM;

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

    // 2. Evaluate Feeding Schedules
    if (!isFeedingActive) {
      for (int i = 0; i < feedingCount; i++) {
        if (currentH == feedings[i].hour && currentM == feedings[i].minute) {
          if (!feedings[i].triggeredThisMinute) {
            feedings[i].triggeredThisMinute = true;
            isFeedingActive = true;
            feedingStartTime = millis();
            feedingDurationMs = (unsigned long)feedings[i].durationSec * 1000UL;
            myServo.write(feedings[i].angle);
            Serial.printf("Feeding Triggered! Moved servo to %d° for %d seconds.\n", feedings[i].angle, feedings[i].durationSec);
          }
        } else {
          feedings[i].triggeredThisMinute = false;
        }
      }
    } else {
      if (millis() - feedingStartTime >= feedingDurationMs) {
        myServo.write(0);
        isFeedingActive = false;
        Serial.println("Feeding finished. Servo returned to 0°.");
      }
    }
  }

  // 3. LED Behavior Control (Non-blocking blink logic based on Feeding and LDR)
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