// ================================================================
// EVA ROBOT - ESP32-C3 + SH1106 128x64 OLED + touch sensor + buzzer
// Adapted from the user's uploaded EVA Robot sketch.
//
// Board: ESP32-C3 (select the matching C3 board in Arduino IDE)
// Wi-Fi: ESP32 creates its own hotspot only; it never joins home Wi-Fi.
//
// Suggested ESP32-C3 Super Mini pins (verify your exact board):
//   OLED SH1106: SDA -> GPIO 5, SCL -> GPIO 6, VCC -> 3V3, GND -> GND
//   Touch sensor OUT -> GPIO 3
//   Active buzzer + -> GPIO 4, buzzer - -> GND
//
// Keep all grounds common. If your touch module output is active LOW,
// change TOUCH_ACTIVE_LEVEL to LOW below. If the buzzer draws more than
// a GPIO can safely supply, use a transistor driver.
//
// Libraries required:
//   Adafruit GFX Library
//   Adafruit SH110X
//   FluxGarage RoboEyes
// ================================================================

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <FluxGarage_RoboEyes.h>
#include "esp_http_server.h"

// ---------------- Pins: ESP32-C3 ----------------
#define OLED_SDA   5
#define OLED_SCL   6
#define OLED_ADDR  0x3C

#define TOUCH_PIN  3
#define BUZZER_PIN 4
#define LED_PIN 7

// Most digital touch modules output HIGH when touched.
#define TOUCH_ACTIVE_LEVEL HIGH

// ---------------- Display ----------------
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define EYES_FPS      25

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
RoboEyes<Adafruit_SH1106G> roboEyes(display);
bool oledOK = false;

// ---------------- Wi-Fi access point only ----------------
const char* AP_SSID = "EVA";
const char* AP_PASS = "88888888";
httpd_handle_t httpServer = nullptr;
bool apEnabled = false;
unsigned long noClientsSince = 0;
const unsigned long AP_NO_CLIENT_TIMEOUT = 90000UL; // 90 seconds

// ---------------- Emotions ----------------
enum Emotion {
  EMO_NEUTRAL,
  EMO_HAPPY,
  EMO_ANGRY,
  EMO_SLEEPY,
  EMO_SAD,
  EMO_CONFUSED,
  EMO_LAUGH,
  EMO_SURPRISED,
  EMO_EXCITED,
  EMO_WINK,
  EMO_PLEADING,
  EMO_NERD,
  EMO_STARSTRUCK,
  EMO_DIZZY,
  EMO_SPIRAL,
  EMO_ALIEN,
  EMO_LOVE
};

// Forward declarations (Arduino IDE 1.8.x compatibility).
bool isCustomEmotion(Emotion e);
void drawCustomEmotion();
void drawHeart(int cx, int cy, int size);
void drawSpiralEye(int cx, int cy);
void updateLed(unsigned long now);

volatile int pendingEmotion = -1;
Emotion currentEmotion = EMO_NEUTRAL;
unsigned long emotionUntil = 0;
unsigned long sleepStart = 0;
unsigned long lastActivityTime = 0;
const unsigned long SLEEPY_TIMEOUT = 10000; // automatic sleep disabled below
const unsigned long HAPPY_HOLD_TIME = 1500;

bool eyesClosed = false;
bool lastTouchState = false;
unsigned long touchWindowStart = 0;
uint8_t touchCount = 0;
unsigned long buzzerOffAt = 0;
unsigned long lastGazeChange = 0;
int gazeDirection = 0;

// ---------------- LED and OLED Flash Controls ----------------
enum LedMode {
  LED_OFF,
  LED_ON,
  LED_SOS
};

enum DisplayMode {
  DISPLAY_NORMAL,
  DISPLAY_FLASH,
  DISPLAY_SOS
};

LedMode ledMode = LED_OFF;
DisplayMode displayMode = DISPLAY_NORMAL;

volatile int pendingLedCommand = 0;
volatile int pendingDisplayCommand = 0;

// LED SOS timing
const unsigned int SOS_PATTERN[] = {
  200, 200, 200, 200, 200, 600,
  600, 200, 600, 200, 600, 600,
  200, 200, 200, 200, 200, 1400
};

