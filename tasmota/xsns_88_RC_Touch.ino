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

#warning JJN Remove this define
#define XSNS_88_USE_RC_TOUCH_BTN

#ifdef XSNS_88_USE_RC_TOUCH_BTN
/*********************************************************************************************\
 * Touch support for ESP8266 using two GPIO channels, a resistor and optional capacitor
\*********************************************************************************************/

#define XSNS_88 88

#include <CapacitiveSensor.h>

// Number of samples used to filter out any spikes
#define AVERAGECOUNT 5

struct xsns_88_rcTouchBtn
{
  bool ready = false;
  CapacitiveSensor *sensor;
  uint8_t readSensorSamples = 0;
  uint16_t rawTouchRead[AVERAGECOUNT];
  uint8_t i_rawTouchRead = 0;
  uint16_t filteredRawTouchRead = 0;
  bool touched = false;
  bool logRawTouch = false;
} rcTouchBtn_data;

void RcTouchBtnInit()
{
  if (!rcTouchBtn_data.ready)
  {
    AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_DEBUG "RC Touch Init"));
    if (PinUsed(GPIO_RC_TOUCH_BTN_OUT) && PinUsed(GPIO_RC_TOUCH_BTN_IN))
    {
      rcTouchBtn_data.sensor = new CapacitiveSensor(Pin(GPIO_RC_TOUCH_BTN_OUT), Pin(GPIO_RC_TOUCH_BTN_IN));

      rcTouchBtn_data.readSensorSamples = 100;
      rcTouchBtn_data.i_rawTouchRead = 0;

      // Init touch threshold to something usefull
      if (Settings->rct_threshold == 0)
        Settings->rct_threshold = 100;

      // Fill touch array with values
      for (int i_raw = 0; i_raw < AVERAGECOUNT; i_raw++)
      {
        rcTouchBtn_data.rawTouchRead[i_raw] = rcTouchBtn_data.sensor->capacitiveSensor(rcTouchBtn_data.readSensorSamples);
      }
      rcTouchBtn_data.ready = true;
      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_DEBUG "RC Touch Initialized on IO: OUT=%d, IN=%d"), Pin(GPIO_RC_TOUCH_BTN_OUT), Pin(GPIO_RC_TOUCH_BTN_IN));
    }
  }
}

bool SensorIsTouched()
{
  if (rcTouchBtn_data.ready)
  {
    // Read raw value into shift register
    rcTouchBtn_data.rawTouchRead[rcTouchBtn_data.i_rawTouchRead] = rcTouchBtn_data.sensor->capacitiveSensor(rcTouchBtn_data.readSensorSamples);
    rcTouchBtn_data.i_rawTouchRead++;
    if (rcTouchBtn_data.i_rawTouchRead >= AVERAGECOUNT)
      rcTouchBtn_data.i_rawTouchRead = 0;

    // Calculate average
    uint16_t sum = 0;
    for (uint8_t i = 0; i < AVERAGECOUNT; i++)
    {
      sum += rcTouchBtn_data.rawTouchRead[i];
    }
    rcTouchBtn_data.filteredRawTouchRead = (sum / AVERAGECOUNT);

    return rcTouchBtn_data.filteredRawTouchRead > Settings->rct_threshold;
  }
  return false;
}

uint8_t RcTouchGetButton()
{
  bool touched = SensorIsTouched();
  if (touched != rcTouchBtn_data.touched)
  {
    rcTouchBtn_data.touched = touched;
    MqttTouchButtonTopic(rcTouchBtn_data.touched);
    if (rcTouchBtn_data.touched)
      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_APPLICATION "Touchbutton touched"));
    else
      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_APPLICATION "Touchbutton released"));
  }
  return touched;
}

void MqttTouchButtonTopic(bool touched)
{
  char scommand[10];
  snprintf_P(scommand, sizeof(scommand), PSTR(D_JSON_BUTTON));
  char mqttstate[7];
#warning JJN: Move touched and released to a generic plase
  Response_P(S_JSON_SVALUE_ACTION_SVALUE, scommand, (touched) ? D_JSON_TOUCHED : D_JSON_RELEASED);
  MqttPublishPrefixTopicRulesProcess_P(RESULT_OR_STAT, scommand);
}

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

/*********************************************************************************************\
 * Commands
 *
 * RCTThreshold {threshold}
 *            get/set the threshold (integer) for the capacitive sensor to toggle a 
 *            touched/released signal. typically this is set around 100 but might differ 
 *            depending on the size of the touch area. Setting is persistent.
 *
 * RCTLogRawData {0/1}
 *            get/set logging of raw sensor data. Data is send every 1 seconds and can be
 *            used. Setting is not persistent.
 * 
 *********************************************************************************************/

#define D_PRFX_RC_TOUCH "RCT"
#define D_CMND_RCT_THRESHOLD "Threshold"
#define D_CMND_RCT_LOGRAWDATA "ShowSensorData"

const char kRCTCommands[] PROGMEM = D_PRFX_RC_TOUCH "|" // Prefix
    D_CMND_RCT_THRESHOLD "|"
    D_CMND_RCT_LOGRAWDATA;

void (*const RCTCommand[])(void) PROGMEM = {
    &CmndTouchThreshold,
    &CmndLogRawData};

void CmndTouchThreshold()
{
  if (XdrvMailbox.data_len > 0)
  {
    Settings->rct_threshold = strtoul(XdrvMailbox.data, nullptr, 0);
  }
  ResponseCmndNumber(Settings->rct_threshold);
}

void CmndLogRawData()
{
  if (XdrvMailbox.data_len == 1)
  {
    rcTouchBtn_data.logRawTouch = (XdrvMailbox.data[0] == '1') ? 1 : 0;
  }
  ResponseCmndNumber(rcTouchBtn_data.logRawTouch);
}

void CmndError(void)
{
  Response_P(PSTR("{\"" D_JSON_COMMAND "\":\"" D_JSON_ERROR "\"}"));
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xsns88(byte callback_id)
{
  bool result = false;

  // Check which callback ID is called by Tasmota
  switch (callback_id)
  {
  case FUNC_INIT:
    RcTouchBtnInit();
    break;
  case FUNC_EVERY_50_MSECOND:

    RcTouchGetButton();

    break;
  case FUNC_EVERY_SECOND:
    if (rcTouchBtn_data.logRawTouch)
    {
      AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_APPLICATION "RC Touch RAW value %d"), rcTouchBtn_data.filteredRawTouchRead);
    }
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
    result = DecodeCommand(kRCTCommands, RCTCommand);
    break;
  }
  return result;
}

#endif // XSNS_88_USE_RC_TOUCH_BTN
