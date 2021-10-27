/*
  xsns_88_RC_Touch.ino - Capacitive Touch input support for Tasmota on ESP8266

  Copyright (C) 2021  Theo Arends, Jeroen Janssen (Xanore)

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

#ifdef USE_XSNS_88_RC_TOUCH_BTN

/*********************************************************************************************\
 * 
 * xsns_88_RC_Touch.ino - Capacitive Touch input support for Tasmota on ESP8266
 * 
 * Based on CapacitiveSensor library v0.5.1 from Paul Bagder, Paul Stoffregen
 * https://github.com/PaulStoffregen/CapacitiveSensor
 * 
 * Can be used for a smart touch-lamp by sending MQTT messages when touching the (metal) housing.
 * Add 'USE_XSNS_88_RC_TOUCH_BTN' to user_config_override.h to include this functionality
 * 
 * Hardware
 *    Place a ~10k resistor between two digital IO pins. Connect one of pins to the (metal) object
 *    used as sensor. 
 *    NOTE: The resistor value depends on the used object. If the value is negative,
 *          try a lower resistor value. If the value is to high, try a higer resistor value. Use 
 *          the command 'RCTLogRawData' to check the value while released and touched.
 *   
 * 
 * Configuration
 *    - Configure a digital GPIO pin as 'Touch Btn Out'. This should connect to only the resistor.
 *    - Configure a digital GPIO pin as 'Touch Btn In'. This should connect the the resistor and 
 *      the wire going to the object
 *    NOTE: GPIO D4 and D2 work fine. When selecting a pin wich is combined with RX/TX the ESP8266
 *          might keep rebooting.
 * 
 * Electric circuit
 * 
 *                  Resistor
 *    GPIO D4 >-------<10K>---+-----> Touch wire to object
 *    'Touch Btn Out'         |
 *                            |
 *                            |
 *    GPIO D2 <---------------+
 *    'Touch Btn In'  
 * 
 * 
 * Commands
 *
 * SetOption13 {0..1}
 *            get/set how the touch button responses (almost identical to regular buttons)
 *            (1) - Direct response. Action 'TOUCHED', 'RELEASED'
 *            (0) - Hold option. Action 'TOUCHED' send on short touch release.
 *                               Action 'HOLD' send on long holding
 * 
 * SetOption32 {0..100}
 *            get/set the duration for the HOLD action is send. (identical to regular buttons)
 *            Duration 0..100 in 0.1sec
 * 
 * RCTThreshold {0..1000}
 *            get/set the threshold (integer) for the capacitive sensor to toggle a 
 *            touched/released signal. typically this is set around 100 but might differ 
 *            depending on the size of the touch area. Setting is persistent.
 *
 * RCTLogRawData {0..1}
 *            get/set logging of raw sensor data. Data is send every 1 seconds and can be
 *            used. Setting is not persistent.
 * 
 *********************************************************************************************/

#define XSNS_88 88
#include <CapacitiveSensor.h>

// Number of samples used to filter out any spikes
#define AVERAGECOUNT 5

struct xsns_88_rcTouchBtn
{
  bool present = false;
  bool ready = false;
  CapacitiveSensor *sensor;
  uint8_t readSensorSamples = 0;
  uint16_t rawTouchRead[AVERAGECOUNT];
  uint8_t i_rawTouchRead = 0;
  uint16_t filteredRawTouchRead = 0;
  bool touched = false;
  bool hold = false;
  bool setting_logRawTouch = false;
  uint32_t holdTill_ms = 0;
} rcTouchBtn_data;

void InitRCTouch()
{
  if (!rcTouchBtn_data.ready)
  {
    AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_DEBUG "RC Touch Init"));

    rcTouchBtn_data.present = PinUsed(GPIO_RC_TOUCH_BTN_OUT) && PinUsed(GPIO_RC_TOUCH_BTN_IN);
    if (rcTouchBtn_data.present)
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

uint16_t GetFilteredSensorValue()
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

  return rcTouchBtn_data.filteredRawTouchRead;
}

void GetTouchButtonStatus()
{
  if (!rcTouchBtn_data.present || !rcTouchBtn_data.ready)
    return;

  if (TasmotaGlobal.uptime < 4)
  {
    return;
  } // Block GPIO for 4 seconds after poweron to workaround Wemos D1 / Obi RTS circuit

  uint16_t sensorValue = GetFilteredSensorValue();
  bool touched = sensorValue > Settings->rct_threshold;

  // touch flank
  if (touched != rcTouchBtn_data.touched)
  {
    rcTouchBtn_data.touched = touched;

    if (Settings->flag.button_single)
    { // SetOption13 (0) - Allow only single button press for immediate action,
      MqttTouchButtonTopic(rcTouchBtn_data.touched, false);
    }
    else
    { // SetOption13 (1) - Allow TOUCHED and HOLD actions (No RELEASED is send), TOUCH is send on release so there will be a delay
      if (rcTouchBtn_data.touched)
      {
        // SetOption32 (40) - Button held for factor times longer 0...100 * 0.1 sec
        rcTouchBtn_data.holdTill_ms = (Settings->param[P_HOLD_TIME] * 100) + millis();
      }
      else // released
      {
        if (!rcTouchBtn_data.hold)
          MqttTouchButtonTopic(true, false);
        rcTouchBtn_data.hold = false;
        rcTouchBtn_data.holdTill_ms = 0;
      }
    }
  }

  // Check if touch button is hold long enough
  if ((rcTouchBtn_data.holdTill_ms > 0) && !rcTouchBtn_data.hold && (rcTouchBtn_data.holdTill_ms < millis()))
  {
    MqttTouchButtonTopic(false, true);
    rcTouchBtn_data.hold = true;
  }
}

void MqttTouchButtonTopic(bool touched, bool hold)
{
  char scommand[10];
  snprintf_P(scommand, sizeof(scommand), PSTR(D_JSON_TOUCH_BTN));
  char mqttstate[7];

  if (hold)
    Response_P(S_JSON_SVALUE_ACTION_SVALUE, scommand, D_JSON_HOLD);
  else
    Response_P(S_JSON_SVALUE_ACTION_SVALUE, scommand, (touched) ? D_JSON_TOUCHED : D_JSON_RELEASED);

  MqttPublishPrefixTopicRulesProcess_P(RESULT_OR_STAT, scommand);
}

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

#define D_PRFX_RC_TOUCH "RCT"
#define D_CMND_RCT_THRESHOLD "Threshold"
#define D_CMND_RCT_LOGRAWDATA "ShowSensorData"

const char kRCTCommands[] PROGMEM = D_PRFX_RC_TOUCH "|" // Prefix
    D_CMND_RCT_THRESHOLD "|" D_CMND_RCT_LOGRAWDATA;

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
    rcTouchBtn_data.setting_logRawTouch = (XdrvMailbox.data[0] == '1') ? 1 : 0;
  }
  ResponseCmndNumber(rcTouchBtn_data.setting_logRawTouch);
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
    InitRCTouch();
    break;
  case FUNC_EVERY_50_MSECOND:
    GetTouchButtonStatus();
    break;
  case FUNC_EVERY_SECOND:
    if (rcTouchBtn_data.setting_logRawTouch)
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

#endif // USE_XSNS_88_RC_TOUCH_BTN