const int SOS_PATTERN_LENGTH = 18;

int sosStep = 0;
unsigned long sosLastChange = 0;

// OLED SOS timing
unsigned long displaySOSLastChange = 0;
bool displaySOSWhite = false;

// ---------------- Minimal emotion-only web page ----------------
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EVA Emotions</title>
<style>
*{box-sizing:border-box}
body{margin:0;padding:22px;background:#111;color:#fff;font-family:Arial,sans-serif;text-align:center}
h2{margin:8px 0 6px}
p{color:#aaa;font-size:14px;margin:0 0 22px}
.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px;max-width:420px;margin:auto}
button{min-height:78px;border:0;border-radius:14px;background:#303744;color:#fff;font-size:15px;font-weight:600;cursor:pointer}
button:active{transform:scale(.96);background:#52627c}
.emoji{display:block;font-size:27px;margin-bottom:5px}
#status{margin-top:20px;color:#9db7ff;font-size:14px}
.led-controls {
  display: flex;
  justify-content: center;
  gap: 12px;
  max-width: 420px;
  margin: 20px auto;
}
.led-controls button {
  flex: 1;
  min-height: 65px;
  background: #245b46;
}
.display-controls {
  display: flex;
  flex-direction: column;
  gap: 10px;
  max-width: 420px;
  margin: 15px auto;
}
.display-controls button {
  min-height: 55px;
  background: #414a60;
}
@media(max-width:350px) {
  .led-controls {
    flex-direction: column;
  }
}
</style>
</head>
<body>
<h2>🤖 EVA</h2>
<p>Connect to the EVA Wi-Fi hotspot, then choose an emotion.</p>
<div class="led-controls">
  <button onclick="sendCommand('/led?m=toggle')">
    💡 ON/OFF
  </button>
  <button onclick="sendCommand('/led?m=sos')">
    💡 SOS
  </button>
</div>
<div class="grid">
<button data-emo="neutral"><span class="emoji">😐</span>Neutral</button>
<button data-emo="happy"><span class="emoji">😊</span>Happy</button>
<button data-emo="angry"><span class="emoji">😠</span>Angry</button>
<button data-emo="sleepy"><span class="emoji">😴</span>Sleepy</button>
<button data-emo="sad"><span class="emoji">😢</span>Sad</button>
<button data-emo="confused"><span class="emoji">😕</span>Confused</button>
<button data-emo="laugh"><span class="emoji">😂</span>Laugh</button>
<button data-emo="wow"><span class="emoji">😮</span>Wow</button>
<button data-emo="excited"><span class="emoji">🤩</span>Excited</button>
<button data-emo="wink"><span class="emoji">😉</span>Wink</button>
<button data-emo="pleading"><span class="emoji">🥹</span>Pleading</button>
<button data-emo="nerd"><span class="emoji">🤓</span>Nerd</button>
<button data-emo="starstruck"><span class="emoji">🤩</span>Star Eyes</button>
<button data-emo="dizzy"><span class="emoji">😵</span>Dizzy</button>
<button data-emo="spiral"><span class="emoji">😵‍💫</span>Spiral</button>
<button data-emo="alien"><span class="emoji">👽</span>Alien</button>
<button data-emo="love"><span class="emoji">❤️</span>Love</button>
</div>
<h3 style="margin-top:30px;">Display Controls</h3>
<div class="display-controls">
  <button onclick="sendCommand('/display?m=flash')">
    DISPLAY FLASH
  </button>
  <button onclick="sendCommand('/display?m=sos')">
    DISPLAY SOS
  </button>
  <button onclick="sendCommand('/display?m=off')">
    DISPLAY NORMAL
  </button>
</div>
<div id="status">Ready</div>
<script>
async function sendCommand(url) {
  const status = document.getElementById('status');
  try {
    await fetch(url, { cache: 'no-store' });
    status.textContent = 'Command sent successfully';
  } catch(e) {
    status.textContent = 'Could not reach EVA. Check Wi-Fi.';
  }
}

document.querySelectorAll('[data-emo]').forEach(function(btn){
  btn.addEventListener('click', async function(){
    const emotion = btn.getAttribute('data-emo');
    const status = document.getElementById('status');
    try {
      await fetch('/emo?e=' + encodeURIComponent(emotion), {cache:'no-store'});
      status.textContent = 'Emotion: ' + emotion;
    } catch(e) {
      status.textContent = 'Could not reach EVA. Check Wi-Fi connection.';
    }
  });
});
</script>
</body>
</html>
)rawliteral";

// ESP-IDF HTTP server handlers
static bool getArg(httpd_req_t* req, const char* key, char* out, size_t outLen) {
  char query[64] = {0};
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) return false;
  return httpd_query_key_value(query, key, out, outLen) == ESP_OK;
}

static esp_err_t indexHandler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t emotionHandler(httpd_req_t* req) {
  char value[16] = {0};
  if (!getArg(req, "e", value, sizeof(value))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing emotion");
    return ESP_FAIL;
  }

  int code = -1;
  if      (!strcmp(value, "neutral"))  code = EMO_NEUTRAL;
  else if (!strcmp(value, "happy"))    code = EMO_HAPPY;
  else if (!strcmp(value, "angry"))    code = EMO_ANGRY;
  else if (!strcmp(value, "sleepy"))   code = EMO_SLEEPY;
  else if (!strcmp(value, "sad"))      code = EMO_SAD;
  else if (!strcmp(value, "confused")) code = EMO_CONFUSED;
  else if (!strcmp(value, "laugh"))    code = EMO_LAUGH;
  else if (!strcmp(value, "wow"))      code = EMO_SURPRISED;
  else if (!strcmp(value, "excited"))     code = EMO_EXCITED;
  else if (!strcmp(value, "wink"))         code = EMO_WINK;
  else if (!strcmp(value, "pleading"))     code = EMO_PLEADING;
  else if (!strcmp(value, "nerd"))         code = EMO_NERD;
  else if (!strcmp(value, "starstruck"))   code = EMO_STARSTRUCK;
  else if (!strcmp(value, "dizzy"))        code = EMO_DIZZY;
  else if (!strcmp(value, "spiral"))       code = EMO_SPIRAL;
  else if (!strcmp(value, "alien"))        code = EMO_ALIEN;
  else if (!strcmp(value, "love"))         code = EMO_LOVE;

  if (code < 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown emotion");
    return ESP_FAIL;
  }

  pendingEmotion = code;   // OLED/RoboEyes is updated from loop() only.
  lastActivityTime = millis();
  return httpd_resp_send(req, "OK", 2);
}

// ---------------- LED CONTROL HANDLER ----------------

static esp_err_t ledHandler(httpd_req_t* req) {
  char value[16] = {0};

  if (!getArg(req, "m", value, sizeof(value))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mode");
    return ESP_FAIL;
  }

  if (!strcmp(value, "toggle")) {
    pendingLedCommand = 1;
  }
  else if (!strcmp(value, "sos")) {
    pendingLedCommand = 2;
  }
  else {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown LED mode");
    return ESP_FAIL;
  }

  return httpd_resp_send(req, "OK", 2);
}


// ---------------- OLED DISPLAY CONTROL HANDLER ----------------

static esp_err_t displayHandler(httpd_req_t* req) {
  char value[16] = {0};

  if (!getArg(req, "m", value, sizeof(value))) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing mode");
    return ESP_FAIL;
  }

  if (!strcmp(value, "flash")) {
    pendingDisplayCommand = 1;
  }
  else if (!strcmp(value, "sos")) {
    pendingDisplayCommand = 2;
  }
  else if (!strcmp(value, "off")) {
    pendingDisplayCommand = 3;
  }
  else {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown display mode");
    return ESP_FAIL;
  }

  return httpd_resp_send(req, "OK", 2);
}

