#include <EEPROM.h>
#define HC_VERSION 1  // change this when the settings structure is changed
#define FIRMWARE_VERSION 1.00

// Motor and clock parameters
// 2048 * 90 / 12 / 60 = 256
#define STEPS_PER_MIN 256

//#define SAFETY_MOTION (STEPS_PER_MIN) // use this for the ratchet version
#define SAFETY_MOTION (0)

// hold setting information in EEPROM
struct {
  int nVersion = HC_VERSION;  // this int must be first
                              // one minute per micro seconds. Theoretically it should be 60000000,
                              // and can be used to adjust the clock speed.
  unsigned long nUSecPerMin = 60000000L;
  bool bReverse = false;
  bool bTestMode = false;
  int nStepSpeed = 6;
  int nSafetyMotion = 0;
  bool bRunning = true;
} settings;

// ports used to control the stepper motor
// if your motor rotates in the opposite direction,
// change the order as {2, 3, 4, 5};
int port[4] = { 5, 4, 3, 2 };

// sequence of stepper motor control
int seq[4][4] = {
  { LOW, LOW, HIGH, LOW },
  { LOW, LOW, LOW, HIGH },
  { HIGH, LOW, LOW, LOW },
  { LOW, HIGH, LOW, LOW }
};

void rotate(int step) {
  if (step == 0)
    return;
  // wait for a single step of stepper
  const int delaytime = settings.nStepSpeed;
  static int phase = 0;
  int i, j;
  int delta = (step < 0 || settings.bReverse) ? 3 : 1;
  int dt = delaytime * 3;

  step = abs(step);
  for (j = 0; j < step; j++) {
    phase = (phase + delta) % 4;
    for (i = 0; i < 4; i++) {
      digitalWrite(port[i], seq[phase][i]);
    }
    delay(dt);
    if (dt > delaytime) dt--;
  }
  // power cut
  for (i = 0; i < 4; i++) {
    digitalWrite(port[i], LOW);
  }
}

void setup() {
  pinMode(port[0], OUTPUT);
  pinMode(port[1], OUTPUT);
  pinMode(port[2], OUTPUT);
  pinMode(port[3], OUTPUT);

  rotate(-STEPS_PER_MIN * 2);  // initialize
  rotate(STEPS_PER_MIN * 2);
  Serial.begin(9600);
  while (!Serial.availableForWrite())
    delay(10);
  EEPROM.begin(512);
  int checkVer;
  EEPROM.get(0, checkVer);
  // see if value is valid
  if (checkVer == HC_VERSION) {
    EEPROM.get(0, settings);
    Serial.println("Loaded settings");
  } else {
    // invalid value, so save defaults
    EEPROM.put(0, settings);
    EEPROM.commit();
    Serial.println("Loaded default settings");
  }
}

void loop() {
  static uint64_t minutes;
  static unsigned long last_micros;
  unsigned long current_micros = micros();
  if (settings.bTestMode) {
    // just run the motor until they enter anything
    rotate(STEPS_PER_MIN);
    if (Serial.available()) {
      settings.bTestMode = false;
      EEPROM.put(0, settings);
      EEPROM.commit();
    }
  } else {
    if (settings.bRunning) {
      // see if we have gone another minute, use while because we might have missed a minute while doing menus
      while (current_micros - last_micros >= settings.nUSecPerMin) {
        // time to advance the clock one minute
        ++minutes;
        // Serial.println(String("run time minutes: ") + (unsigned long)minutes + " = Hours: " + String((float)minutes / 60, 2) + " internal clock uS: " + current_micros + " last uS: " + last_micros);
        Serial.println(String("run time minutes: ") + (unsigned long)minutes + " = Hours: " + String((float)minutes / 60, 2));
        rotate(STEPS_PER_MIN + SAFETY_MOTION);  // go a little too far
        rotate(-SAFETY_MOTION);                 // correct it
        // bump the uSeconds for the next minute
        last_micros += settings.nUSecPerMin;
      }
    } else {
      last_micros = current_micros = micros();
      minutes = 0;
    }
    // check for keyboard
    RunMenu();
  }
  delay(10);
}

