/****************************************************************
    NESPi NDEF Reader/Power Controlller v0.1  [mike.g|jun2016]
 ****************************************************************/
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <math.h>
#include <SPI.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <NfcAdapter.h>
#include <EEPROM.h>
#include "FastLED.h"

const int buttonPin = 2;       // input pin for the NES 'reset' button
const int powerPin = A2;       // output to MOSFET
const int piPin = A3;          // input for RPi status
const int ledPin = 5;          // front LED indicator

boolean bootState = 0;        // boot status variable
boolean tagToggle = true;     // toggle variable for tag reader
boolean buttonWasAsleep = 0;  // if we've just woken from sleep
boolean piVal;                // value read from piPin
boolean piLast;               // last value of piPin

#define NUM_LEDS 1      // number of LEDs(could expand?)
#define DATA_PIN A1     // pin for LED data
CRGB leds[NUM_LEDS];    // LED array
boolean ledEnable;      // led enable state

String piMsg;           // String to hold the serial data from the Pi
long resetTime;         // used to check if the Pi was reset or shutdown

PN532_SPI pn532spi(SPI, 10);              // NFC connected to the SPI bus
NfcAdapter nfc = NfcAdapter(pn532spi);    // PN532 'NFC MODULE V3' by elechouse
const int nfcPin = A0;                    // PN532 RSTPDN to sleep NFC reader when pulled LOW
boolean nfcWasAsleep = 0;                 // NFC reader sleep value

/*****************************************************************************************
 *****************************************************************************************/
void setup() {

  // start the serial port
  Serial.begin(9600);

  // start the NFC reader in power-down state by driving the PN532 RSTPDN pin LOW
  pinMode(nfcPin, OUTPUT);
  digitalWrite(nfcPin, LOW);

  // Set-up the RGB LED
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
  FastLED.setBrightness(255);

  // Set-up the button
  pinMode(buttonPin, INPUT_PULLUP);

  // Set-up the indicator LED and MOSFET outputs
  pinMode(powerPin, OUTPUT);
  digitalWrite(powerPin, LOW);        // ensure MOSFET is off when Arduino starts
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  // Read stored preferences from EEPROM
  ledEnable = EEPROM.read(0);

  // set  pull-up resistors on unused pins
  pinMode(3, INPUT_PULLUP);
  pinMode(4, INPUT_PULLUP);
  for (int i = 6; i < 10; i++)pinMode(i, INPUT_PULLUP);

  // after setting up go to sleep to save power at idle
  go_to_sleep();

}
/*****************************************************************************************
 *****************************************************************************************/
void loop() {

  // restart the nfc reader after waking from sleep
  if (nfcWasAsleep) {
    digitalWrite(nfcPin, HIGH);      // set RSTPDN HIGH to enable NFC reader
    pinMode(nfcPin, INPUT_PULLUP);   // set input pullup resistor to limit current
    delay(100);                      // wait for NFC reader to settle
    nfc.begin();                     // initialise NFC reader
    nfcWasAsleep = 0;                // toggle the bit so we only do this once
  }


  // Get button event
  int b = checkButton();

  if (b == 1) nesReset();             // single button click
  if (b == 2) ledToggle();            // double-click
  if (b == 3) powerOn();              // press and hold
  if (b == 4) shutdownPi();           // press and long-hold


  // poll the piPin to detect a software shutdown of the Raspberry Pi
  // if piPin goes from HIGH to LOW, then the RPi has inititated a shut-down or reboot
  piVal = digitalRead(piPin);
  if (bootState == 1) {                                       // only check after boot-up
    if (piVal == 0 && piLast == 1) resetTime = millis();      // start a timer if piPin falling edge
  }
  piLast = piVal;
  // if piPin is still low after 12 seconds then the Pi has shut down not reset, and we can cut the power
  if (millis() - resetTime > 12000) if (digitalRead(piPin) == 0 && bootState == 1) powerOff();

  scanTag();
  readPi();

  // set the LED colour if the RPi hasn't started (button wasn't held long enough when waking up)
  if (bootState == 0)leds[0] = CRGB::DarkRed; FastLED.show();

}
/*****************************************************************************************
 *****************************************************************************************/
void nesReset() {

  // send 'reset' to the Pi to trigger a reset of the emulator
  Serial.print("reset"); Serial.print(", "); Serial.println(", ");

}
/*****************************************************************************************
 *****************************************************************************************/
void ledToggle() {

  // toggle the ledEnable bit
  ledEnable = !ledEnable;
  // save the setting to EEPROM
  EEPROM.write(0, ledEnable);

  // turn LED(s) off if we need to
  if (ledEnable == 0) leds[0] = CRGB::Black; FastLED.show();

  // set tagToggle to refresh the LED colour(read the tag again)
  tagToggle = 1;

}
/*****************************************************************************************
 *****************************************************************************************/