static void addUri(const char* uri, esp_err_t (*handler)(httpd_req_t*)) {
  httpd_uri_t route = {};
  route.uri = uri;
  route.method = HTTP_GET;
  route.handler = handler;
  route.user_ctx = nullptr;
  httpd_register_uri_handler(httpServer, &route);
}

void startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 4;
  config.stack_size = 6144;
  if (httpd_start(&httpServer, &config) == ESP_OK) {
    addUri("/", indexHandler);
    addUri("/emo", emotionHandler);
    addUri("/led", ledHandler);
    addUri("/display", displayHandler);
    Serial.println("Emotion web server started.");
  } else {
    Serial.println("ERROR: web server failed to start.");
  }
}

// ---------------- Custom sticker-style eye drawings ----------------
// Draw eyes only; no mouth or full emoji face.
bool isCustomEmotion(Emotion e) {
  return e == EMO_WINK || e == EMO_PLEADING || e == EMO_NERD ||
         e == EMO_STARSTRUCK || e == EMO_DIZZY || e == EMO_SPIRAL ||
         e == EMO_ALIEN || e == EMO_LOVE;
}

void drawHeart(int cx, int cy, int size) {
  int r = size / 2;
  display.fillCircle(cx - r / 2, cy - r / 3, r / 2 + 1, SH110X_WHITE);
  display.fillCircle(cx + r / 2, cy - r / 3, r / 2 + 1, SH110X_WHITE);
  display.fillTriangle(cx - size, cy, cx + size, cy, cx, cy + size, SH110X_WHITE);
}

