/*
  xsns_88_RC_Touch.ino - Capacitive Touch input support for Tasmota

  Copyright (C) 2021  Theo Arends, Jeroen Janssen

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#warning JJN: Should be removed!!!
#define USE_RC_TOUCH_BTN

#ifdef USE_RC_TOUCH_BTN
/*********************************************************************************************\
 * Touch support for ESP8266 using two GPIO channels, a resistor and optional capacitor
\*********************************************************************************************/

#define XSNS_88 88

bool SensorIsTouched()
{
  
}

uint8_t RcTouchGetButton(int pinTouchIn, int pinTouchOut) {

  AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_DEBUG "xsns_88.RcTouchGetButton pinTouchIn=%d, pinTouchOut=%d"), pinTouchIn, pinTouchOut);
  // for (uint32_t idx = 0; idx < Adcs.present; idx++) {
  //   if (Adc[idx].pin == pin) {
  //     if (ADC_BUTTON_INV == Adc[idx].type) {
  //       return (AdcRead(Adc[idx].pin, 1) < Adc[idx].param1);
  //     }
  //     else if (ADC_BUTTON == Adc[idx].type) {
  //       return (AdcRead(Adc[idx].pin, 1) > Adc[idx].param1);
  //     }
  //   }
  // }
  return 0;
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns88(byte callback_id)
{
  bool result = false;

  // Check which callback ID is called by Tasmota
  switch (callback_id) {
    case FUNC_INIT:
      break;
    case FUNC_EVERY_50_MSECOND:
      break;
    case FUNC_EVERY_SECOND:
      break;
    case FUNC_JSON_APPEND:
      break;
#ifdef USE_WEBSERVER
    case FUNC_WEB_SENSOR:
      break;
#endif // USE_WEBSERVER
    case FUNC_SAVE_BEFORE_RESTART:
      break;
    case FUNC_COMMAND:
      break;
  }
  return result;
}

#endif // USE_RC_TOUCH_BTN
