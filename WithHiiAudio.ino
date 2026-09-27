#include <Wire.h>
#include "FS.h"
#include "SD_MMC.h"
#include <Audio.h>              // ESP32-audioI2S library (schreibfaul1) -- MUST come before RoboEyes
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <FluxGarage_RoboEyes.h>  // defines macros N and E -- keep this LAST

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 8
#define OLED_SCL 9
#define OLED_ADDR 0x3C

#define TOUCH_PIN 5           // Touch sensor connected to GPIO5
#define SLEEPY_TIMEOUT 10000  // 10 seconds of no touch -> sleepy face
#define HAPPY_HOLD_TIME 1500  // how long to stay happy after a touch (ms)

// ---- I2S (MAX98357A) pins ----
#define I2S_BCLK 4
#define I2S_LRC  6
#define I2S_DOUT 7

// Path to your audio file on the onboard microSD slot (use .mp3 or .wav, not .mp4)
const char* HAPPY_SOUND = "/esp32audio/hello.mp3";

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
RoboEyes<Adafruit_SH1106G> roboEyes(display);
Audio audio;

// ---- Mood state machine ----
enum MoodState { STATE_NORMAL, STATE_SLEEPY, STATE_HAPPY };
MoodState currentState = STATE_NORMAL;

bool lastTouchState = LOW;
unsigned long lastActivityTime = 0;
unsigned long happyStartTime = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);
  delay(100);

  pinMode(TOUCH_PIN, INPUT);

  if (!display.begin(OLED_ADDR, true)) {
    while (true);
  }

  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100);
  roboEyes.setAutoblinker(ON, 3, 2);
  roboEyes.setIdleMode(ON, 2, 2);
  roboEyes.setMood(DEFAULT);
  roboEyes.setPosition(DEFAULT);
  lastActivityTime = millis();

  // ---- Onboard microSD slot init (SD_MMC, auto pins) ----
  if (!SD_MMC.begin()) {
    Serial.println("SD_MMC Mount Failed! Check card / pins.");
  } else {
    Serial.println("SD_MMC ready.");
  }

  // ---- I2S audio init ----
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(18); // range 0-21
}

void loop() {
  handleTouchMoods();
  roboEyes.update();
  audio.loop();  // must be called continuously to stream audio
}

void handleTouchMoods() {
  bool touchState = digitalRead(TOUCH_PIN);

  // Rising edge: sensor just got touched -> go HAPPY immediately + play sound
  if (touchState == HIGH && lastTouchState == LOW) {
    currentState = STATE_HAPPY;
    happyStartTime = millis();
    roboEyes.setMood(HAPPY);
    audio.connecttoFS(SD_MMC, HAPPY_SOUND);
  }
  lastTouchState = touchState;

  switch (currentState) {
    case STATE_HAPPY:
      if (millis() - happyStartTime >= HAPPY_HOLD_TIME && touchState == LOW) {
        currentState = STATE_NORMAL;
        roboEyes.setMood(DEFAULT);
        lastActivityTime = millis();
      }
      break;

    case STATE_NORMAL:
      if (millis() - lastActivityTime >= SLEEPY_TIMEOUT) {
        currentState = STATE_SLEEPY;
        roboEyes.setMood(TIRED);
      }
      break;

    case STATE_SLEEPY:
      break;
  }
}

// Required by ESP32-audioI2S library, prints info/errors to Serial
void audio_info(const char *info) {
  Serial.print("audio_info: ");
  Serial.println(info);
}