void drawSpiralEye(int cx, int cy) {
  display.drawCircle(cx, cy, 13, SH110X_WHITE);
  display.drawCircle(cx + 2, cy, 8, SH110X_WHITE);
  display.drawCircle(cx + 3, cy + 1, 4, SH110X_WHITE);
  display.drawPixel(cx + 3, cy + 1, SH110X_BLACK);
}

void drawCustomEmotion() {
  if (!oledOK) return;
  display.clearDisplay();
  const int leftX = 39, rightX = 89, eyeY = 31;

  switch (currentEmotion) {
    case EMO_WINK:
      display.fillRoundRect(leftX - 13, eyeY - 14, 26, 28, 10, SH110X_WHITE);
      display.fillCircle(leftX, eyeY + 1, 5, SH110X_BLACK);
      display.fillCircle(leftX - 2, eyeY - 2, 2, SH110X_WHITE);
      display.drawLine(rightX - 14, eyeY + 2, rightX - 7, eyeY - 3, SH110X_WHITE);
      display.drawLine(rightX - 7, eyeY - 3, rightX, eyeY - 5, SH110X_WHITE);
      display.drawLine(rightX, eyeY - 5, rightX + 7, eyeY - 3, SH110X_WHITE);
      display.drawLine(rightX + 7, eyeY - 3, rightX + 14, eyeY + 2, SH110X_WHITE);
      break;
    case EMO_PLEADING:
      display.fillRoundRect(leftX - 14, eyeY - 17, 28, 34, 12, SH110X_WHITE);
      display.fillRoundRect(rightX - 14, eyeY - 17, 28, 34, 12, SH110X_WHITE);
      display.fillCircle(leftX, eyeY + 2, 7, SH110X_BLACK);
      display.fillCircle(rightX, eyeY + 2, 7, SH110X_BLACK);
      display.fillCircle(leftX - 2, eyeY - 2, 3, SH110X_WHITE);
      display.fillCircle(rightX - 2, eyeY - 2, 3, SH110X_WHITE);
      display.drawPixel(leftX - 12, eyeY + 20, SH110X_WHITE);
      display.drawPixel(rightX + 12, eyeY + 20, SH110X_WHITE);
      break;
    case EMO_NERD:
      display.drawRoundRect(leftX - 17, eyeY - 15, 34, 30, 7, SH110X_WHITE);
      display.drawRoundRect(rightX - 17, eyeY - 15, 34, 30, 7, SH110X_WHITE);
      display.drawLine(leftX + 17, eyeY - 5, rightX - 17, eyeY - 5, SH110X_WHITE);
      display.fillCircle(leftX, eyeY, 5, SH110X_WHITE);
      display.fillCircle(rightX, eyeY, 5, SH110X_WHITE);
      display.fillCircle(leftX + 1, eyeY, 2, SH110X_BLACK);
      display.fillCircle(rightX + 1, eyeY, 2, SH110X_BLACK);
      break;
    case EMO_STARSTRUCK:
      display.drawRoundRect(leftX - 15, eyeY - 16, 30, 32, 12, SH110X_WHITE);
      display.drawRoundRect(rightX - 15, eyeY - 16, 30, 32, 12, SH110X_WHITE);
      for (int cx : {leftX, rightX}) {
        display.drawLine(cx, eyeY - 10, cx, eyeY + 10, SH110X_WHITE);
        display.drawLine(cx - 9, eyeY, cx + 9, eyeY, SH110X_WHITE);
        display.drawLine(cx - 6, eyeY - 6, cx + 6, eyeY + 6, SH110X_WHITE);
        display.drawLine(cx + 6, eyeY - 6, cx - 6, eyeY + 6, SH110X_WHITE);
      }
      break;
    case EMO_DIZZY:
      display.drawCircle(leftX, eyeY, 14, SH110X_WHITE);
      display.drawCircle(rightX, eyeY, 14, SH110X_WHITE);
      display.drawLine(leftX - 7, eyeY - 7, leftX + 7, eyeY + 7, SH110X_WHITE);
      display.drawLine(leftX + 7, eyeY - 7, leftX - 7, eyeY + 7, SH110X_WHITE);
      display.drawLine(rightX - 7, eyeY - 7, rightX + 7, eyeY + 7, SH110X_WHITE);
      display.drawLine(rightX + 7, eyeY - 7, rightX - 7, eyeY + 7, SH110X_WHITE);
      break;
    case EMO_SPIRAL:
      drawSpiralEye(leftX, eyeY);
      drawSpiralEye(rightX, eyeY);
      break;
    case EMO_ALIEN:
      display.fillRoundRect(leftX - 12, eyeY - 20, 24, 40, 12, SH110X_WHITE);
      display.fillRoundRect(rightX - 12, eyeY - 20, 24, 40, 12, SH110X_WHITE);
      display.fillRoundRect(leftX - 6, eyeY - 12, 12, 28, 6, SH110X_BLACK);
      display.fillRoundRect(rightX - 6, eyeY - 12, 12, 28, 6, SH110X_BLACK);
      display.fillCircle(leftX - 2, eyeY - 5, 3, SH110X_WHITE);
      display.fillCircle(rightX - 2, eyeY - 5, 3, SH110X_WHITE);
      break;
    case EMO_LOVE:
      display.drawRoundRect(leftX - 15, eyeY - 16, 30, 32, 11, SH110X_WHITE);
      display.drawRoundRect(rightX - 15, eyeY - 16, 30, 32, 11, SH110X_WHITE);
      drawHeart(leftX, eyeY, 10);
      drawHeart(rightX, eyeY, 10);
      break;
    default: return;
  }
  display.display();
}

