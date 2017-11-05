/*
MD_UISwitch class implementation.

See main header file for information.
*/

#include "MD_UISwitch.h"

/**
 * \file
 * \brief Main code file for MD_UISwitch library
 */

#define UI_DEBUG 0  ///< Library debugging flag

#if UI_DEBUG
#define UI_PRINTS(s)   { Serial.print(F(s)); }                  ///< Debugging macro
#define UI_PRINT(s, v) { Serial.print(F(s)); Serial.print(v); } ///< Debugging macro
#else
#define UI_PRINTS(s)    ///< Debugging macro
#define UI_PRINT(s, v)  ///< Debugging macro
#endif

MD_UISwitch::MD_UISwitch(void) : _state(S_IDLE)
{
  setDebounceTime(KEY_DEBOUNCE_TIME);
  setDoublePressTime(KEY_DPRESS_TIME);
  setLongPressTime(KEY_LONGPRESS_TIME);
  setRepeatTime(KEY_REPEAT_TIME);
}

MD_UISwitch::keyResult_t MD_UISwitch::processFSM(bool b, uint8_t keyID)
// return one of the keypress types depending on what has been detected
{
  keyResult_t k = KEY_NULL;

  switch (_state)
  {
    case S_IDLE:    // waiting for first transition
      //UI_PRINTS("\nS_IDLE");
      if (b)
      {
        _state = S_DEBOUNCE1;   // edge detected, initiate debounce
        _timeActive = millis();
      }
      break;

    case S_DEBOUNCE1:
      UI_PRINTS("\nS_DEBOUNCE1");
      // Wait for debounce time to run out and ignore any key switching
      // that might be going on as this may be bounce
      if ((millis() - _timeActive) < _timeDebounce)
        break;

      // after debounce - possible long or double press so move to next state
      _state = S_PRESS;
      // fall through

    case S_PRESS:   // press?
      UI_PRINTS("\nS_PRESS");
      // Key off before a long press registered, so it is either double press or a press.
      if (!b)
      {
        if (bitRead(_enableFlags, DPRESS_ENABLE))  // DPRESS allowed
          _state = S_DPRESS;
        else      // this is just a press
        {
          k = KEY_PRESS;
          _state = S_IDLE;
        }
      }
      else  // if (b)
      {
        // if the switch is still on and we have not run out of longpress time, then
        // just wait for the timer to expire
        if (millis() - _timeActive < _timeLongPress)
          break;

        // time has run out, we either have a long press or are heading
        // towards repeats if they are enabled
        if (bitRead(_enableFlags, LONGPRESS_ENABLE) || bitRead(_enableFlags, REPEAT_ENABLE))
        {
          _timeActive = millis();   // reset for repeat timer base
          _state = S_LPRESS;
        }
        else    // no other option as we have detected something!
        {
          k = KEY_PRESS;
          _state = S_WAIT;
        }
      }
      break;

    case S_LPRESS:  // long press or auto repeat?
      UI_PRINTS("\nS_LPRESS");
      // It is a long press if
      // - Key off before a repeat press is registered, or
      // - Auto repeat is disabled
      // Set the return code and go back to waiting
      if (!b || !bitRead(_enableFlags, REPEAT_ENABLE))
      {
        k = bitRead(_enableFlags, LONGPRESS_ENABLE) ? KEY_LONGPRESS : KEY_PRESS;
        _state = S_WAIT;
        break;
      }
      // fall through but remain in this state

    case S_REPEAT: // repeat?
      // Key off before another repeat press is registered, so we have finished.
      // Go back to waiting
      if (!b)
      {
        if (_state == S_LPRESS) // did not make the first repeat
          k = bitRead(_enableFlags, LONGPRESS_ENABLE) ? KEY_LONGPRESS : KEY_PRESS;
        _state = S_IDLE;
      }
      else    // if (b)
      {
        // if the switch is still on and we have not run out of repeat time, then
        // just wait for the timer to expire.
        if ((millis() - _timeActive) < _timeRepeat)
          break;

        // we are now sure we have a repeat, set the return code and remain in this
        // state checking for further repeats if enabled
        k = (bitRead(_enableFlags, REPEAT_RESULT_ENABLE) && _state == S_REPEAT) ? KEY_RPTPRESS : KEY_PRESS;
        _state = bitRead(_enableFlags, REPEAT_ENABLE) ? S_REPEAT : S_WAIT;
        _timeActive = millis();	// next key repeat time starts now
      }
      break;

    case S_DPRESS:
      UI_PRINTS("\nS_DPRESS");
      if (!b)
      {
        // we didn't get a second press within time then this was just a press
        if (millis() - _timeActive >= _timeDoublePress)
        {
          k = KEY_PRESS;
          _state = S_IDLE;
        }
      }
      else  // if (b)
      {
        _state = S_DEBOUNCE2;		// edge detected, initiate debounce
        _timeActive = millis();
      }
      break;

    case S_DEBOUNCE2:
      UI_PRINTS("\nS_DEBOUNCE2");
      // Wait for debounce time to run out and ignore any key switching
      // that might be going on as this may be bounce
      if ((millis() - _timeActive) < _timeDebounce)
        break;

      k = b ? KEY_DPRESS : KEY_PRESS;
      _timeActive = millis();
      _state = S_WAIT;
      break;

    case S_WAIT:
    default:
      // After completing while still key active, allow the user to release the switch
      // to meet starting conditions for S_IDLE
      UI_PRINTS("\nS_WAITING");
      if (!b)  _state = S_IDLE;
      break;
  }

  return(k);
}