void powerOn() {

  if (bootState == 0) {

    // flash the RGB Led twice in white
    leds[0] = CRGB::White; FastLED.show(); delay(100);
    leds[0] = CRGB::Black; FastLED.show(); delay(100);
    leds[0] = CRGB::White; FastLED.show(); delay(100);
    leds[0] = CRGB::Black; FastLED.show();

    // turn on the MOSFET
    digitalWrite(powerPin, HIGH);
    // turn on the LED indicator
    digitalWrite(ledPin, HIGH);

    // wait for the RPi to finish boot before continuing, lazy, but prevents additional button triggers
    while (digitalRead(piPin) == 0) {
      // pulse the LED indictor slowly during boot-up
      float val = (exp(sin(millis() / 1500.0 * PI)) - 0.36787944) * 108.0;
      analogWrite(ledPin, val);
    }

    // boot-up has finished here
    // turn on LED indicator
    digitalWrite(ledPin, HIGH);
    delay(500);
    bootState = 1;

  }
}
/*****************************************************************************************
 *****************************************************************************************/
void powerOff() {

  // turn off the RGB LED
  leds[0] = CRGB::Black; FastLED.show();

  // pulse the LED indicator for 5 more seconds before cutting power to the Pi
  for (int i = 0; i < 500; i++) {
    float val = (exp(sin(millis() / 500.0 * PI)) - 0.36787944) * 108.0;
    analogWrite(ledPin, val);
    delay(10);
  }
  digitalWrite(powerPin, LOW);    // cut power to the Pi
  bootState = 0;
  pinMode(nfcPin, OUTPUT);
  digitalWrite(nfcPin, LOW);      // put NFC reader into shutdown state
  digitalWrite(ledPin, LOW);      // turn off indicator LED
  go_to_sleep();

}
/*****************************************************************************************
 *****************************************************************************************/
void shutdownPi() {

  // send shutdown message to Raspberry Pi
  Serial.print("shutdown"); Serial.print(", "); Serial.println(", ");

  // flash the RGB Led twice in red
  leds[0] = CRGB::Red; FastLED.show(); delay(100);
  leds[0] = CRGB::Black; FastLED.show(); delay(100);
  leds[0] = CRGB::Red; FastLED.show(); delay(100);
  leds[0] = CRGB::Black; FastLED.show();

  // could do a check/response over serial here?(can't be bothered)
  // wait for the RPi signal to go LOW before cutting the power
  while (digitalRead(piPin) == 1) {
    // pulse the LED a bit faster during shutdown
    float val = (exp(sin(millis() / 500.0 * PI)) - 0.36787944) * 108.0;
    analogWrite(ledPin, val);
  }

  // shutdown has finished here
  powerOff();

}

/*****************************************************************************************
 *****************************************************************************************/
void scanTag () {

  if (nfc.tagPresent(100))     // timeout=100, balance between reads and button responsiveness
  {
    if (tagToggle) {
      NfcTag tag = nfc.read();
      // Serial.println(tag.getTagType());
      Serial.print(tag.getUidString()); Serial.print(", ");    // print the UID to serial monitor
      if (tag.hasNdefMessage()) {
        NdefMessage message = tag.getNdefMessage();
        // get the first 2 records of the tag
        for (int i = 0; i < 2; i++)
        {
          NdefRecord record = message.getRecord(i);
          int payloadLength = record.getPayloadLength();
          byte payload[payloadLength];
          record.getPayload(payload);
          // force the data into a String (should be ok for plain text fields)
          String payloadAsString = "";
          // start 3 characters from the left to remove garbage(language characters?)
          for (int c = 3; c < payloadLength; c++) {
            payloadAsString += (char)payload[c];
          }
          Serial.print(payloadAsString); Serial.print(", ");    // print the NDEF record
        }
        Serial.println(" ");
        tagToggle = 0;
      }
    }
  }
  else {
    // when there's no tag present
    if (ledEnable && bootState) {
      leds[0] = CRGB::OrangeRed; FastLED.show();
    }
    if (tagToggle == 0) {
      Serial.print("cart_eject"); Serial.print(", "); Serial.println(", ");
      tagToggle = 1;
    }
  }
}
/*****************************************************************************************
 *****************************************************************************************/