// ---------------- Eye setup and emotion behavior ----------------
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
  roboEyes.setPosition(DEFAULT);   // recenter after every emotion
  roboEyes.setIdleMode(OFF, 2, 2); // keep eyes centered while an emotion is selected
  roboEyes.setCuriosity(OFF);
  roboEyes.setAutoblinker(ON, 3, 2);
}

void setEmotion(Emotion emotion, unsigned long durationMs) {
  currentEmotion = emotion;
  emotionUntil = durationMs ? millis() + durationMs : 0;

  if (!oledOK) return;
  resetEyes();

  // Custom sticker expressions are drawn directly, not by RoboEyes.
  if (isCustomEmotion(emotion)) {
    roboEyes.setAutoblinker(OFF, 3, 2);
    drawCustomEmotion();
    return;
  }

  if (emotion != EMO_NEUTRAL) {
      roboEyes.setCuriosity(OFF);
  } else {
      roboEyes.setCuriosity(ON);
  }

  switch (emotion) {
    case EMO_NEUTRAL:
      roboEyes.setMood(DEFAULT);
      roboEyes.setIdleMode(OFF, 2, 2);
      break;

    case EMO_HAPPY:
      // Keep normal eye size; allow subtle curious left/right glances.
      roboEyes.setMood(HAPPY);
      roboEyes.setIdleMode(OFF, 2, 2);
      break;

    case EMO_ANGRY:
      roboEyes.setMood(ANGRY);
      break;

    case EMO_SLEEPY:
      // Sleepy face is drawn directly: two centered horizontal eyelid lines.
      // loop() animates small Z symbols without RoboEyes overwriting them.
      roboEyes.setAutoblinker(OFF, 3, 2);
      sleepStart = millis();
      break;

    case EMO_SAD:
      // Sad mood only; do not move the eyes to the bottom edge.
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
      roboEyes.setSpacebetween(10);
      break;

    case EMO_EXCITED:
      // Keep normal eye size, with subtle glances and excitement flicker.
      roboEyes.setMood(HAPPY);
      roboEyes.setIdleMode(OFF, 2, 2);
      roboEyes.setVFlicker(ON, 2);
      break;

    case EMO_WINK:
    case EMO_PLEADING:
    case EMO_NERD:
    case EMO_STARSTRUCK:
    case EMO_DIZZY:
    case EMO_SPIRAL:
    case EMO_ALIEN:
    case EMO_LOVE:
      // Drawn directly by drawCustomEmotion().
      break;
  }
}