// -----------------------------------------------
// MD_UISwitch_Digital methods
// -----------------------------------------------
void MD_UISwitch_Digital::begin(void)
{
  UI_PRINTS("\nUISwitch_Digital begin()");
  if (_onState == LOW)
    pinMode(_pin, INPUT_PULLUP);
  else
    pinMode(_pin, INPUT);

  MD_UISwitch::begin();
}

MD_UISwitch::keyResult_t MD_UISwitch_Digital::read(void)
{
  bool b = (digitalRead(_pin) == _onState);

  return(processFSM(b));
}
// -----------------------------------------------

// -----------------------------------------------
// MD_UISwitch_Analog methods
// -----------------------------------------------
void MD_UISwitch_Analog::begin(void)
{
  UI_PRINTS("\nUISwitch_Analog begin()");
  pinMode(_pin, INPUT);

  MD_UISwitch::begin();
}

MD_UISwitch::keyResult_t MD_UISwitch_Analog::read(void)
{
  bool b = false;
  uint8_t v = analogRead(_pin);
  int16_t idx = -1;

  // work out what key this is
  for (uint8_t k = 0; k < _ktSize; k++)
  {
    if ((v >= (_kt[k].adcThreshold - _kt[k].adcTolerance)) &&
        (v <= (_kt[k].adcThreshold + _kt[k].adcTolerance)))
    {
      idx = _kt[k].value;
      break;
    }
  }

  // is this the same as the previous key?
  b = (idx != -1) && (idx == _lastKeyIdx);
  _lastKeyIdx = idx;
  if (idx != -1) _lastKey = _kt[idx].value;

  if (_lastKeyIdx != -1) UI_PRINT("\nKey idx ", idx);

  return(processFSM(b));
}
// -----------------------------------------------

// -----------------------------------------------
// MD_UISwitch_Matrix methods
// -----------------------------------------------
void MD_UISwitch_Matrix::begin(void)
{
  UI_PRINTS("\nUISwitch_Matrix begin()");

  // initialise the hardware
  for (uint8_t i = 0; i < _rows; i++) 
    pinMode(_rowPin[i], INPUT_PULLUP);

  // columns initialise during scanning
  //for (uint8_t i = 0; i < _rows; i++)
  //  pinMode(_colPin[i], OUTPUT);

  MD_UISwitch::begin();
}

MD_UISwitch::keyResult_t MD_UISwitch_Matrix::read(void)
{
  bool b = false;
  uint16_t idx = -1;

  // scan the keypad and stop at the first key detected
  for (uint8_t c = 0; c < _cols && idx == -1; c++) 
  {
    pinMode(_colPin[c], OUTPUT);
    digitalWrite(_colPin[c], LOW);	    // column pulse
    for (uint8_t r = 0; r < _rows; r++)
    {
      if (digitalRead(_rowPin[r]) == LOW)
      {
        idx = (r * _rows) + c;
        break;
      }
    }
    digitalWrite(_colPin[c], HIGH);     // end column pulse
    pinMode(_colPin[c], INPUT);         // set high impedance
  }

  // is this the same as the previous key?
  b = (idx != -1) && (idx == _lastKeyIdx);
  _lastKeyIdx = idx;
  if (idx != -1) _lastKey = _kt[idx];

  if (_lastKeyIdx != -1) UI_PRINT("\nKey idx ", idx);

  return(processFSM(b));
}
// -----------------------------------------------