void readPi() {

  // read data from the serial port into a string to check for messages
  if (Serial.available()) {
    piMsg = Serial.readString();

    // toggle the tag reader when prompted by the RPi... lets us start with a cart insterted
    if (piMsg == "ready") {
      tagToggle = true;
    }

    // change the LED colour depending on the message recieved from the Raspbeery Pi
    else if (piMsg == "ok" && ledEnable == 1) {
      leds[0] = CRGB::Green; FastLED.show();
    }
    else if (piMsg == "bad" && ledEnable == 1) {
      leds[0] = CRGB::Red; FastLED.show();
    }
  }
}
/*****************************************************************************************
 *****************************************************************************************/
void go_to_sleep() {

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);    // set the sleep mode
  sleep_enable();                         // enable sleep bit so sleep is possible
  attachInterrupt(0, wakeUp, LOW);        // attach pin 2 interrupt to wake up from sleep
  delay(100);
  sleep_mode();                           // go to sleep
  sleep_disable();                        // wake up here
  detachInterrupt(0);                     // detach the interrupt after waking up
}

void wakeUp() {

  // set toggle bits to let the main program know we just woke up
  nfcWasAsleep = 1;
  tagToggle = 1;
  buttonWasAsleep = 1;
  bootState = 0;
}
/*****************************************************************************************
 *****************************************************************************************/
/*=========================================================================================
    MULTI-CLICK:  One Button, Multiple Events
    By Jeff Saltzman
    Oct. 13, 2009

    http://jmsarduino.blogspot.co.uk/2009/10/4-way-button-click-double-click-hold.html
  ==========================================================================================*/

// Button timing variables
int debounce = 20;          // ms debounce period to prevent flickering when pressing or releasing the button
int DCgap = 250;            // max ms between clicks for a double click event
int holdTime = 1000;        // ms hold period: how long to wait for press+hold event
int longHoldTime = 2500;    // ms long hold period: how long to wait for press+hold event

// Button variables
boolean buttonVal = HIGH;   // value read from button
boolean buttonLast = HIGH;  // buffered value of the button's previous state
boolean DCwaiting = false;  // whether we're waiting for a double click (down)
boolean DConUp = false;     // whether to register a double click on next release, or whether to wait and click
boolean singleOK = true;    // whether it's OK to do a single click
long downTime = -1;         // time the button was pressed down
long upTime = -1;           // time the button was released
boolean ignoreUp = false;   // whether to ignore the button release because the click+hold was triggered
boolean waitForUp = false;        // when held, whether to wait for the up event
boolean holdEventPast = false;    // whether or not the hold event happened already
boolean longHoldEventPast = false;// whether or not the long hold event happened already

int checkButton() {

  /****************************************************************************************************/
  // hacky bit I added here :/ because I use the same button to interrupt sleep it would already be
  // pressed when we check it here, so that we can just keep it held down to power back up we want
  // buttonLast to be HIGH after waking from sleep so that this function acts as if it was pressed

  if (buttonWasAsleep) {
    buttonLast = 1;
    buttonWasAsleep = 0;
  }
  /****************************************************************************************************/

  int event = 0;
  buttonVal = digitalRead(buttonPin);

  // Button pressed down
  if (buttonVal == LOW && buttonLast == HIGH && (millis() - upTime) > debounce)
  {
    downTime = millis();
    ignoreUp = false;
    waitForUp = false;
    singleOK = true;
    holdEventPast = false;
    longHoldEventPast = false;
    if ((millis() - upTime) < DCgap && DConUp == false && DCwaiting == true)  DConUp = true;
    else  DConUp = false;
    DCwaiting = false;
  }
  // Button released
  else if (buttonVal == HIGH && buttonLast == LOW && (millis() - downTime) > debounce)
  {
    if (not ignoreUp)
    {
      upTime = millis();
      if (DConUp == false) DCwaiting = true;
      else
      {
        event = 2;
        DConUp = false;
        DCwaiting = false;
        singleOK = false;
      }
    }
  }
  // Test for normal click event: DCgap expired
  if ( buttonVal == HIGH && (millis() - upTime) >= DCgap && DCwaiting == true && DConUp == false && singleOK == true && event != 2)
  {
    event = 1;
    DCwaiting = false;
  }
  // Test for hold
  if (buttonVal == LOW && (millis() - downTime) >= holdTime) {
    // Trigger "normal" hold
    if (not holdEventPast)
    {
      event = 3;
      waitForUp = true;
      ignoreUp = true;
      DConUp = false;
      DCwaiting = false;
      //downTime = millis();
      holdEventPast = true;
    }
    // Trigger "long" hold
    if ((millis() - downTime) >= longHoldTime)
    {
      if (not longHoldEventPast)
      {
        event = 4;
        longHoldEventPast = true;
      }
    }
  }
  buttonLast = buttonVal;
  return event;
}