void applyEmotionCode(int code) {
  switch ((Emotion)code) {
    case EMO_NEUTRAL:   setEmotion(EMO_NEUTRAL, 0); break;
    case EMO_HAPPY:     setEmotion(EMO_HAPPY, 4000); break;
    case EMO_ANGRY:     setEmotion(EMO_ANGRY, 4000); break;
    case EMO_SAD:       setEmotion(EMO_SAD, 4000); break;
    case EMO_SLEEPY:    setEmotion(EMO_SLEEPY, 0); break;
    case EMO_CONFUSED:  setEmotion(EMO_CONFUSED, 4000); break;
    case EMO_LAUGH:     setEmotion(EMO_LAUGH, 4000); break;
    case EMO_SURPRISED: setEmotion(EMO_SURPRISED, 4000); break;
    case EMO_EXCITED:   setEmotion(EMO_EXCITED, 4000); break;
    case EMO_WINK:      setEmotion(EMO_WINK, 4000); break;
    case EMO_PLEADING:  setEmotion(EMO_PLEADING, 4000); break;
    case EMO_NERD:      setEmotion(EMO_NERD, 4000); break;
    case EMO_STARSTRUCK:setEmotion(EMO_STARSTRUCK, 4000); break;
    case EMO_DIZZY:     setEmotion(EMO_DIZZY, 4000); break;
    case EMO_SPIRAL:    setEmotion(EMO_SPIRAL, 4000); break;
    case EMO_ALIEN:     setEmotion(EMO_ALIEN, 4000); break;
    case EMO_LOVE:      setEmotion(EMO_LOVE, 4000); break;
  }
}

void beepOnTouch(unsigned long now) {
  digitalWrite(BUZZER_PIN, HIGH);
  buzzerOffAt = now + 120; // short beep per touch
}

void handleTouch(unsigned long now) {
  bool touched = (digitalRead(TOUCH_PIN) == TOUCH_ACTIVE_LEVEL);

  if (touched && !lastTouchState) {
    lastActivityTime = now;
    beepOnTouch(now);

    if (now - touchWindowStart > 2000) {
      touchCount = 0;
      touchWindowStart = now;
    }
    touchCount++;

    if (touchCount >= 5) {
      touchCount = 0;
      touchWindowStart = now;
      setEmotion(EMO_ANGRY, 3000);
    } else {
      setEmotion(EMO_HAPPY, HAPPY_HOLD_TIME);
    }
  }

  // Touch-selected Happy remains active until another emotion is selected.
  // Keep the selected emotion active until another emotion is selected.
  if (touched && currentEmotion == EMO_HAPPY) {
      emotionUntil = now + HAPPY_HOLD_TIME;
      lastActivityTime = now;
  }
    lastTouchState = touched;
  }

void setupOLED() {
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);
  Wire.setTimeOut(50);
  delay(150);

  if (!display.begin(OLED_ADDR, true)) {
    Serial.println("WARNING: OLED initialization failed; check SDA/SCL and controller.");
    oledOK = false;
    return;
  }

  oledOK = true;
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(2);
  display.setCursor(38, 24);
  display.println("EVA");
  display.display();
  delay(400);

  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, EYES_FPS);
  roboEyes.setCuriosity(OFF);
  setEmotion(EMO_NEUTRAL, 0);
}

