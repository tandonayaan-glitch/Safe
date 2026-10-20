/*
  ESP32 DIY Safe - keypad + MG90S servo lock

  Libraries: Keypad (Mark Stanley, Alexander Brevig), ESP32Servo (Kevin Harrington, John K. Bennett)
  Serial Monitor: 115200 baud

  HOBBY PROJECT ONLY - this is not a theft-resistant security device.
*/

#include <Keypad.h>
#include <ESP32Servo.h>

// ===================== CONFIGURATION =====================

// Password (digits only). Fixed in source; it cannot be changed from the keypad.
const char SAFE_PASSWORD[] = "270414";
const uint8_t MAX_PASSWORD_LENGTH = 12;

// Servo
const int SERVO_PIN = 18;
const int LOCKED_ANGLE = 0;      // CALIBRATE for your printed bolt (0-180)
const int UNLOCKED_ANGLE = 90;   // CALIBRATE for your printed bolt (0-180)
const int SERVO_MIN_PULSE_US = 500;
const int SERVO_MAX_PULSE_US = 2400;
const unsigned long UNLOCK_DURATION_MS = 5000;  // time the safe stays unlocked
const unsigned long SERVO_SETTLE_MS = 800;      // time allowed for the servo to reach its angle

// true: stop sending servo pulses once it has reached its position (no buzzing/stalling
// against the bolt stop). false: keep holding the position actively.
const bool DETACH_SERVO_WHEN_IDLE = true;

// Keypad pins. GPIO 12 is a boot-strapping pin on many ESP32 boards: if the board
// fails to boot/upload with the keypad connected, move Row 2 to a free GPIO
// such as 4, 16, 17, 19 or 23 and change ROW_PINS below.
const byte ROWS = 4;
const byte COLS = 4;
byte ROW_PINS[ROWS] = {13, 12, 14, 27};   // Row 1..4
byte COL_PINS[COLS] = {26, 25, 33, 32};   // Column 1..4

char KEYS[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

// ===================== INTERNAL STATE =====================

static_assert(sizeof(SAFE_PASSWORD) - 1 > 0 && sizeof(SAFE_PASSWORD) - 1 <= MAX_PASSWORD_LENGTH,
              "SAFE_PASSWORD must be 1 to MAX_PASSWORD_LENGTH characters");

Keypad keypad = Keypad(makeKeymap(KEYS), ROW_PINS, COL_PINS, ROWS, COLS);
Servo lockServo;

char entry[MAX_PASSWORD_LENGTH + 1];
uint8_t entryLength = 0;

bool isUnlocked = false;
unsigned long unlockedAt = 0;

bool servoAttached = false;
bool servoSettling = false;
unsigned long servoMovedAt = 0;

// ===================== SERVO =====================

void moveServo(int angle) {
  angle = constrain(angle, 0, 180);
  if (!servoAttached) {
    lockServo.setPeriodHertz(50);
    lockServo.attach(SERVO_PIN, SERVO_MIN_PULSE_US, SERVO_MAX_PULSE_US);
    servoAttached = true;
  }
  lockServo.write(angle);
  servoMovedAt = millis();
  servoSettling = true;
}

void updateServo() {
  if (servoSettling && millis() - servoMovedAt >= SERVO_SETTLE_MS) {
    servoSettling = false;
    if (DETACH_SERVO_WHEN_IDLE && servoAttached) {
      lockServo.detach();
      servoAttached = false;
    }
  }
}

void lockSafe() {
  moveServo(LOCKED_ANGLE);
  isUnlocked = false;
  Serial.println("Safe locked.");
}

void unlockSafe() {
  if (!isUnlocked) {
    moveServo(UNLOCKED_ANGLE);
    isUnlocked = true;
  }
  unlockedAt = millis();  // a correct entry while unlocked restarts the timer
}

// ===================== PASSWORD HANDLING =====================

void clearEntry() {
  memset(entry, 0, sizeof(entry));
  entryLength = 0;
}

// Compares without exiting early on the first mismatch.
bool entryMatchesPassword() {
  const uint8_t passwordLength = sizeof(SAFE_PASSWORD) - 1;
  if (entryLength != passwordLength) return false;
  uint8_t diff = 0;
  for (uint8_t i = 0; i < passwordLength; i++) {
    diff |= (uint8_t)(entry[i] ^ SAFE_PASSWORD[i]);
  }
  return diff == 0;
}

void submitEntry() {
  Serial.println();
  if (entryLength == 0) {
    Serial.println("Nothing entered. Enter the password and press #.");
  } else if (entryMatchesPassword()) {
    Serial.println("Password CORRECT. Unlocking for 5 seconds.");
    unlockSafe();
  } else {
    Serial.println("Password INCORRECT. Safe stays locked.");
  }
  clearEntry();
}

void handleKey(char key) {
  if (key >= '0' && key <= '9') {
    if (entryLength < MAX_PASSWORD_LENGTH) {
      entry[entryLength++] = key;
      entry[entryLength] = '\0';
      Serial.print('*');
    } else {
      Serial.print("\n(Maximum of 12 digits reached. Press # to submit or * to clear.)\n");
    }
  } else if (key == '*') {
    clearEntry();
    Serial.println("\nEntry cleared.");
  } else if (key == '#') {
    submitEntry();
  }
  // Letters A-D are ignored.
}

// ===================== ARDUINO =====================

void setup() {
  Serial.begin(115200);
  delay(200);

  ESP32PWM::allocateTimer(0);
  clearEntry();
  moveServo(LOCKED_ANGLE);  // always boot locked

  Serial.println();
  Serial.println("=== ESP32 DIY Safe ===");
  Serial.println("Safe is LOCKED.");
  Serial.println("Enter the password on the keypad and press # to submit (* clears).");
}

void loop() {
  updateServo();

  if (isUnlocked && millis() - unlockedAt >= UNLOCK_DURATION_MS) {
    lockSafe();
  }

  char key = keypad.getKey();
  if (key) {
    handleKey(key);
  }
}
