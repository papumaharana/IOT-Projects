#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <FluxGarage_RoboEyes.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDR 0x3C

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
RoboEyes<Adafruit_SH1106G> roboEyes(display);   // pass your display into the constructor

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);
  delay(100);

  if (!display.begin(OLED_ADDR, true)) {
    while (true);
  }

  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100);
  roboEyes.setAutoblinker(ON, 3, 2);
  roboEyes.setIdleMode(ON, 2, 2);
}

void loop() {
  roboEyes.update();
  // ---------------- MOOD EXPRESSIONS ----------------
  // DEFAULT mood
  roboEyes.setMood(DEFAULT);
  roboEyes.setPosition(DEFAULT);
  holdAndUpdate(2000);

  // HAPPY mood
  roboEyes.setMood(HAPPY);
  holdAndUpdate(2000);

  // TIRED mood
  roboEyes.setMood(TIRED);
  holdAndUpdate(2000);

  // ANGRY mood
  roboEyes.setMood(ANGRY);
  holdAndUpdate(2000);

  // back to DEFAULT before position demo
  roboEyes.setMood(DEFAULT);

  // ---------------- EYE POSITIONS (cardinal directions) ----------------
  roboEyes.setPosition(N);   // looking up
  holdAndUpdate(800);

  roboEyes.setPosition(NE);  // looking up-right
  holdAndUpdate(800);

  roboEyes.setPosition(E);   // looking right
  holdAndUpdate(800);

  roboEyes.setPosition(SE);  // looking down-right
  holdAndUpdate(800);

  roboEyes.setPosition(S);   // looking down
  holdAndUpdate(800);

  roboEyes.setPosition(SW);  // looking down-left
  holdAndUpdate(800);

  roboEyes.setPosition(W);   // looking left
  holdAndUpdate(800);

  roboEyes.setPosition(NW);  // looking up-left
  holdAndUpdate(800);

  roboEyes.setPosition(DEFAULT); // centered again
  holdAndUpdate(800);

  // ---------------- ONE-SHOT ANIMATIONS ----------------
  // Laughing animation (eyes shake up and down) - looks best with HAPPY mood
  roboEyes.setMood(HAPPY);
  roboEyes.anim_laugh();
  holdAndUpdate(1500);

  // Confused animation (eyes shake left and right)
  roboEyes.setMood(DEFAULT);
  roboEyes.anim_confused();
  holdAndUpdate(1500);

  // Manual blink - both eyes
  roboEyes.blink();
  holdAndUpdate(500);

  // Manual blink - right eye only (blink(leftEye, rightEye))
  roboEyes.blink(0, 1);
  holdAndUpdate(500);

  // Manual blink - left eye only
  roboEyes.blink(1, 0);
  holdAndUpdate(500);

  // ---------------- OPEN / CLOSE ----------------
  // Close both eyes
  roboEyes.close();
  holdAndUpdate(700);

  // Open both eyes again
  roboEyes.open();
  holdAndUpdate(700);

  // Close left eye only (close(leftEye, rightEye))
  roboEyes.close(1, 0);
  holdAndUpdate(700);
  roboEyes.open(1, 0);
  holdAndUpdate(500);

  // Close right eye only
  roboEyes.close(0, 1);
  holdAndUpdate(700);
  roboEyes.open(0, 1);
  holdAndUpdate(500);

  // ---------------- CURIOSITY MODE ----------------
  // When ON, outer eye grows taller as eyes move to far left/right
  roboEyes.setCuriosity(ON);
  roboEyes.setPosition(W);
  holdAndUpdate(1000);
  roboEyes.setPosition(E);
  holdAndUpdate(1000);
  roboEyes.setCuriosity(OFF);
  roboEyes.setPosition(DEFAULT);

  // ---------------- SWEAT (nervous look) ----------------
  roboEyes.setSweat(ON);
  holdAndUpdate(2000);
  roboEyes.setSweat(OFF);

  // ---------------- CYCLOPS MODE (single eye) ----------------
  roboEyes.setCyclops(ON);
  roboEyes.setMood(DEFAULT);
  holdAndUpdate(1500);
  roboEyes.setCyclops(OFF);

  // ---------------- HORIZONTAL / VERTICAL FLICKER ----------------
  // Horizontal flicker (glitch-like side-to-side displacement)
  roboEyes.setHFlicker(ON, 4);
  holdAndUpdate(1500);
  roboEyes.setHFlicker(OFF, 0);

  // Vertical flicker (glitch-like up-down displacement)
  roboEyes.setVFlicker(ON, 4);
  holdAndUpdate(1500);
  roboEyes.setVFlicker(OFF, 0);

  delay(1000);
}

// Helper: keep calling update() for a given duration so autoblinker/idle/flicker
// transitions render smoothly instead of a single static frame.
void holdAndUpdate(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    roboEyes.update();
  }
}