void setupWiFiAP() {
  WiFi.mode(WIFI_AP);            // AP-only: no STA/home-network connection
  WiFi.setSleep(false);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 4);
  apEnabled = ok;
  noClientsSince = millis();

  Serial.println(ok ? "EVA hotspot started." : "ERROR: EVA hotspot failed.");
  Serial.print("Wi-Fi name: "); Serial.println(AP_SSID);
  Serial.print("Password: "); Serial.println(AP_PASS);
  Serial.print("Open in browser: http://"); Serial.println(WiFi.softAPIP());
}

void manageHotspot(unsigned long now) {
  if (!apEnabled) return;

  // Any connected station keeps the hotspot alive and resets the timer.
  if (WiFi.softAPgetStationNum() > 0) {
    noClientsSince = now;
    return;
  }

  // If no station has connected since boot, the same 90-second timer applies.
  if (now - noClientsSince >= AP_NO_CLIENT_TIMEOUT) {
    Serial.println("No Wi-Fi clients for 90 seconds. Turning hotspot off until power cycle.");

    if (httpServer != nullptr) {
      httpd_stop(httpServer);
      httpServer = nullptr;
    }

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    apEnabled = false; // Never auto-restart; a power cycle starts it again.
  }
}

// ---------------- PROCESS LED COMMANDS ----------------
void processLedCommands(unsigned long now) {
  int command = pendingLedCommand;
  if (command == 0) return;
  pendingLedCommand = 0;
  if (command == 1) {
    // Toggle normal LED ON/OFF.
    if (ledMode == LED_ON || ledMode == LED_SOS) {
      ledMode = LED_OFF;
      digitalWrite(LED_PIN, LOW);
    }
    else {
      ledMode = LED_ON;
      digitalWrite(LED_PIN, HIGH);
    }
  }

  else if (command == 2) {
    // Toggle LED SOS mode.
    if (ledMode == LED_SOS) {
      ledMode = LED_OFF;
      digitalWrite(LED_PIN, LOW);
    }
    else {
      ledMode = LED_SOS;
      sosStep = 0;
      sosLastChange = now;
      digitalWrite(LED_PIN, HIGH);
    }
  }
}


// ---------------- PROCESS DISPLAY COMMANDS ----------------
void processDisplayCommands(unsigned long now) {
  int command = pendingDisplayCommand;
  if (command == 0) return;
  pendingDisplayCommand = 0;
  if (command == 1) {
    // Toggle entire OLED white.
    displayMode =
      (displayMode == DISPLAY_FLASH)
      ? DISPLAY_NORMAL
      : DISPLAY_FLASH;
  }
  else if (command == 2) {
    // Toggle OLED SOS.
    if (displayMode == DISPLAY_SOS) {
      displayMode = DISPLAY_NORMAL;
    }
    else {
      displayMode = DISPLAY_SOS;
      displaySOSWhite = true;
      displaySOSLastChange = now;
    }
  }
  else if (command == 3) {
    // Restore normal emotion display.
    displayMode = DISPLAY_NORMAL;
  }
}

// ---------------- OLED FULL SCREEN FLASH ----------------
void updateDisplayMode(unsigned long now) {
  if (!oledOK) return;

  if (displayMode == DISPLAY_FLASH) {
    display.fillScreen(SH110X_WHITE);
    display.display();
  }

  else if (displayMode == DISPLAY_SOS) {
    if (now - displaySOSLastChange >= 500) {
      displaySOSWhite = !displaySOSWhite;
      displaySOSLastChange = now;
    }

    if (displaySOSWhite) {
      display.fillScreen(SH110X_WHITE);
    }
    else {
      display.clearDisplay();
    }

    display.display();
  }
}

