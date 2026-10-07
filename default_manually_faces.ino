#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// =====================================================
// OLED
// =====================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_SDA 8
#define OLED_SCL 9
#define OLED_ADDR 0x3C

bool oledOK = false;

Adafruit_SH1106G display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);

// =====================================================
// TOUCH SENSOR
// =====================================================

#define TOUCH_PIN 5

#define SLEEPY_TIMEOUT 10000
#define HAPPY_HOLD_TIME 1500

bool lastTouchState = LOW;

unsigned long lastActivityTime = 0;
unsigned long happyStartTime = 0;

// =====================================================
// MOTOR DRIVER - L298N
// =====================================================

#define IN1 14
#define IN2 17
#define IN3 15
#define IN4 16

unsigned long lastCmdTime = 0;

#define CMD_TIMEOUT 800

bool moving = false;

// =====================================================
// BUZZER
// =====================================================

#define BUZZER 18

bool hornOn = false;

// =====================================================
// WIFI
// =====================================================

const char* AP_SSID = "EVA";
const char* AP_PASS = "88888888";

WebServer server(80);

// =====================================================
// EYE STATES
// =====================================================

enum EyeMood {
  EYE_NORMAL,
  EYE_HAPPY,
  EYE_SLEEPY,
  EYE_ANGRY
};

EyeMood eyeMood = EYE_NORMAL;

// =====================================================
// EYE DIRECTION
// =====================================================

enum EyeDirection {
  EYE_CENTER,
  EYE_UP,
  EYE_DOWN,
  EYE_LEFT,
  EYE_RIGHT
};

EyeDirection eyeDirection = EYE_CENTER;

// =====================================================
// BLINK
// =====================================================

bool eyesClosed = false;

unsigned long lastBlinkTime = 0;
unsigned long blinkStartTime = 0;

const unsigned long BLINK_INTERVAL = 4000;
const unsigned long BLINK_DURATION = 150;

// =====================================================
// DISPLAY UPDATE CONTROL
// =====================================================

unsigned long lastEyeDraw = 0;

const unsigned long EYE_DRAW_INTERVAL = 80;

// =====================================================
// MOBILE WEB PAGE
// =====================================================

const char INDEX_HTML[] PROGMEM = R"rawliteral(

<!DOCTYPE html>

<html>

<head>

<meta name="viewport"
content="width=device-width,
initial-scale=1,
maximum-scale=1,
user-scalable=no">

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

  -webkit-tap-highlight-color:
  transparent;
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

  font-size: 18px;
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

  <button data-command="F">
    F
  </button>

  <div></div>

  <button data-command="L">
    L
  </button>

  <button id="stop"
          class="stop">
    S
  </button>

  <button data-command="R">
    R
  </button>

  <div></div>

  <button data-command="B">
    B
  </button>

  <div></div>

</div>

<button id="horn"
        class="horn">

  HORN

</button>

<div class="info">

  Hold direction button to move.<br>

  Release button to stop.

</div>

<script>

let commandTimer = null;

let currentCommand = "S";

function sendCommand(command) {

  fetch("/move?d=" + command)
    .catch(function() {});

}

function startMovement(command) {

  currentCommand = command;

  sendCommand(command);

  clearInterval(commandTimer);

  commandTimer = setInterval(
    function() {

      sendCommand(currentCommand);

    },
    250
  );
}

function stopMovement() {

  clearInterval(commandTimer);

  commandTimer = null;

  currentCommand = "S";

  sendCommand("S");

}

document
  .querySelectorAll("[data-command]")
  .forEach(function(button) {

    const command =
      button.getAttribute("data-command");

    button.addEventListener(
      "touchstart",
      function(event) {

        event.preventDefault();

        startMovement(command);

      },
      { passive: false }
    );

    button.addEventListener(
      "touchend",
      function(event) {

        event.preventDefault();

        stopMovement();

      },
      { passive: false }
    );

    button.addEventListener(
      "touchcancel",
      function(event) {

        event.preventDefault();

        stopMovement();

      },
      { passive: false }
    );

    button.addEventListener(
      "mousedown",
      function() {

        startMovement(command);

      }
    );

    button.addEventListener(
      "mouseup",
      function() {

        stopMovement();

      }
    );

    button.addEventListener(
      "mouseleave",
      function() {

        stopMovement();

      }
    );

  });

document
  .getElementById("stop")
  .addEventListener(
    "click",
    function() {

      stopMovement();

    }
  );

const horn =
  document.getElementById("horn");

horn.addEventListener(
  "touchstart",
  function(event) {

    event.preventDefault();

    fetch("/horn?s=1")
      .catch(function() {});

  },
  { passive: false }
);

