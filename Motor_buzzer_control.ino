#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <FluxGarage_RoboEyes.h>

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 8
#define OLED_SCL 9
#define OLED_ADDR 0x3C
bool oledOK = false;

// TOUCH SENSOR
#define TOUCH_PIN 5
#define SLEEPY_TIMEOUT 10000
#define HAPPY_HOLD_TIME 1500

// L298N MINI MOTOR DRIVER
#define IN1 14
#define IN2 17
#define IN3 15
#define IN4 16

// BUZZER
#define BUZZER 18

// MOTOR FAILSAFE
#define CMD_TIMEOUT 800

// WIFI Credentials
const char* AP_SSID = "EVA";
const char* AP_PASS = "88888888";
WebServer server(80);

// OLED OBJECT
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
RoboEyes<Adafruit_SH1106G> roboEyes(display);

// EMOTION STATES
enum MoodState {STATE_NORMAL, STATE_SLEEPY, STATE_HAPPY};
MoodState currentState = STATE_NORMAL;

// TOUCH VARIABLES
bool lastTouchState = LOW;
unsigned long lastActivityTime = 0;
unsigned long happyStartTime = 0;

// MOTOR VARIABLES
unsigned long lastCmdTime = 0;
bool moving = false;
bool hornOn = false;

// MOBILE WEB PAGE
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>EVA Robot</title>

<style>
* {
  box-sizing: border-box;
}

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

h2 {
  margin-top: 10px;
  margin-bottom: 5px;
}

.grid {
  display: grid;
  grid-template-columns:
  repeat(3, 1fr);
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

button:active {
  transform: scale(0.95);
  background: #173e8f;
}

.stop {
  background: #d33;
}

.stop:active {
  background: #8b1f1f;
}

.horn {
  width: 100%;
  max-width: 330px;
  margin-top: 20px;
  background: #d99b00;
  font-size: 24px;
}

.horn:active {
  background: #8d6800;
}

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
    <button data-command="F">Forward</button>
  <div></div>
    <button data-command="L">Left</button>
    <button id="stop" class="stop">Stop</button>
    <button data-command="R">Right</button>
  <div></div>
    <button data-command="B">Backward</button>
  <div></div>
</div>

<button id="horn" class="horn">HORN</button>

<div class="info">
  Hold direction button to move.<br>
  Release button to stop.
</div>


<script>
let commandTimer = null;
let currentCommand = "S";

// SEND MOVEMENT
function sendCommand(command) {
  fetch("/move?d=" + command).catch(function() {});
}

// START MOVEMENT
function startMovement(command) {
  currentCommand = command;
  sendCommand(command);
  clearInterval(commandTimer);
  commandTimer = setInterval(
    function() {
      sendCommand(currentCommand);
    }, 250
  );
}

// STOP MOVEMENT
function stopMovement() {
  clearInterval(commandTimer);
  commandTimer = null;
  currentCommand = "S";
  sendCommand("S");
}

// =================================================
// MOVEMENT BUTTONS
// =================================================
document.querySelectorAll("[data-command]").forEach(function(button) {

  const command = button.getAttribute("data-command");

  // TOUCH START
  button.addEventListener("touchstart", function(event) {
      event.preventDefault();
      startMovement(command);
    }, { passive: false }
  );


  // TOUCH END
  button.addEventListener("touchend", function(event) {
      event.preventDefault();
      stopMovement();
    }, { passive: false }
  );


  // TOUCH CANCEL
  button.addEventListener("touchcancel", function(event) {
      event.preventDefault();
      stopMovement();
    }, { passive: false }
  );


  // MOUSE
  button.addEventListener("mousedown", function() {
      startMovement(command);
    }
  );
  button.addEventListener("mouseup", function() {
      stopMovement();
    }
  );
  button.addEventListener("mouseleave", function() {
      stopMovement();
    }
  );
});


// STOP BUTTON
document.getElementById("stop").addEventListener("click", function() {
    stopMovement();
  }
);


// =================================================
// HORN
// =================================================
const horn = document.getElementById("horn");

// TOUCH START
horn.addEventListener("touchstart", function(event) {
    event.preventDefault();
    fetch("/horn?s=1").catch(function() {});
  }, { passive: false }
);

// TOUCH END
horn.addEventListener("touchend", function(event) {
    event.preventDefault();
    fetch("/horn?s=0").catch(function() {});
  }, { passive: false }
);


// MOUSE
horn.addEventListener("mousedown", function() {
    fetch("/horn?s=1").catch(function() {});
  }
);

horn.addEventListener("mouseup", function() {
    fetch("/horn?s=0").catch(function() {});
  }
);

</script>
</body>
</html>)rawliteral";