// ---------------- LED SOS ANIMATION ----------------
void updateLed(unsigned long now) {

  if (ledMode == LED_OFF) {
    digitalWrite(LED_PIN, LOW);
    return;
  }

  if (ledMode == LED_ON) {
    digitalWrite(LED_PIN, HIGH);
    return;
  }

  if (ledMode == LED_SOS) {

    if (now - sosLastChange >= SOS_PATTERN[sosStep]) {

      sosStep++;

      if (sosStep >= SOS_PATTERN_LENGTH) {
        sosStep = 0;
      }

      sosLastChange = now;

      digitalWrite(
        LED_PIN,
        (sosStep % 2 == 0) ? HIGH : LOW
      );
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n--- EVA ESP32-C3 Emotion Robot ---");

  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  lastActivityTime = millis();
  setupOLED();
  setupWiFiAP();
  startWebServer();

  Serial.println("Ready. Connect your phone to EVA and open the IP shown above.");
}

void loop() {
  unsigned long now = millis();

  manageHotspot(now);

  // Process LED and OLED commands
  processLedCommands(now);
  processDisplayCommands(now);

  // Keep LED animations non-blocking
  updateLed(now);

  // Apply web-requested emotions in the main loop only.
  int requested = pendingEmotion;
  if (requested >= 0) {
    pendingEmotion = -1;
    applyEmotionCode(requested);
  }

  handleTouch(now);

  if (buzzerOffAt != 0 && now >= buzzerOffAt) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerOffAt = 0;
  }

  if (emotionUntil != 0 && now >= emotionUntil) {
      if (currentEmotion == EMO_HAPPY ||
          currentEmotion == EMO_ANGRY ||
          currentEmotion == EMO_CONFUSED ||
          currentEmotion == EMO_LAUGH ||
          currentEmotion == EMO_SURPRISED ||
          currentEmotion == EMO_EXCITED) {
  
          setEmotion(EMO_NEUTRAL, 0);
          lastActivityTime = now;
  
      } else if (currentEmotion == EMO_SAD) {
          // Sad finishes, then Sleepy starts.
          setEmotion(EMO_SLEEPY, 0);
          lastActivityTime = now;
      }
  }
  
  if (currentEmotion == EMO_NEUTRAL &&
      now - lastActivityTime >= SLEEPY_TIMEOUT) {
      setEmotion(EMO_SAD, 4000);
  }

  if (oledOK && displayMode == DISPLAY_NORMAL &&
    currentEmotion == EMO_SLEEPY) {
    // Repaint the whole sleepy face each frame so the Z animation remains visible.
    display.clearDisplay();
    display.drawLine(24, 32, 56, 32, SH110X_WHITE);
    display.drawLine(72, 32, 104, 32, SH110X_WHITE);

    // Animated Z / z marks drift upward on the right side.
    uint8_t phase = (now / 350) % 3;
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    // Keep Z above the right eye, away from the horizontal line.
    int zX = 88 - (int)phase * 3;
    int zY = 5 + (int)phase * 3;
    display.setCursor(zX, zY);
    display.print(phase == 1 ? "Z" : "z");
    display.display();
  } else if (oledOK && displayMode != DISPLAY_NORMAL) {
      // Flash/SOS overrides all eye animations.
      updateDisplayMode(now);
  } else if (oledOK) {

    if (isCustomEmotion(currentEmotion)) {
      // Keep custom sticker-style eyes on screen; RoboEyes must not overwrite them.
      // Redraw spiral eyes for a small animated feel.
      if (currentEmotion == EMO_SPIRAL && (now % 250) < 25) {
        drawCustomEmotion();
      }
    } else {
      roboEyes.update();
    }
  }
  delay(1);

  // Subtle gaze movement during normal eye emotions.
  // Skip custom gaze animation while Sleepy is drawn directly.
  if (oledOK && currentEmotion != EMO_SLEEPY && !isCustomEmotion(currentEmotion)) {
      if (now - lastGazeChange >= 1800) {
          lastGazeChange = now;
          gazeDirection++;
  
          if (gazeDirection > 2) {
              gazeDirection = 0;
          }
  
          if (currentEmotion != EMO_NEUTRAL) {
             // Other emotions: look left, right, and center.
              roboEyes.setPosition(DEFAULT);
              roboEyes.setCuriosity(OFF);
          } else {
              // Neutral: eyes stay centered; no gaze animation.
              roboEyes.setCuriosity(ON);
          
              if (gazeDirection == 0) {
                  roboEyes.setPosition(DEFAULT);
              } else if (gazeDirection == 1) {
                  roboEyes.setPosition(W);
              } else {
                  roboEyes.setPosition(E);
              }
          }
      }
  }
}