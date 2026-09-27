#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <FluxGarage_RoboEyes.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 8
#define OLED_SCL 9
#define OLED_ADDR 0x3C

#define TOUCH_PIN 5           // Touch sensor connected to GPIO5
#define SLEEPY_TIMEOUT 10000  // 10 seconds of no touch -> sleepy face
#define HAPPY_HOLD_TIME 1500  // how long to stay happy after a touch (ms)

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
RoboEyes<Adafruit_SH1106G> roboEyes(display);   // pass your display into the constructor

// ---- Mood state machine ----
enum MoodState { STATE_NORMAL, STATE_SLEEPY, STATE_HAPPY };
MoodState currentState = STATE_NORMAL;

bool lastTouchState = LOW;
unsigned long lastActivityTime = 0;   // resets whenever we go back to NORMAL
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

  // Start in normal/default mood
  roboEyes.setMood(DEFAULT);
  roboEyes.setPosition(DEFAULT);
  lastActivityTime = millis();
}

void loop() {
  handleTouchMoods();
  roboEyes.update();
}

void handleTouchMoods() {
  bool touchState = digitalRead(TOUCH_PIN);

  // Rising edge: sensor just got touched -> go HAPPY immediately
  if (touchState == HIGH && lastTouchState == LOW) {
    currentState = STATE_HAPPY;
    happyStartTime = millis();
    roboEyes.setMood(HAPPY);
  }
  lastTouchState = touchState;

  switch (currentState) {
    case STATE_HAPPY:
      // Stay happy for a bit after the touch, then go back to normal
      if (millis() - happyStartTime >= HAPPY_HOLD_TIME && touchState == LOW) {
        currentState = STATE_NORMAL;
        roboEyes.setMood(DEFAULT);
        lastActivityTime = millis();  // restart the 10s sleepy countdown
      }
      break;

    case STATE_NORMAL:
      // If untouched for SLEEPY_TIMEOUT, go sleepy
      if (millis() - lastActivityTime >= SLEEPY_TIMEOUT) {
        currentState = STATE_SLEEPY;
        roboEyes.setMood(TIRED);
      }
      break;

    case STATE_SLEEPY:
      // Stays sleepy until next touch triggers HAPPY (handled above on edge)
      break;
  }
}