horn.addEventListener(
  "touchend",
  function(event) {

    event.preventDefault();

    fetch("/horn?s=0")
      .catch(function() {});

  },
  { passive: false }
);

horn.addEventListener(
  "mousedown",
  function() {

    fetch("/horn?s=1")
      .catch(function() {});

  }
);

horn.addEventListener(
  "mouseup",
  function() {

    fetch("/horn?s=0")
      .catch(function() {});

  }
);

</script>

</body>

</html>

)rawliteral";


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

void drive(
  bool left1,
  bool left2,
  bool right1,
  bool right2
) {

  digitalWrite(IN1, left1);
  digitalWrite(IN2, left2);

  digitalWrite(IN3, right1);
  digitalWrite(IN4, right2);

  moving = true;

}


// =====================================================
// DRAW CLOSED EYES
// =====================================================

void drawClosedEyes() {

  display.clearDisplay();

  display.fillRoundRect(
    12,
    30,
    45,
    5,
    3,
    SH110X_WHITE
  );

  display.fillRoundRect(
    71,
    30,
    45,
    5,
    3,
    SH110X_WHITE
  );

  display.display();

}


// =====================================================
// DRAW NORMAL EYES
// =====================================================

void drawNormalEyes() {

  display.clearDisplay();

  int leftX = 12;
  int rightX = 71;

  int eyeY = 17;

  int eyeW = 45;
  int eyeH = 31;

  // Direction movement

  if (eyeDirection == EYE_UP) {

    eyeY = 11;

  }

  else if (eyeDirection == EYE_DOWN) {

    eyeY = 23;

  }

  // Eye shapes

  display.fillRoundRect(
    leftX,
    eyeY,
    eyeW,
    eyeH,
    9,
    SH110X_WHITE
  );

  display.fillRoundRect(
    rightX,
    eyeY,
    eyeW,
    eyeH,
    9,
    SH110X_WHITE
  );


  // Pupil offset

  int pupilOffsetX = 0;

  int pupilOffsetY = 0;

  if (eyeDirection == EYE_LEFT) {

    pupilOffsetX = -7;

  }

  else if (eyeDirection == EYE_RIGHT) {

    pupilOffsetX = 7;

  }

  else if (eyeDirection == EYE_UP) {

    pupilOffsetY = -6;

  }

  else if (eyeDirection == EYE_DOWN) {

    pupilOffsetY = 6;

  }


  // Pupils

  display.fillCircle(
    34 + pupilOffsetX,
    32 + pupilOffsetY,
    7,
    SH110X_BLACK
  );

  display.fillCircle(
    93 + pupilOffsetX,
    32 + pupilOffsetY,
    7,
    SH110X_BLACK
  );


  // Small highlights

  display.fillCircle(
    32 + pupilOffsetX,
    30 + pupilOffsetY,
    2,
    SH110X_WHITE
  );

  display.fillCircle(
    91 + pupilOffsetX,
    30 + pupilOffsetY,
    2,
    SH110X_WHITE
  );

  display.display();

}


// =====================================================
// DRAW HAPPY EYES
// =====================================================

void drawHappyEyes() {

  display.clearDisplay();

  // Left eye

  display.drawLine(
    13, 37,
    24, 27,
    SH110X_WHITE
  );

  display.drawLine(
    24, 27,
    35, 37,
    SH110X_WHITE
  );

  display.drawLine(
    17, 34,
    24, 30,
    SH110X_WHITE
  );


  // Right eye

  display.drawLine(
    73, 37,
    84, 27,
    SH110X_WHITE
  );

  display.drawLine(
    84, 27,
    95, 37,
    SH110X_WHITE
  );

  display.drawLine(
    77, 34,
    84, 30,
    SH110X_WHITE
  );


  // Little cheeks

  display.drawPixel(
    42,
    43,
    SH110X_WHITE
  );

  display.drawPixel(
    43,
    43,
    SH110X_WHITE
  );

  display.drawPixel(
    101,
    43,
    SH110X_WHITE
  );

  display.drawPixel(
    102,
    43,
    SH110X_WHITE
  );

  display.display();

}


// =====================================================
// DRAW SLEEPY EYES
// =====================================================

void drawSleepyEyes() {

  display.clearDisplay();

  // Left eyelid

  display.fillRoundRect(
    12,
    30,
    45,
    9,
    4,
    SH110X_WHITE
  );

  // Right eyelid

  display.fillRoundRect(
    71,
    30,
    45,
    9,
    4,
    SH110X_WHITE
  );

  // Small lower eyelid lines

  display.drawLine(
    18,
    42,
    50,
    42,
    SH110X_WHITE
  );

  display.drawLine(
    78,
    42,
    110,
    42,
    SH110X_WHITE
  );

  display.display();

}


