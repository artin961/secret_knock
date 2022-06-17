#include <EEPROM.h>



#define PIN_MANUAL_OPEN 4             //PIN 3 on ATTINY85            
#define PIN_LED_PGM_BUTTON  3        //PIN 2 on ATTINY85    BUTTON AND SWITCH ON SAME IO/ DRIVE LWO TO LIT LED, AND INPUT PULLUP TO SEE IF BTN IS PRESSED
#define PIN_ANALOG_KNOCK_SENSE  A1  //PIN 7 on ATTINY85
#define PIN_BUZZER    1             // PB1 //PIN 6 on ATTINY85
#define PIN_LOCK_OPEN 0             ////PIN 5 on ATTINY85

#define ATTEMPTS_BEFORE_LOCKOUT 10
#define LOCKOUT_DURATION 60
#define PROGRAM_BUTTON_DEBOUNCE 1000 ///DEBOUNCE PROGRAMING BUTTON
#define MANUAL_OPEN_DEBOUNCE 100   //FILTER NOISE ON MANUAL OPENER PIN
#define EEPROM_CHECKSUM 123        //CHECKSUM TO NOW IF EEPROM DATA IS VALID

#define THRESHOLD 3                // Minimum signal from the piezo to register as a knock. Higher = less sensitive. Typical values 1 - 10
#define REJECT_VALUE  25          // If an individual knock is off by this percentage of a knock we don't unlock. Typical values 10-30
#define AVG_REJECT_VALUE 15       // If the average timing of all the knocks is off by this percent we don't unlock. Typical values 5-20
#define KNOCK_FADE_TIME  150      // Milliseconds we allow a knock to fade before we listen for another one. (Debounce timer.)
#define OPEN_HOLD_TIME 100        // Milliseconds that we operate the lock solenoid latch before releasing it.
#define MAXIMUM_KNOCKS  20        // Maximum number of knocks to listen for.
#define KNOCK_COMPLEATE  1200     // Longest time to wait for a knock before we assume that it's finished. (milliseconds)


////TONES
#define CHIRP_1KHZ 500  ///TO GENERATE 1KHZ TONE USING CHIRP FUNCTION
#define CHIRP_2KHZ 250  ///TO GENERATE 2KHZ TONE USING CHIRP FUNCTION
#define CHIRP_4KHZ 125  ///TO GENERATE 4KHZ TONE USING CHIRP FUNCTION

bool ledStatus = false; ////IF LED SHOULD BE ON OR OFF FOR MULTIPLEXING BUTTON AND LED ON THE SAME PIN
bool actuated = false;  ////if door lock is currently actuated

uint8_t wrongAttempts = 0; //LOGIN ATTEMPTS FOR LOCKOUT

