/*
 * DeskBuddy_Touch.ino
 * ─────────────────────────────────────────────────────────────────
 *  Hardware:
 *    - Generic ESP32 development board
 *    - SH1106 128×64 OLED  →  SDA = GPIO 21,  SCL = GPIO 22
 *    - Touch sensor (capacitive / TTP223 etc.) → GPIO 5 (INPUT)
 *
 *  Libraries required (install via Arduino Library Manager):
 *    1. Adafruit GFX Library
 *    2. Adafruit SH110X
 *    3. FluxGarage_RoboEyes
 *
 *  Behaviour:
 *    - Each touch cycles through the mood sequence:
 *        DEFAULT → HAPPY → TIRED → ANGRY → (back to DEFAULT)
 *    - Eyes animate continuously (auto-blink + idle wander)
 *    - Serial monitor (115200 baud) shows the current mood name
 * ─────────────────────────────────────────────────────────────────
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <FluxGarage_RoboEyes.h>

// ── Display config (copied from DemoExpressions) ─────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_SDA       21
#define OLED_SCL       22
#define OLED_ADDR    0x3C

// ── Touch sensor ──────────────────────────────────────────────────
#define TOUCH_PIN       5     // GPIO connected to touch sensor output
#define TOUCH_HIGH      HIGH  // Set to LOW if your sensor is active-low

// ── Display & RoboEyes objects ────────────────────────────────────
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
RoboEyes<Adafruit_SH1106G> roboEyes(display);

// ── Mood cycling ──────────────────────────────────────────────────
// Ordered list of moods to cycle through on each touch
const uint8_t MOODS[]    = { DEFAULT, HAPPY, TIRED, ANGRY };
const char*   MOOD_NAMES[] = { "DEFAULT", "HAPPY", "TIRED", "ANGRY" };
const uint8_t MOOD_COUNT   = sizeof(MOODS) / sizeof(MOODS[0]);
uint8_t currentMoodIndex   = 0;

// ── Touch debounce ────────────────────────────────────────────────
bool     lastTouchState    = false;   // was sensor active last loop?
uint32_t lastDebounceTime  = 0;
const uint32_t DEBOUNCE_MS = 50;      // ms to wait before accepting a new touch

// ── One-shot animation flag ───────────────────────────────────────
bool playAnim = false;

// ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== DeskBuddy Touch ===");

  // Initialise I²C with the chosen SDA/SCL pins
  Wire.begin(OLED_SDA, OLED_SCL);
  delay(100);

  // Initialise the SH1106 display
  if (!display.begin(OLED_ADDR, true)) {
    Serial.println("ERROR: SH1106 not found – check wiring!");
    while (true) { delay(1000); }
  }

  // Initialise RoboEyes (width, height, frame-rate limit)
  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 60);
  roboEyes.setAutoblinker(ON, 3, 2);   // auto-blink every ~3 s ±2 s
  roboEyes.setIdleMode(ON, 2, 2);      // random gaze wander

  // Apply the starting mood
  roboEyes.setMood(MOODS[currentMoodIndex]);
  roboEyes.setPosition(DEFAULT);

  // Touch sensor pin
  pinMode(TOUCH_PIN, INPUT);

  Serial.print("Starting mood: ");
  Serial.println(MOOD_NAMES[currentMoodIndex]);
}

// ─────────────────────────────────────────────────────────────────
void loop() {
  // ── 1. Read & debounce the touch sensor ─────────────────────────
  bool touchNow = (digitalRead(TOUCH_PIN) == TOUCH_HIGH);

  if (touchNow != lastTouchState) {
    lastDebounceTime = millis();          // reset debounce timer on change
    lastTouchState = touchNow;
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
    // Stable HIGH → rising edge (finger just placed)
    if (touchNow && !playAnim) {
      playAnim = true;                    // flag: trigger one-shot anim once

      // Advance to the next mood
      currentMoodIndex = (currentMoodIndex + 1) % MOOD_COUNT;
      roboEyes.setMood(MOODS[currentMoodIndex]);
      roboEyes.setPosition(DEFAULT);

      Serial.print("Touch! Mood → ");
      Serial.println(MOOD_NAMES[currentMoodIndex]);

      // Play a one-shot animation that fits the new mood
      switch (MOODS[currentMoodIndex]) {
        case HAPPY:
          roboEyes.anim_laugh();
          break;
        case TIRED:
          roboEyes.close();               // droopy eyes – reopen after brief hold
          break;
        case ANGRY:
          roboEyes.anim_confused();
          break;
        default:  // DEFAULT
          roboEyes.blink();
          break;
      }
    }
  }

  // Clear the one-shot flag once the sensor is released
  if (!touchNow) {
    playAnim = false;
  }

  // ── 2. Keep RoboEyes animated every loop ────────────────────────
  roboEyes.update();

  // Re-open eyes after TIRED close (brief blocking hold, ~700 ms)
  // We do this lazily – just let autoblink handle the re-open via update().
  // If you want instant re-open, call roboEyes.open() after a delay.
}
