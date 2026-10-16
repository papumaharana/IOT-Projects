// =====================================================================
//  EVA ROBOT  -  ESP32 WROOM 30-pin DevKit (ESP32 DevKit V1)
//  OLED (I2C) + L298N mini + N20 motors + touch sensor + buzzer
//
//  Arduino: folder name must be  EVA_Robot_WROOM  (same as this file)
//  Board  : "ESP32 Dev Module"
//
//  WIRING
//    OLED   SDA -> GPIO 21     SCL -> GPIO 22     VCC -> 3V3   GND -> GND
//    L298N  IN1 -> GPIO 25     IN2 -> GPIO 26
//           IN3 -> GPIO 27     IN4 -> GPIO 33
//    Buzzer (active) + -> GPIO 23  (use a transistor if it draws > 20 mA)
//    Touch sensor OUT  -> GPIO 32
//    All grounds (ESP32, L298N, battery, OLED) must be connected together.
//
//  None of these pins are flash pins (6-11), input-only pins (34-39) or
//  boot-strapping pins (0, 2, 5, 12, 15), so they are safe at power-up.
// =====================================================================

#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include "esp_http_server.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------
// DISPLAY TYPE  ->  choose ONE:  DISPLAY_SSD1306 (0.96")  or  DISPLAY_SH1106 (1.3")
// ---------------------------------------------------------------------
#define DISPLAY_SSD1306 0
#define DISPLAY_SH1106  1
#define OLED_TYPE DISPLAY_SH1106

#if OLED_TYPE == DISPLAY_SH1106
  #include <Adafruit_SH110X.h>
  typedef Adafruit_SH1106G OledType;
  #define OLED_WHITE SH110X_WHITE
#else
  #include <Adafruit_SSD1306.h>
  typedef Adafruit_SSD1306 OledType;
  #define OLED_WHITE SSD1306_WHITE
#endif

#include <FluxGarage_RoboEyes.h>

// NOTE: RoboEyes defines the macros ON, OFF, DEFAULT, TIRED, ANGRY, HAPPY,
// N, NE, E, SE, S, SW, W, NW - never use those as variable names.

// =====================================================
// PINS
// =====================================================
#define OLED_SDA   21
#define OLED_SCL   22
#define OLED_ADDR  0x3C

#define IN1 25
#define IN2 26
#define IN3 27
#define IN4 33

#define BUZZER     23
#define TOUCH_PIN  32

// =====================================================
// SETTINGS
// =====================================================
#define SCREEN_WIDTH     128
#define SCREEN_HEIGHT    64
#define EYES_FPS         25
#define SLEEPY_TIMEOUT   10000
#define HAPPY_HOLD_TIME  1500
#define CMD_TIMEOUT      600      // motors stop if no command for this long (ms)

const char* AP_SSID = "EVA";
const char* AP_PASS = "88888888";

const char* WIFI_SSID = "papu";
const char* WIFI_PASSWORD = "88888888";

// =====================================================
// OBJECTS
// =====================================================
OledType display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
RoboEyes<OledType> roboEyes(display);
httpd_handle_t httpServer = NULL;

// =====================================================
// EMOTIONS
// =====================================================
enum Emotion {
  EMO_NEUTRAL,
  EMO_HAPPY,
  EMO_ANGRY,
  EMO_SLEEPY,
  EMO_SAD,
  EMO_CONFUSED,
  EMO_LAUGH,
  EMO_SURPRISED,
  EMO_EXCITED
};

// web-emotion request codes
#define REQ_HAPPY    0
#define REQ_ANGRY    1
#define REQ_SAD      2
#define REQ_SLEEPY   3
#define REQ_CONFUSED 4
#define REQ_LAUGH    5
#define REQ_WOW      6
#define REQ_EXCITED  7
#define REQ_NEUTRAL  8

// State used ONLY by loop()  (eyes are never touched from the web task)
Emotion currentEmotion = EMO_NEUTRAL;
unsigned long emotionUntil = 0;     // 0 = stays until changed
unsigned long sleepStart = 0;
bool eyesClosed = false;
bool oledOK = false;
char lastDir = 'S';
bool hornApplied = false;
bool lastTouchState = LOW;
unsigned long tapWindowStart = 0;
byte tapCount = 0;

// State shared between the web task and loop()
volatile bool moving = false;
volatile char moveDir = 'S';
volatile bool hornOn = false;
volatile int pendingEmo = -1;
volatile unsigned long lastCmdTime = 0;
volatile unsigned long lastActivityTime = 0;