// =====================================================
// MOTOR STOP
// =====================================================
void motorsStop() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  moving = false;
}

// =====================================================
// MOTOR DRIVE
// =====================================================
void drive(bool left1, bool left2, bool right1, bool right2) {
  digitalWrite(IN1, left1);
  digitalWrite(IN2, left2);
  digitalWrite(IN3, right1);
  digitalWrite(IN4, right2);
  moving = true;
}

// =====================================================
// WAKE UP
// =====================================================
void wakeUp() {
  lastActivityTime = millis();
  if (currentState == STATE_SLEEPY) {
    currentState = STATE_NORMAL;
    if (oledOK) {
      roboEyes.setMood(DEFAULT);
    }
  }
}

// =====================================================
// MOVE HANDLER
// =====================================================
void handleMove() {
  String d = server.arg("d");
  lastCmdTime = millis();
  wakeUp();

  // FORWARD
  if (d == "F") {
    drive(HIGH, LOW, HIGH, LOW);
    if (oledOK) {
      roboEyes.setIdleMode(OFF, 2, 2);
      roboEyes.setPosition(N);
    }
  }

  // BACKWARD
  else if (d == "B") {
    drive(LOW, HIGH, LOW, HIGH);
    if (oledOK) {
      roboEyes.setIdleMode(OFF, 2, 2);
      roboEyes.setPosition(S);
    }
  }

  // LEFT
  else if (d == "L") {
    drive(LOW, HIGH, HIGH, LOW);
    if (oledOK) {
      roboEyes.setIdleMode(OFF, 2, 2);
      roboEyes.setPosition(W);
    }
  }

  // RIGHT
  else if (d == "R") {
    drive(HIGH, LOW, LOW, HIGH);
    if (oledOK) {
      roboEyes.setIdleMode(OFF, 2, 2);
      roboEyes.setPosition(E);
    }
  }
  // STOP
  else {
    motorsStop();
    if (oledOK) {
      roboEyes.setPosition(DEFAULT);
      roboEyes.setIdleMode(ON, 2, 2);
    }
  }
  server.send(200, "text/plain", "OK");
}


// =====================================================
// HORN HANDLER
// =====================================================
void handleHorn() {
  hornOn = server.arg("s") == "1";

  digitalWrite(
    BUZZER,
    hornOn ? HIGH : LOW
  );
  if (hornOn) {
    wakeUp();
    if (oledOK && currentState != STATE_HAPPY) {
      roboEyes.setMood(ANGRY);
    }
  }
  else {
    if (oledOK && currentState == STATE_NORMAL) {
      roboEyes.setMood(DEFAULT);
    }
  }
  server.send(200, "text/plain", "OK");
}