void RunMenu() {
  if (Serial.available()) {
    String line;
    line = Serial.readString();
    line.trim();
    line.toUpperCase();
    if (line.length() == 0)
      line = "?";
    // Serial.println("line read: " + line);
    // see if any parameters, leave in line
    char ch = line[0];
    line = line.substring(1);
    line.trim();
    long argval = 0;
    if (line.length()) {
      argval = line.toInt();
    }
    bool bSaveSettings = false;
    switch (ch) {
      case '+':
        if (argval == 0)
          argval = 1;
        rotate(STEPS_PER_MIN * argval);
        break;
      case '-':  // go back one too many and then back forward to take care of backlash in the gears
        if (argval == 0)
          argval = 1;
        rotate(-(STEPS_PER_MIN * argval + STEPS_PER_MIN));
        rotate(STEPS_PER_MIN);
        break;
      case 'A':  // adjust stepper position
        if (argval == 0)
          argval = 1;
        rotate(argval);
        // if negative move backwards to take up slack
        if (argval < 0) {
          rotate(-STEPS_PER_MIN);
          rotate(STEPS_PER_MIN);
        }
        break;
      case 'T':  // test mode
        settings.bTestMode = true;
        bSaveSettings = true;
        break;
      case 'S':  // stepper delay
        if (argval == 0)
          argval = 6;
        settings.nStepSpeed = argval;
        bSaveSettings = true;
        break;
      case 'C':  // clock calibration, default is 0
        settings.nUSecPerMin = 60000000L - argval;
        bSaveSettings = true;
        break;
      case 'R':  // toggle reverse motor setting
        settings.bReverse = !settings.bReverse;
        bSaveSettings = true;
        break;
      case 'W':  // toggle running state
        settings.bRunning = !settings.bRunning;
        bSaveSettings = true;
        break;
      case 'F':  // figure out time correction setting
        if (line.length() == 0)
          break;
        {
          float seconds, hours;
          seconds = line.toFloat();
          // check for second argument
          int index = line.indexOf(' ');
          if (index <= 0)
            break;
          line = line.substring(index);
          line.trim();
          hours = line.toFloat();
          long correction = (long)(seconds * 1000000.0 / hours / 60.0);
          // Serial.println(correction);
          // add to current setting, makes more sense since it might already have been adjusted
          settings.nUSecPerMin = (long)settings.nUSecPerMin + correction;
          bSaveSettings = true;
        }
        break;
    }
    if (bSaveSettings) {
      EEPROM.put(0, settings);
      EEPROM.commit();
    }
    ShowMenu();
  }
}

void ShowMenu() {
  long correction = 60000000L - settings.nUSecPerMin;
  Serial.println(String("------ Current Settings ------"));
  Serial.println(String("Firmware version              : ") + FIRMWARE_VERSION);
  Serial.println(String("uSeconds calibrate per minute : ") + String(correction) + " or " + String((float)((float)correction * 60 * 24 / 1000000L), 2) + " sec/day");
  Serial.println(String("Wait/Run State                : ") + (settings.bRunning ? "Running" : "Waiting"));
  Serial.println(String("Stepper Delay (mSec)          : ") + settings.nStepSpeed);
  Serial.println(String("Test Mode                     : ") + (settings.bTestMode ? "On" : "Off"));
  Serial.println(String("Reverse Motor                 : ") + (settings.bReverse ? "Yes" : "No"));
  Serial.println(String("--- Commands ---"));
  Serial.println(String("+<n>           : Advance n minutes"));
  Serial.println(String("-<n>           : Reverse n minutes"));
  Serial.println(String("A<n>           : Adjust Minute Position (+/- 256 is a full minute)"));
  Serial.println(String("W              : Wait, toggle running state of clock"));
  Serial.println(String("C<n>           : Calibrate uSeconds per minute, is default, change as needed, +speeds up, -slows down"));
  Serial.println(String("F<sec> <hours> : Figure correction using seconds and hours (floats), e.g. F -2.5 24.0 if 2 seconds slow per day"));
  Serial.println(String("S<n>           : Set stepper motor delay, default is 6, range 2 to 120"));
  Serial.println(String("T              : Test mode (enter anything while running to stop)"));
  Serial.println(String("R              : Reverse motor setting"));
  Serial.println("Command?");
}