// =====================================================
// DRAW ANGRY EYES
// =====================================================

void drawAngryEyes() {

  display.clearDisplay();

  // Left eye

  display.fillRoundRect(
    12,
    22,
    45,
    27,
    7,
    SH110X_WHITE
  );

  // Right eye

  display.fillRoundRect(
    71,
    22,
    45,
    27,
    7,
    SH110X_WHITE
  );


  // Angry eyebrows

  display.drawLine(
    10,
    13,
    57,
    25,
    SH110X_BLACK
  );

  display.drawLine(
    71,
    25,
    118,
    13,
    SH110X_BLACK
  );


  // Pupils

  display.fillCircle(
    35,
    37,
    7,
    SH110X_BLACK
  );

  display.fillCircle(
    93,
    37,
    7,
    SH110X_BLACK
  );

  display.display();

}


// =====================================================
// UPDATE EYES
// =====================================================

void updateEyes() {

  if (!oledOK) {
    return;
  }

  if (eyesClosed) {

    drawClosedEyes();

    return;
  }


  switch (eyeMood) {

    case EYE_NORMAL:

      drawNormalEyes();

      break;


    case EYE_HAPPY:

      drawHappyEyes();

      break;


    case EYE_SLEEPY:

      drawSleepyEyes();

      break;


    case EYE_ANGRY:

      drawAngryEyes();

      break;

  }

}


// =====================================================
// BLINK SYSTEM
// =====================================================

void updateBlink() {

  if (eyeMood != EYE_NORMAL) {

    return;
  }

  unsigned long now = millis();


  // Start blink

  if (
    !eyesClosed &&
    now - lastBlinkTime >= BLINK_INTERVAL
  ) {

    eyesClosed = true;

    blinkStartTime = now;

    updateEyes();

  }


  // End blink

  if (
    eyesClosed &&
    now - blinkStartTime >= BLINK_DURATION
  ) {

    eyesClosed = false;

    lastBlinkTime = now;

    updateEyes();

  }

}


// =====================================================
// SET EYE MOOD
// =====================================================

void setEyeMood(EyeMood newMood) {

  if (eyeMood == newMood) {

    return;

  }

  eyeMood = newMood;

  eyesClosed = false;

  updateEyes();

}


// =====================================================
// SET EYE DIRECTION
// =====================================================

void setEyeDirection(EyeDirection direction) {

  if (eyeDirection == direction) {

    return;

  }

  eyeDirection = direction;

  updateEyes();

}


// =====================================================
// WAKE UP
// =====================================================

void wakeUp() {

  lastActivityTime = millis();

  if (eyeMood == EYE_SLEEPY) {

    setEyeMood(EYE_NORMAL);

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

    drive(
      HIGH,
      LOW,
      HIGH,
      LOW
    );

    setEyeDirection(EYE_UP);

  }


  // BACKWARD

  else if (d == "B") {

    drive(
      LOW,
      HIGH,
      LOW,
      HIGH
    );

    setEyeDirection(EYE_DOWN);

  }


  // LEFT

  else if (d == "L") {

    drive(
      LOW,
      HIGH,
      HIGH,
      LOW
    );

    setEyeDirection(EYE_LEFT);

  }


  // RIGHT

  else if (d == "R") {

    drive(
      HIGH,
      LOW,
      LOW,
      HIGH
    );

    setEyeDirection(EYE_RIGHT);

  }


  // STOP

  else {

    motorsStop();

    setEyeDirection(EYE_CENTER);

  }


  server.send(
    200,
    "text/plain",
    "OK"
  );

}


// =====================================================
// HORN HANDLER
// =====================================================

void handleHorn() {

  hornOn =
    server.arg("s") == "1";


  digitalWrite(
    BUZZER,
    hornOn ? HIGH : LOW
  );


  if (hornOn) {

    wakeUp();

    setEyeMood(EYE_ANGRY);

  }

  else {

    setEyeMood(EYE_NORMAL);

  }


  server.send(
    200,
    "text/plain",
    "OK"
  );

}


// =====================================================
// TOUCH + EMOTIONS
// =====================================================

void handleTouchMoods() {

  bool touchState =
    digitalRead(TOUCH_PIN);


  // =================================================
  // NEW TOUCH
  // =================================================

  if (
    touchState == HIGH &&
    lastTouchState == LOW
  ) {

    setEyeMood(EYE_HAPPY);

    happyStartTime =
      millis();

    lastActivityTime =
      millis();

  }


  lastTouchState =
    touchState;


  // =================================================
  // HAPPY STATE
  // =================================================

  if (eyeMood == EYE_HAPPY) {

    if (
      millis() - happyStartTime
        >= HAPPY_HOLD_TIME &&
      touchState == LOW
    ) {

      setEyeMood(EYE_NORMAL);

      lastActivityTime =
        millis();

    }

    return;
  }


  // =================================================
  // SLEEPY STATE
  // =================================================

  if (
    eyeMood == EYE_NORMAL &&
    !moving &&
    !hornOn &&
    millis() - lastActivityTime
      >= SLEEPY_TIMEOUT
  ) {

    setEyeMood(EYE_SLEEPY);

  }

}