byte secretCode[MAXIMUM_KNOCKS] = {50, 25, 25, 50, 100, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // Initial setup: "Shave and a Hair Cut, two bits."
int knockReadings[MAXIMUM_KNOCKS];    // When someone knocks this array fills with the delays between knocks.
int knockSensorValue = 0;            // Last reading of the knock sensor.
boolean programModeActive = false;   // True if we're trying to program a new knock.
unsigned long previousMillisManualOpen = 0;     //for debouncind manual  open input
unsigned long previousMillisProgramButton = 0;     //for debouncind programing button input
unsigned long prewMillisSeconds = 0; //for counting seconds for lockout
uint16_t lockoutSecs = 0; ///seconds remaining in lockout 0-non locked

void setup() {
  ///SETUP IO
  pinMode(PIN_MANUAL_OPEN, INPUT_PULLUP); ///ACTIVE LOW SIGNAL FOR MANUALLY OPEN THE DOOR
  pinMode(PIN_BUZZER, OUTPUT);            ///BUZZER PWM PIN
  pinMode(PIN_LED_PGM_BUTTON, INPUT);////BUTTON AND SWITCH ON SAME IO/ DRIVE LWO TO LIT LED, AND INPUT PULLUP TO SEE IF BTN IS PRESSED
  pinMode(PIN_LOCK_OPEN, OUTPUT);           ////ACTIVE HIGH TO ACTUATE THE DOOR LOCK
  digitalWrite(PIN_BUZZER, LOW);  ///BUZZER OFF
  digitalWrite(PIN_LOCK_OPEN, LOW);  ///LOCK OFF
  loadFromEeprom();   // Load the secret knock (if any) from EEPROM.
  delay(500);          // This delay is here because the solenoid lock returning to place can otherwise trigger and inadvertent knock.
}
void loop() {
  importtantToLoop(); ////CHECKING PROGRAMMING BUTTON AND DOOR OPENING BUTTON REGARDLES OF THE KNOCKING

  // Listen for any knock at all.
  knockSensorValue = analogRead(PIN_ANALOG_KNOCK_SENSE);

  if (knockSensorValue >= THRESHOLD && (lockoutSecs == 0 || programModeActive)) {
    LedWrite(!programModeActive);//BLINK THE LED
    knockDelay();
    LedWrite(programModeActive);//UNBLINK THE LED
    listenToSecretKnock();           // We have our first knock. Go and see what other knocks are in store...
  }


}

// Records the timing of knocks.
void listenToSecretKnock() {
  // First reset the listening array.
  for (int i = 0; i < MAXIMUM_KNOCKS; i++) {
    knockReadings[i] = 0;
  }

  int currentKnockNumber = 0;               // Position counter for the array.
  int startTime = millis();                 // Reference for when this knock started.
  int now = millis();

  do {                                      // Listen for the next knock or wait for it to timeout.
    knockSensorValue = analogRead(PIN_ANALOG_KNOCK_SENSE);
    if (knockSensorValue >= THRESHOLD) {                  // Here's another knock. Save the time between knocks.
      now = millis();
      knockReadings[currentKnockNumber] = now - startTime;
      currentKnockNumber ++;
      startTime = now;

      LedWrite(!programModeActive);//BLINK THE LED
      knockDelay();
      LedWrite(programModeActive);//UNBLINK THE LED
    }

    now = millis();

    // Stop listening if there are too many knocks or there is too much time between knocks.
  } while ((now - startTime < KNOCK_COMPLEATE) && (currentKnockNumber < MAXIMUM_KNOCKS));

  //we've got our knock recorded, lets see if it's valid
  if (!programModeActive) {          // Only do this if we're not recording a new knock.
    if (validateKnock()) {
      doorUnlock(OPEN_HOLD_TIME);
    } else {
      // knock is invalid. Blink the LED as a warning to others.
      if (wrongAttempts >= ATTEMPTS_BEFORE_LOCKOUT)
      {
        lockoutSecs = LOCKOUT_DURATION;
        for (int i = 0; i < 3; i++) {
          blinkError();
          chirp(10, CHIRP_4KHZ);
          nonBlockDelayMS(50);
        }
      }
      else
      {
        blinkError();
        wrongAttempts++;
      }


    }
  } else { // If we're in programming mode we still validate the lock because it makes some numbers we need, we just don't do anything with the return.
    validateKnock();
  }
}
void blinkError() {
  for (int i = 0; i < 4; i++) {
    LedWrite(true);
    nonBlockDelayMS(50);
    LedWrite(false);
    nonBlockDelayMS(50);
  }
}

// Unlocks the door.
void doorUnlock(int delayTime) {
  lockoutSecs = 0; ///manually reset counter for loking down
  wrongAttempts = 0; //reset wrong login attempts
  LedWrite(true);
  digitalWrite(PIN_LOCK_OPEN, HIGH);  ///LOCK OFF
  delay(delayTime);
  digitalWrite(PIN_LOCK_OPEN, LOW);  ///LOCK OFF
  chirp(100, CHIRP_2KHZ);
  nonBlockDelayMS(1500);   // This delay is here because releasing the latch can cause a vibration that will be sensed as a knock.
  LedWrite(false);
}

// Checks to see if our knock matches the secret.
// Returns true if it's a good knock, false if it's not.
boolean validateKnock() {
  int i = 0;

  int currentKnockCount = 0;
  int secretKnockCount = 0;
  int maxKnockInterval = 0;               // We use this later to normalize the times.

  for (i = 0; i < MAXIMUM_KNOCKS; i++) {
    if (knockReadings[i] > 0) {
      currentKnockCount++;
    }
    if (secretCode[i] > 0) {
      secretKnockCount++;
    }

    if (knockReadings[i] > maxKnockInterval) {  // Collect normalization data while we're looping.
      maxKnockInterval = knockReadings[i];
    }
  }

  // If we're recording a new knock, save the info and get out of here.
  if (programModeActive == true) {
    for (i = 0; i < MAXIMUM_KNOCKS; i++) { // Normalize the time between knocks. (the longest time = 100)
      secretCode[i] = map(knockReadings[i], 0, maxKnockInterval, 0, 100);
    }
    saveSecretKnock();                // save the result to EEPROM
    programModeActive = false;
    playbackKnock(maxKnockInterval);
    return false;
  }

  if (currentKnockCount != secretKnockCount) { // Easiest check first. If the number of knocks is wrong, don't unlock.
    return false;
  }

  /*  Now we compare the relative intervals of our knocks, not the absolute time between them.
      (ie: if you do the same pattern slow or fast it should still open the door.)
      This makes it less picky, which while making it less secure can also make it
      less of a pain to use if you're tempo is a little slow or fast.
  */
  int totaltimeDifferences = 0;
  int timeDiff = 0;
  for (i = 0; i < MAXIMUM_KNOCKS; i++) { // Normalize the times
    knockReadings[i] = map(knockReadings[i], 0, maxKnockInterval, 0, 100);
    timeDiff = abs(knockReadings[i] - secretCode[i]);
    if (timeDiff > REJECT_VALUE) {       // Individual value too far out of whack. No access for this knock!
      return false;
    }
    totaltimeDifferences += timeDiff;
  }
  // It can also fail if the whole thing is too inaccurate.
  if (totaltimeDifferences / secretKnockCount > AVG_REJECT_VALUE) {
    return false;
  }

  return true;
}


// reads the secret knock from EEPROM. (if any.)
void loadFromEeprom() {
  byte reading;
  reading = EEPROM.read(0);
  if (reading == EEPROM_CHECKSUM) {   // only read EEPROM if the signature byte is correct.
    for (int i = 0; i < MAXIMUM_KNOCKS ; i++) {
      secretCode[i] =  EEPROM.read(i + 1);
    }
  }
}


//saves a new pattern too eeprom
void saveSecretKnock() {
  EEPROM.write(0, 0);  // clear out the signature. That way we know if we didn't finish the write successfully.
  for (int i = 0; i < MAXIMUM_KNOCKS; i++) {
    EEPROM.write(i + 1, secretCode[i]);
  }
  EEPROM.write(0, EEPROM_CHECKSUM);  // all good. Write the signature so we'll know it's all good.
}

// Plays back the pattern of the knock in blinks and beeps
void playbackKnock(int maxKnockInterval) {
  LedWrite(false);
  delay(1000);
  LedWrite(true);
  chirp(50, CHIRP_2KHZ);
  for (int i = 0; i < MAXIMUM_KNOCKS ; i++) {
    LedWrite(false);
    // only turn it on if there's a delay
    if (secretCode[i] > 0) {
      delay(map(secretCode[i], 0, 100, 0, maxKnockInterval)); // Expand the time back out to what it was. Roughly.
      LedWrite(true);
      chirp(50, CHIRP_2KHZ);
    }
  }
  LedWrite(false);
}

// Deals with the knock delay thingy.
void knockDelay() {
  int itterations = (KNOCK_FADE_TIME / 20);      // Wait for the peak to dissipate before listening to next one.
  for (int i = 0; i < itterations; i++) {
    nonBlockDelayMS(10);
    analogRead(PIN_ANALOG_KNOCK_SENSE);                  // This is done in an attempt to defuse the analog sensor's capacitor that will give false readings on high impedance sensors.
    nonBlockDelayMS(10);
  }
}

// Plays a non-musical tone on the piezo.
// playTime = milliseconds to play the tone
// delayTime = time in microseconds between ticks. (smaller=higher pitch tone.)
void chirp(int playTime, int delayTime) {///FUNCTION TO MAKE BUZZES
  long loopTime = (playTime * 1000L) / (delayTime);
  for (int i = 0; i < loopTime; i++) {
    PORTB ^= (1 << PB1); //TOGGLE BUZZER
    delayMicroseconds(delayTime);
  }
  PORTB &= ~(1 << PB1);//BUZZER PIN OFF JUST IN CASE
}

void LedWrite(bool input)
{
  if (input)
  {
    digitalWrite(PIN_LED_PGM_BUTTON, LOW);
    pinMode(PIN_LED_PGM_BUTTON, OUTPUT);
    digitalWrite(PIN_LED_PGM_BUTTON, LOW);
  }
  else
  {
    pinMode(PIN_LED_PGM_BUTTON, INPUT);
  }
  ledStatus = input;
}
bool readPGMButton()
{
  pinMode(PIN_LED_PGM_BUTTON, INPUT);
  //delayMicroseconds(10);
  bool retVal = digitalRead(PIN_LED_PGM_BUTTON);
  LedWrite(ledStatus);
  return retVal;
}

void nonBlockDelayMS(unsigned long ms)
{
  unsigned long millisendenter = millis();
  while ( millis() - millisendenter < ms)
    importtantToLoop();
}



void importtantToLoop()
{
  unsigned long currentMillis = millis();
  if (!digitalRead(PIN_MANUAL_OPEN))//////ACTIVE LOW SIGNAL
  {
    if ((!actuated) && (currentMillis - previousMillisManualOpen >= MANUAL_OPEN_DEBOUNCE))
    {
      actuated = true;
      doorUnlock(OPEN_HOLD_TIME);
    }
  }
  else
  {
    previousMillisManualOpen = currentMillis;
    actuated = false;
  }

  if (!readPGMButton())//////ACTIVE LOW SIGNAL
  {
    if (currentMillis - previousMillisProgramButton >= PROGRAM_BUTTON_DEBOUNCE)
    {
      lockoutSecs = 0; ///manually reset counter for loking down
      wrongAttempts = 0; //reset wrong login attempts
      if (!programModeActive) {    // If we're not in programming mode, turn it on.
        programModeActive = true;          // Remember we're in programming mode.
        LedWrite(true);//LED ON       // Turn on the red light too so the user knows we're programming.
        chirp(100, CHIRP_1KHZ);                  // And play a tone in case the user can't see the LED.
        chirp(100, CHIRP_2KHZ);
      } else {                             // If we are in programing mode, turn it off.
        programModeActive = false;
        LedWrite(false);//LED OFF
        chirp(100, CHIRP_2KHZ);                  // Turn off the programming LED and play a sad note.
        chirp(100, CHIRP_1KHZ);
        delay(500);
      }
      while (!readPGMButton()) {
        delay(10);                         // Hang around until the button is released.
      }
      delay(1000);//chep debounce
    }
  }
  else
  {
    previousMillisProgramButton = currentMillis;
  }

  if ((lockoutSecs > 0) && ((currentMillis - prewMillisSeconds) >= 1000))
  {
    prewMillisSeconds = millis();
    lockoutSecs--;

    if (lockoutSecs == 0)
      wrongAttempts = ATTEMPTS_BEFORE_LOCKOUT - 3;
  }

}
