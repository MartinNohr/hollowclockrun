#include <EEPROM.h>
#define HC_VERSION 2  // change this when the settings structure is changed

// Motor and clock parameters
// 2048 * 90 / 12 / 60 = 256
#define STEPS_PER_MIN 256

// one minute per micro seconds. Theoretically it should be 60000000,
// and can be used to adjust the clock speed.
#define MIN_PER_USEC_DEF (60000000L)
//#define SAFETY_MOTION (STEPS_PER_MIN) // use this for the ratchet version
#define SAFETY_MOTION (0)

// hold setting information in EEPROM
struct {
	int nVersion = HC_VERSION;  // this int must be first
	unsigned long nMinPerUsec = MIN_PER_USEC_DEF;
	bool bReverse = false;
	bool bTestMode = false;
	int nStepSpeed = 60;
	int nSafetyMotion = 0;
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
	// wait for a single step of stepper
	const int delaytime = 6;
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
	Serial.println("Starting clock");
	int checkVer;
	EEPROM.get(0, checkVer);
	// see if value is valid
	if (checkVer == HC_VERSION) {
		EEPROM.get(0, settings);
		Serial.println("Loaded settings");
	}
	else {
		// invalid value, so save defaults
		EEPROM.put(0, settings);
		EEPROM.commit();
		Serial.println("Loaded default settings");
	}
}

void loop() {
	static uint64_t prev_cnt = -1;
	uint64_t cnt, usec;

	if (settings.bTestMode) {
		// just run the motor
		rotate(1000);
	}
	else {
		usec = (uint64_t)millis() * (uint64_t)1000;
		cnt = usec / settings.nMinPerUsec;
		if (prev_cnt == cnt) {
			// check for keyboard
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
				int64_t argval = 0;
				if (line.length()) {
					argval = (int64_t)line.toDouble();
				}
				switch (ch) {
				case '?':
					ShowMenu();
					break;
				case '+':
					if (argval == 0)
						argval = 1;
					rotate(STEPS_PER_MIN * argval);
					break;
				case '-':  // go back one to0 many and then back forward to take care of slop in the gears
					if (argval == 0)
						argval = 1;
					rotate(-(STEPS_PER_MIN * argval + STEPS_PER_MIN));
					rotate(STEPS_PER_MIN);
					break;
				case 'T':  // tweak stepper position
					if (argval == 0)
						argval = 1;
					rotate(argval);
				}
			}
			delay(10);
			return;
		}
		prev_cnt = cnt;

		rotate(STEPS_PER_MIN + SAFETY_MOTION);  // go too far a bit
		if (SAFETY_MOTION)
			rotate(-SAFETY_MOTION);  // alignment
	}
}

void ShowMenu() {
	Serial.println(String("----- Current Settings -----"));
	Serial.println(String("Data version        : ") + HC_VERSION);
	Serial.println(String("uSeconds per minute : ") + String(settings.nMinPerUsec - 60000000L));
	Serial.println(String("Reverse Motor       : ") + settings.bReverse);
	Serial.println(String("Test Mode           : ") + settings.bTestMode);
	Serial.println(String("Step Speed          : ") + settings.nStepSpeed);
	Serial.println(String("----- Commands -----"));
	Serial.println(String("+<n> : Advance n minutes"));
	Serial.println(String("-<n> : Reverse n minutes"));
	Serial.println(String("T<n> : Tweak Position Steps"));
	Serial.println("Command?");
}