// Global variables for api calls crashing
volatile bool apiRunning = false;
volatile bool apiDone = false;

char apiResult[768] = "Not tested";

portMUX_TYPE apiMux = portMUX_INITIALIZER_UNLOCKED;

// =====================================================
// WEB PAGE
// =====================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>EVA Robot</title>

<style>
* { box-sizing: border-box; }

body {
  margin: 0;
  padding: 15px;
  background: #111;
  color: white;
  font-family: Arial, sans-serif;
  text-align: center;
  user-select: none;
  -webkit-user-select: none;
  touch-action: manipulation;
}

h2 { margin-top: 10px; margin-bottom: 5px; }

.grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 10px;
  width: 100%;
  max-width: 330px;
  margin: auto;
}

button {
  height: 80px;
  border: none;
  border-radius: 15px;
  font-size: 30px;
  color: white;
  background: #2d6cdf;
  touch-action: none;
  -webkit-tap-highlight-color: transparent;
}

button:active { transform: scale(0.95); background: #173e8f; }

.stop { background: #d33; }
.stop:active { background: #8b1f1f; }

.horn {
  width: 100%;
  max-width: 330px;
  margin-top: 20px;
  background: #d99b00;
  font-size: 24px;
}
.horn:active { background: #8d6800; }

.emo {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 8px;
  width: 100%;
  max-width: 330px;
  margin: 20px auto 0 auto;
}
.emo button {
  height: 56px;
  font-size: 13px;
  border-radius: 12px;
  background: #3a3a3a;
}
.emo button:active { background: #666; }

.info {
  margin-top: 25px;
  color: #777;
  font-size: 13px;
  line-height: 1.6;
}
</style>
</head>
<body>
<h2>EVA</h2>

<div class="grid">
  <div></div>
  <button data-command="F">F</button>
  <div></div>
  <button data-command="L">L</button>
  <button id="stop" class="stop">Stop</button>
  <button data-command="R">R</button>
  <div></div>
  <button data-command="B">B</button>
  <div></div>
</div>

<button id="horn" class="horn">HORN</button>

<button id="testApi" class="horn"
        style="background:#16803c; margin-top:10px;">
  TEST HOME API
</button>

<div id="apiResult"
     style="margin-top:10px; font-size:14px; color:#fff;">
  API status: Not tested
</div>

<div class="emo">
  <button data-emo="happy">&#128522;<br>Happy</button>
  <button data-emo="angry">&#128544;<br>Angry</button>
  <button data-emo="sad">&#128546;<br>Sad</button>
  <button data-emo="sleepy">&#128564;<br>Sleepy</button>
  <button data-emo="confused">&#128533;<br>Confused</button>
  <button data-emo="laugh">&#128514;<br>Laugh</button>
  <button data-emo="wow">&#128558;<br>Wow</button>
  <button data-emo="excited">&#129321;<br>Excited</button>
</div>

<div class="info">
  Hold direction button to move.<br>
  Release button to stop.
</div>

<script>
let commandTimer = null;
let currentCommand = "S";
let busy = false;
let lastSend = 0;

function send(url) {
  return fetch(url, { cache: "no-store" }).catch(function() {});
}

function sendCommand(command) {
  // never pile up requests: skip while one is still in flight (stop always goes out)
  if (command !== "S" && busy && Date.now() - lastSend < 400) return;
  busy = true;
  lastSend = Date.now();
  send("/move?d=" + command).then(function() { busy = false; });
}

function startMovement(command) {
  currentCommand = command;
  busy = false;
  sendCommand(command);
  clearInterval(commandTimer);
  commandTimer = setInterval(function() {
    sendCommand(currentCommand);
  }, 150);
}

function stopMovement() {
  clearInterval(commandTimer);
  commandTimer = null;
  currentCommand = "S";
  busy = false;
  sendCommand("S");
}

document.querySelectorAll("[data-command]").forEach(function(button) {
  const command = button.getAttribute("data-command");

  button.addEventListener("touchstart", function(event) {
    event.preventDefault();
    startMovement(command);
  }, { passive: false });

  button.addEventListener("touchend", function(event) {
    event.preventDefault();
    stopMovement();
  }, { passive: false });

  button.addEventListener("touchcancel", function(event) {
    event.preventDefault();
    stopMovement();
  }, { passive: false });

  button.addEventListener("mousedown", function() { startMovement(command); });
  button.addEventListener("mouseup", function() { stopMovement(); });
  button.addEventListener("mouseleave", function() { stopMovement(); });
});

document.getElementById("stop").addEventListener("click", function() {
  stopMovement();
});

document.querySelectorAll("[data-emo]").forEach(function(button) {
  button.addEventListener("click", function() {
    send("/emo?e=" + button.getAttribute("data-emo"));
  });
});

const horn = document.getElementById("horn");

horn.addEventListener("touchstart", function(event) {
  event.preventDefault();
  send("/horn?s=1");
}, { passive: false });

horn.addEventListener("touchend", function(event) {
  event.preventDefault();
  send("/horn?s=0");
}, { passive: false });

horn.addEventListener("mousedown", function() { send("/horn?s=1"); });
horn.addEventListener("mouseup", function() { send("/horn?s=0"); });

document.getElementById("testApi").addEventListener("click", async function() {
  const result = document.getElementById("apiResult");
  result.textContent = "Starting API request...";
  try {
    const response = await fetch("/test-home", {
      cache: "no-store"
    });
    if (!response.ok) {
      result.textContent = await response.text();
      return;
    }
    for (let i = 0; i < 15; i++) {
      await new Promise(resolve => setTimeout(resolve, 1000));
      const status = await fetch("/test-home-status", {
        cache: "no-store"
      });
      const text = await status.text();
      result.textContent = text;
      if (!text.includes("running...")) {
        return;
      }
    }
    result.textContent = "Timed out waiting for API result.";
  } catch (error) {
    result.textContent = "Request failed: " + error.message;
  }
});
</script>
</body>
</html>)rawliteral";

// =====================================================
// MOTORS  (plain digitalWrite - safe to call from any task)
// =====================================================
void motorsStop() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  moving = false;
}

void drive(bool left1, bool left2, bool right1, bool right2) {
  digitalWrite(IN1, left1);
  digitalWrite(IN2, left2);
  digitalWrite(IN3, right1);
  digitalWrite(IN4, right2);
  moving = true;
}

// Independent safety task: stops the motors even if loop() is busy
void safetyTask(void* param) {
  for (;;) {
    if (moving && (millis() - lastCmdTime > CMD_TIMEOUT)) {
      motorsStop();
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// =====================================================
// WEB HANDLERS  (run in the HTTP task - never touch the eyes here)
// =====================================================
static bool getArg(httpd_req_t* req, const char* key, char* out, size_t outLen) {
  char query[48];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
  return httpd_query_key_value(query, key, out, outLen) == ESP_OK;
}

static esp_err_t sendOK(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

static esp_err_t indexHandler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

void homeApiTask(void* parameter) {
  String result;

  WiFiClient client;
  HTTPClient http;

  http.setConnectTimeout(5000);
  http.setTimeout(10000);

  if (http.begin(client, "http://10.244.22.129:8000/home")) {
        int code = http.GET();

        if (code > 0) {
    String body = http.getString();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);

    if (!error) {
        const char* reply = doc["reply"] | "No reply received";
        result = reply;
    } else {
        result = "JSON parsing failed";
    }
} else {
      result = "Request failed: ";
      result += HTTPClient::errorToString(code);
    }

    http.end();
  } else {
    result = "Could not initialize HTTP request";
  }

  portENTER_CRITICAL(&apiMux);
  snprintf(apiResult, sizeof(apiResult), "%s", result.c_str());
  apiDone = true;
  apiRunning = false;
  portEXIT_CRITICAL(&apiMux);

  vTaskDelete(NULL);
}

static esp_err_t testHomeApiHandler(httpd_req_t* req) {
  portENTER_CRITICAL(&apiMux);

  if (apiRunning) {
    portEXIT_CRITICAL(&apiMux);
    httpd_resp_set_status(req, "409 Conflict");
    return httpd_resp_send(req, "API request already running",
                           HTTPD_RESP_USE_STRLEN);
  }

  apiRunning = true;
  apiDone = false;
  snprintf(apiResult, sizeof(apiResult), "Request started");
  portEXIT_CRITICAL(&apiMux);

  BaseType_t created = xTaskCreate(
    homeApiTask,
    "homeApi",
    8192,
    NULL,
    1,
    NULL
  );

  if (created != pdPASS) {
    portENTER_CRITICAL(&apiMux);
    apiRunning = false;
    snprintf(apiResult, sizeof(apiResult), "Could not create API task");
    portEXIT_CRITICAL(&apiMux);

    httpd_resp_set_status(req, "500 Internal Server Error");
    return httpd_resp_send(req, "Task creation failed",
                           HTTPD_RESP_USE_STRLEN);
  }

  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "API request started",
                         HTTPD_RESP_USE_STRLEN);
}

static esp_err_t homeApiStatusHandler(httpd_req_t* req) {
  char snapshot[768];

  portENTER_CRITICAL(&apiMux);
  snprintf(snapshot, sizeof(snapshot), "%s", apiResult);
  bool running = apiRunning;
  bool done = apiDone;
  portEXIT_CRITICAL(&apiMux);

  httpd_resp_set_type(req, "text/plain");

  if (running) {
    return httpd_resp_send(req, "API request running...",
                           HTTPD_RESP_USE_STRLEN);
  }

  if (!done) {
    return httpd_resp_send(req, "Not tested",
                           HTTPD_RESP_USE_STRLEN);
  }

  return httpd_resp_send(req, snapshot, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t moveHandler(httpd_req_t* req) {
  char v[8] = {0};
  char c = 'S';
  if (getArg(req, "d", v, sizeof(v))) c = v[0];

  // MOTORS FIRST - instant response
  switch (c) {
    case 'F': moveDir = 'F'; drive(HIGH, LOW, HIGH, LOW); break;
    case 'B': moveDir = 'B'; drive(LOW, HIGH, LOW, HIGH); break;
    case 'L': moveDir = 'L'; drive(HIGH, LOW, LOW, HIGH); break;
    case 'R': moveDir = 'R'; drive(LOW, HIGH, HIGH, LOW); break;
    default:  motorsStop(); break;
  }

  lastCmdTime = millis();
  lastActivityTime = lastCmdTime;
  return sendOK(req);
}

static esp_err_t hornHandler(httpd_req_t* req) {
  char v[4] = {0};
  bool on = getArg(req, "s", v, sizeof(v)) && v[0] == '1';

  digitalWrite(BUZZER, on ? HIGH : LOW);
  hornOn = on;
  if (on) lastActivityTime = millis();
  return sendOK(req);
}

static esp_err_t emoHandler(httpd_req_t* req) {
  char v[12] = {0};
  int code = REQ_NEUTRAL;

  if (getArg(req, "e", v, sizeof(v))) {
    if      (strcmp(v, "happy") == 0)    code = REQ_HAPPY;
    else if (strcmp(v, "angry") == 0)    code = REQ_ANGRY;
    else if (strcmp(v, "sad") == 0)      code = REQ_SAD;
    else if (strcmp(v, "sleepy") == 0)   code = REQ_SLEEPY;
    else if (strcmp(v, "confused") == 0) code = REQ_CONFUSED;
    else if (strcmp(v, "laugh") == 0)    code = REQ_LAUGH;
    else if (strcmp(v, "wow") == 0)      code = REQ_WOW;
    else if (strcmp(v, "excited") == 0)  code = REQ_EXCITED;
  }

  lastActivityTime = millis();
  pendingEmo = code;
  return sendOK(req);
}

static void addUri(const char* uri, esp_err_t (*handler)(httpd_req_t*)) {
  httpd_uri_t u;
  memset(&u, 0, sizeof(u));
  u.uri = uri;
  u.method = HTTP_GET;
  u.handler = handler;
  u.user_ctx = NULL;
  httpd_register_uri_handler(httpServer, &u);
}

void startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 10;
  config.stack_size = 6144;
  config.lru_purge_enable = true;     // drop old sockets instead of refusing new ones
  config.recv_wait_timeout = 3;
  config.send_wait_timeout = 3;

  if (httpd_start(&httpServer, &config) == ESP_OK) {
    addUri("/", indexHandler);
    addUri("/move", moveHandler);
    addUri("/horn", hornHandler);
    addUri("/emo", emoHandler);
    addUri("/test-home", testHomeApiHandler);
    addUri("/test-home-status", homeApiStatusHandler);

    Serial.println("Web server started (non-blocking).");
  } else {
    Serial.println("Web server FAILED to start!");
  }
}

// =====================================================
// EYES: SHAPE / LOOK / EMOTION   (called from loop() only)
// =====================================================
void resetEyes() {
  if (eyesClosed) {
    roboEyes.open();
    eyesClosed = false;
  }
  roboEyes.setWidth(36, 36);
  roboEyes.setHeight(36, 36);
  roboEyes.setBorderradius(8, 8);
  roboEyes.setSpacebetween(10);
  roboEyes.setHFlicker(OFF, 0);
  roboEyes.setVFlicker(OFF, 0);
  roboEyes.setAutoblinker(ON, 3, 2);
}

void updateLook() {
  if (!oledOK) return;

  if (lastDir != 'S') {
    roboEyes.setIdleMode(OFF, 2, 2);
    switch (lastDir) {
      case 'F': roboEyes.setPosition(N); break;
      case 'B': roboEyes.setPosition(S); break;
      case 'L': roboEyes.setPosition(W); break;
      case 'R': roboEyes.setPosition(E); break;
      default:  roboEyes.setPosition(DEFAULT); break;
    }
  } else {
    bool idleAllowed = (currentEmotion == EMO_NEUTRAL ||
                        currentEmotion == EMO_HAPPY ||
                        currentEmotion == EMO_EXCITED ||
                        currentEmotion == EMO_LAUGH);

    roboEyes.setPosition(currentEmotion == EMO_SAD ? S : DEFAULT);
    roboEyes.setIdleMode(idleAllowed ? ON : OFF, 2, 2);
  }
}

void setEmotion(Emotion e, unsigned long durationMs) {
  currentEmotion = e;
  emotionUntil = durationMs ? millis() + durationMs : 0;

  if (!oledOK) return;

  resetEyes();

  switch (e) {
    case EMO_NEUTRAL:
      roboEyes.setMood(DEFAULT);
      break;

    case EMO_HAPPY:
      roboEyes.setMood(HAPPY);
      break;

    case EMO_ANGRY:
      roboEyes.setMood(ANGRY);
      break;

    case EMO_SLEEPY:
      roboEyes.setMood(TIRED);
      roboEyes.setAutoblinker(OFF, 3, 2);
      sleepStart = millis();
      break;

    case EMO_SAD:
      roboEyes.setMood(TIRED);
      break;

    case EMO_CONFUSED:
      roboEyes.setMood(DEFAULT);
      roboEyes.anim_confused();
      break;

    case EMO_LAUGH:
      roboEyes.setMood(HAPPY);
      roboEyes.anim_laugh();
      break;

    case EMO_SURPRISED:
      roboEyes.setMood(DEFAULT);
      roboEyes.setWidth(42, 42);
      roboEyes.setHeight(44, 44);
      roboEyes.setBorderradius(14, 14);
      break;

    case EMO_EXCITED:
      roboEyes.setMood(HAPPY);
      roboEyes.setVFlicker(ON, 2);
      break;
  }

  updateLook();
}

void wakeUp() {
  lastActivityTime = millis();
  if (currentEmotion == EMO_SLEEPY) {
    setEmotion(EMO_NEUTRAL, 0);
  }
}

void applyWebEmotion(int code) {
  switch (code) {
    case REQ_HAPPY:    setEmotion(EMO_HAPPY, 4000);     break;
    case REQ_ANGRY:    setEmotion(EMO_ANGRY, 4000);     break;
    case REQ_SAD:      setEmotion(EMO_SAD, 4000);       break;
    case REQ_SLEEPY:   setEmotion(EMO_SLEEPY, 0);       break;
    case REQ_CONFUSED: setEmotion(EMO_CONFUSED, 3000);  break;
    case REQ_LAUGH:    setEmotion(EMO_LAUGH, 2500);     break;
    case REQ_WOW:      setEmotion(EMO_SURPRISED, 3000); break;
    case REQ_EXCITED:  setEmotion(EMO_EXCITED, 4000);   break;
    default:           setEmotion(EMO_NEUTRAL, 0);      break;
  }
}

// =====================================================
// LOOP HELPERS
// =====================================================
// Eyes follow the movement state set by the web task
void syncMovement() {
  char eff = moving ? moveDir : 'S';
  if (eff == lastDir) return;

  lastDir = eff;
  if (eff != 'S') wakeUp();

  if (eff == 'B' && currentEmotion == EMO_NEUTRAL) {
    setEmotion(EMO_SURPRISED, 0);
  } else if (eff != 'B' && currentEmotion == EMO_SURPRISED && emotionUntil == 0) {
    setEmotion(EMO_NEUTRAL, 0);
  } else {
    updateLook();
  }
}

void syncHorn(unsigned long now) {
  bool h = hornOn;
  if (h == hornApplied) return;
  hornApplied = h;

  if (h) {
    wakeUp();
    setEmotion(EMO_ANGRY, 0);
  } else if (currentEmotion == EMO_ANGRY) {
    setEmotion(EMO_NEUTRAL, 0);
    lastActivityTime = now;
  }
}

void handleTouch(unsigned long now) {
  bool touchState = digitalRead(TOUCH_PIN);

  if (touchState == HIGH && lastTouchState == LOW) {
    wakeUp();

    if (now - tapWindowStart > 2000) {
      tapCount = 0;
      tapWindowStart = now;
    }
    tapCount++;

    if (!hornOn) {
      if (tapCount >= 3) {
        tapCount = 0;
        setEmotion(EMO_LAUGH, 2500);          // 3 quick taps = laugh
      } else {
        setEmotion(EMO_HAPPY, HAPPY_HOLD_TIME);
      }
    }
  }

  if (touchState == HIGH && currentEmotion == EMO_HAPPY) {
    emotionUntil = now + HAPPY_HOLD_TIME;
    lastActivityTime = now;
  }

  lastTouchState = touchState;
}

// =====================================================
// OLED + WIFI SETUP
// =====================================================
bool i2cPing(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void setupOLED() {
  Serial.println("Starting OLED...");

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setTimeOut(50);
  Wire.setClock(400000);
  delay(250);

  Serial.print("OLED ping at 0x");
  Serial.print(OLED_ADDR, HEX);
  Serial.println(i2cPing(OLED_ADDR) ? " : OK" : " : NO ANSWER (check wiring) - trying anyway");

#if OLED_TYPE == DISPLAY_SH1106
  display.begin(OLED_ADDR, true);
#else
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, true, false);
#endif
  oledOK = true;    // like the working eyes-only test: always run the eyes

  display.clearDisplay();
  display.setTextColor(OLED_WHITE);
  display.setTextSize(2);
  display.setCursor(38, 24);
  display.println("EVA");
  display.display();
  delay(400);

  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, EYES_FPS);
  roboEyes.setCuriosity(ON);
  setEmotion(EMO_NEUTRAL, 0);

  Serial.println("RoboEyes started.");
}

void setupWiFi() {
  Serial.println("Starting Wi-Fi AP + STA mode...");
  WiFi.mode(WIFI_AP_STA);

  // Keep the existing EVA hotspot
  bool result = WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 4);
  WiFi.setSleep(false);

  Serial.println(result ? "EVA hotspot started!" : "Hotspot failed!");
  Serial.print("Hotspot IP: ");
  Serial.println(WiFi.softAPIP());

  // Connect ESP32 to your home Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to home Wi-Fi...");

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Home Wi-Fi connected!");
    Serial.print("Home Wi-Fi IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Home Wi-Fi connection failed.");
    Serial.println("EVA hotspot remains available.");
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("================================");
  Serial.println("       EVA ROBOT START");
  Serial.println("================================");

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  motorsStop();

  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);

  pinMode(TOUCH_PIN, INPUT_PULLDOWN);

  lastActivityTime = millis();

  setupOLED();
  setupWiFi();
  startWebServer();

  xTaskCreatePinnedToCore(safetyTask, "safety", 2048, NULL, 3, NULL, 0);

  lastActivityTime = millis();
  Serial.println("EVA ROBOT READY!");
}

// =====================================================
// LOOP  - nothing here blocks the web server or the motors
// =====================================================
void loop() {
  unsigned long now = millis();

  syncMovement();
  syncHorn(now);

  int pe = pendingEmo;
  if (pe >= 0) {
    pendingEmo = -1;
    applyWebEmotion(pe);
  }

  handleTouch(now);

  // Timed emotions return to neutral
  if (emotionUntil != 0 && now > emotionUntil) {
    setEmotion(EMO_NEUTRAL, 0);
    lastActivityTime = now;
  }

  // Idle -> sleepy
  if (currentEmotion == EMO_NEUTRAL && !moving && !hornOn &&
      now - lastActivityTime >= SLEEPY_TIMEOUT) {
    setEmotion(EMO_SLEEPY, 0);
  }

  // Sleepy -> eyes fully closed after a few seconds
  if (oledOK && currentEmotion == EMO_SLEEPY && !eyesClosed &&
      now - sleepStart > 4000) {
    roboEyes.close();
    eyesClosed = true;
  }

  if (oledOK) {
    roboEyes.update();
  }

  delay(1);   // let the idle task run
}