// TOUCH + EMOTIONS
void handleTouchMoods() {
      bool touchState = digitalRead(TOUCH_PIN);
      // NEW TOUCH
      if (touchState == HIGH && lastTouchState == LOW) {
          currentState = STATE_HAPPY;
          happyStartTime = millis();
          lastActivityTime = millis();
          if (oledOK) {
            roboEyes.setMood(HAPPY);
          }
      }
      lastTouchState = touchState;
      // STATE MACHINE
      switch (currentState) {
        // HAPPY
        case STATE_HAPPY:
          if (millis() - happyStartTime >= HAPPY_HOLD_TIME && touchState == LOW) 
            {
              currentState = STATE_NORMAL;
              if (oledOK) {
                roboEyes.setMood(DEFAULT);
              }
              lastActivityTime = millis();
            }
          break;
        // NORMAL
        case STATE_NORMAL:
          if (!moving && !hornOn && millis() - lastActivityTime >= SLEEPY_TIMEOUT) 
            {
              currentState = STATE_SLEEPY;
              if (oledOK) {
                roboEyes.setMood(TIRED);
              }
            }
          break;
        // SLEEPY
        case STATE_SLEEPY:
          break;
      }
    }


// =====================================================
// OLED INITIALIZATION
// =====================================================
void setupOLED() {
  Serial.println("Starting OLED...");
  Wire.begin(OLED_SDA, OLED_SCL);

  delay(100);

  oledOK = display.begin(OLED_ADDR, true);
  if (!oledOK) {
      Serial.println("OLED initialization FAILED!");
      Serial.println("Continuing without OLED...");
      return;
    }
  Serial.println("OLED initialization SUCCESS!");


  // ROBO EYES
  roboEyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 100);
  roboEyes.setAutoblinker(ON, 3, 2);
  roboEyes.setIdleMode(ON, 2, 2);
  roboEyes.setMood(DEFAULT);
  roboEyes.setPosition(DEFAULT);

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0 );
  display.println("EVA ROBOT");
  display.setCursor(0, 15);
  display.println("Starting...");
  display.display();

  delay(500);
}


// =====================================================
// WIFI INITIALIZATION
// =====================================================
void setupWiFi() {
  Serial.println();
  Serial.println("Starting WiFi Access Point...");

  WiFi.mode(WIFI_AP);

  delay(500);

  bool result = WiFi.softAP(AP_SSID, AP_PASS);
  if (result) {
    Serial.println("WIFI AP STARTED SUCCESSFULLY!");
  }
  else {
    Serial.println("WIFI AP FAILED!");
  }
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASS);
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("MAC Address: ");
  Serial.println(WiFi.softAPmacAddress());


  // =================================================
  // WEB SERVER
  // =================================================
  server.on("/", []() {
      server.send_P(200, "text/html", INDEX_HTML);
    }
  );

  server.on("/move", handleMove);

  server.on("/horn", handleHorn);

  server.begin();

  Serial.println("Web server started!");
  Serial.println();
  Serial.println("================================");
  Serial.println("Connect phone to:");
  Serial.println("SSID: EVA");
  Serial.println("Password: 88888888");
  Serial.println();
  Serial.println("Open:");
  Serial.println("http://192.168.4.1");
  Serial.println("================================");
}


// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("       EVA ROBOT START");
  Serial.println("================================");

  // MOTOR GPIO
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  motorsStop();

  // BUZZER
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);

  // TOUCH
  pinMode(TOUCH_PIN, INPUT);

  // OLED
  setupOLED();

  // ACTIVITY TIMER
  lastActivityTime = millis();

  // WIFI
  setupWiFi();

  Serial.println();
  Serial.println("EVA ROBOT READY!");
}


// =====================================================
// LOOP
// =====================================================
void loop() {
  // WEB SERVER
  server.handleClient();

  // MOTOR FAILSAFE
  if (moving && millis() - lastCmdTime > CMD_TIMEOUT) {
        motorsStop();
        if (oledOK) {
          roboEyes.setPosition(DEFAULT);
          roboEyes.setIdleMode(ON, 2, 2);
        }
      }

  // TOUCH / EMOTIONS
  handleTouchMoods();

  // ROBO EYES UPDATE
  if (oledOK) {
    roboEyes.update();
  }
}