// =====================================================
// OLED INITIALIZATION
// =====================================================

void setupOLED() {

  Serial.println();
  Serial.println("==============================");
  Serial.println("OLED INITIALIZATION");
  Serial.println("==============================");


  Serial.println(
    "OLED: Starting I2C..."
  );


  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );


  // Stable I2C speed

  Wire.setClock(100000);


  delay(500);


  Serial.println(
    "OLED: Starting SH1106..."
  );


  oledOK =
    display.begin(
      OLED_ADDR,
      true
    );


  if (!oledOK) {

    Serial.println(
      "OLED: INIT FAILED!"
    );

    return;

  }


  Serial.println(
    "OLED: INIT SUCCESS!"
  );


  // Clear display

  display.clearDisplay();

  display.display();


  // Initial face

  eyeMood = EYE_NORMAL;

  eyeDirection =
    EYE_CENTER;

  eyesClosed = false;

  lastBlinkTime =
    millis();


  updateEyes();


  Serial.println(
    "OLED: Manual eyes ready!"
  );

}


// =====================================================
// WIFI INITIALIZATION
// =====================================================

void setupWiFi() {

  Serial.println();
  Serial.println(
    "Starting WiFi Access Point..."
  );


  WiFi.mode(WIFI_AP);

  delay(500);


  bool result =
    WiFi.softAP(
      AP_SSID,
      AP_PASS
    );


  if (result) {

    Serial.println(
      "WIFI AP STARTED SUCCESSFULLY!"
    );

  }

  else {

    Serial.println(
      "WIFI AP FAILED!"
    );

  }


  Serial.print("SSID: ");
  Serial.println(AP_SSID);


  Serial.print("Password: ");
  Serial.println(AP_PASS);


  Serial.print("IP Address: ");
  Serial.println(
    WiFi.softAPIP()
  );


  Serial.print("MAC Address: ");
  Serial.println(
    WiFi.softAPmacAddress()
  );


  // =================================================
  // WEB SERVER
  // =================================================

  server.on(
    "/",
    []() {

      server.send_P(
        200,
        "text/html",
        INDEX_HTML
      );

    }
  );


  server.on(
    "/move",
    handleMove
  );


  server.on(
    "/horn",
    handleHorn
  );


  server.begin();


  Serial.println(
    "Web server started!"
  );


  Serial.println();

  Serial.println(
    "================================"
  );

  Serial.println(
    "Connect phone to:"
  );

  Serial.println(
    "SSID: EVA"
  );

  Serial.println(
    "Password: 88888888"
  );

  Serial.println();

  Serial.println(
    "Open:"
  );

  Serial.println(
    "http://192.168.4.1"
  );

  Serial.println(
    "================================"
  );

}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  delay(1000);


  Serial.println();

  Serial.println(
    "================================"
  );

  Serial.println(
    "       EVA ROBOT START"
  );

  Serial.println(
    "================================"
  );


  // =================================================
  // OLED
  // =================================================

  setupOLED();


  // =================================================
  // MOTOR GPIO
  // =================================================

  Serial.println(
    "Starting motor GPIO..."
  );


  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);

  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);


  motorsStop();


  Serial.println(
    "Motor GPIO ready!"
  );


  // =================================================
  // BUZZER
  // =================================================

  pinMode(
    BUZZER,
    OUTPUT
  );

  digitalWrite(
    BUZZER,
    LOW
  );


  // =================================================
  // TOUCH
  // =================================================

  pinMode(
    TOUCH_PIN,
    INPUT
  );


  // =================================================
  // ACTIVITY TIMER
  // =================================================

  lastActivityTime =
    millis();


  // =================================================
  // WIFI
  // =================================================

  setupWiFi();


  Serial.println();

  Serial.println(
    "EVA ROBOT READY!"
  );

}


// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

  // Web server

  server.handleClient();


  // Motor safety timeout

  if (
    moving &&
    millis() - lastCmdTime
      > CMD_TIMEOUT
  ) {

    motorsStop();

    setEyeDirection(
      EYE_CENTER
    );

  }


  // Touch / emotions

  handleTouchMoods();


  // Automatic blink

  updateBlink();


  delay(20);

}