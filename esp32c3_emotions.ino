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
  EMO_EXCITED
};

volatile int pendingEmotion = -1;
Emotion currentEmotion = EMO_NEUTRAL;
unsigned long emotionUntil = 0;
unsigned long sleepStart = 0;
unsigned long lastActivityTime = 0;
const unsigned long SLEEPY_TIMEOUT = 10000;
const unsigned long HAPPY_HOLD_TIME = 1500;

bool eyesClosed = false;
bool lastTouchState = false;
unsigned long touchWindowStart = 0;
uint8_t touchCount = 0;
unsigned long buzzerOffAt = 0;

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
</style>
</head>
<body>
<h2>🤖 EVA Emotions</h2>
<p>Connect to the EVA Wi-Fi hotspot, then choose an emotion.</p>
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
</div>
<div id="status">Ready</div>
<script>
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
  else if (!strcmp(value, "excited"))  code = EMO_EXCITED;

  if (code < 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown emotion");
    return ESP_FAIL;
  }

  pendingEmotion = code;   // OLED/RoboEyes is updated from loop() only.
  lastActivityTime = millis();
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
    Serial.println("Emotion web server started.");
  } else {
    Serial.println("ERROR: web server failed to start.");
  }
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
  roboEyes.setPosition(DEFAULT);   // reset any previous directional gaze
  roboEyes.setIdleMode(OFF, 2, 2); // prevent gaze drifting and sticking at an edge
  roboEyes.setAutoblinker(ON, 3, 2);
}

void setEmotion(Emotion emotion, unsigned long durationMs) {
  currentEmotion = emotion;
  emotionUntil = durationMs ? millis() + durationMs : 0;

  if (!oledOK) return;
  resetEyes();

  switch (emotion) {
    case EMO_NEUTRAL:
      roboEyes.setMood(DEFAULT);
      roboEyes.setIdleMode(ON, 2, 2); // occasional left/right glances; curiosity can accent one eye
      break;

    case EMO_HAPPY:
      // Keep normal eye size; allow subtle curious left/right glances.
      roboEyes.setMood(HAPPY);
      roboEyes.setIdleMode(ON, 2, 2);
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
      roboEyes.setIdleMode(ON, 2, 2);
      roboEyes.setVFlicker(ON, 2);
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
    case EMO_CONFUSED:  setEmotion(EMO_CONFUSED, 3000); break;
    case EMO_LAUGH:     setEmotion(EMO_LAUGH, 2500); break;
    case EMO_SURPRISED: setEmotion(EMO_SURPRISED, 3000); break;
    case EMO_EXCITED:   setEmotion(EMO_EXCITED, 4000); break;
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

    if (touchCount >= 3) {
      touchCount = 0;
      touchWindowStart = now;
      setEmotion(EMO_LAUGH, 2500);
    } else {
      setEmotion(EMO_HAPPY, HAPPY_HOLD_TIME);
    }
  }

  // While held, keep Happy on screen rather than allowing its timer to expire.
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
  roboEyes.setCuriosity(ON);
  setEmotion(EMO_NEUTRAL, 0);
}

void setupWiFiAP() {
  WiFi.mode(WIFI_AP);            // AP-only: no STA/home-network connection
  WiFi.setSleep(false);
  bool ok = WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 4);

  Serial.println(ok ? "EVA hotspot started." : "ERROR: EVA hotspot failed.");
  Serial.print("Wi-Fi name: "); Serial.println(AP_SSID);
  Serial.print("Password: "); Serial.println(AP_PASS);
  Serial.print("Open in browser: http://"); Serial.println(WiFi.softAPIP());
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n--- EVA ESP32-C3 Emotion Robot ---");

  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  lastActivityTime = millis();
  setupOLED();
  setupWiFiAP();
  startWebServer();

  Serial.println("Ready. Connect your phone to EVA and open the IP shown above.");
}

void loop() {
  unsigned long now = millis();

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
    setEmotion(EMO_NEUTRAL, 0);
    lastActivityTime = now;
  }

  if (currentEmotion == EMO_NEUTRAL &&
      now - lastActivityTime >= SLEEPY_TIMEOUT) {
    setEmotion(EMO_SLEEPY, 0);
  }

  if (oledOK && currentEmotion == EMO_SLEEPY && !eyesClosed &&
      now - sleepStart > 4000) {
    roboEyes.close();
    eyesClosed = true;
  }

  if (oledOK) roboEyes.update();
  delay(1);
}