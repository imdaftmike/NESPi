/************************************************************************************
*             NESPi NES Controller USB Gamepad v0.1 [mike.g|june2016]               *
*                                [daftmike.com]                                     *
*************************************************************************************/
// Adapted from 'JoystickButton' example
// by Matthew Heironimus
// 2015-11-20
// https://github.com/MHeironimus/ArduinoJoystickLibrary

#include <Joystick.h>

void setup() {
  for (int i = 2; i < 10; i++) pinMode(i, INPUT_PULLUP);  // set pullups on pins 2-9 for the buttons
  Joystick.begin();       // initialise Joystick library
}
const int pinToButtonMap = 2;      // start from pin 2
int lastButtonState[8] = {0, 0, 0, 0, 0, 0, 0, 0, 0};   // last state of the button

void loop() {

  for (int index = 0; index < 8; index++)  // go through the loop 8 times, once for each button
  {
    int currentButtonState = !digitalRead(index + pinToButtonMap);  // read the pin and store in variable
    if (currentButtonState != lastButtonState[index])    // if the button state has changed
    {
      Joystick.setButton(index, currentButtonState);    // write the button state to the joystick
      lastButtonState[index] = currentButtonState;    // save state to compare for the next loop
    }
  }
  delay(50);
}