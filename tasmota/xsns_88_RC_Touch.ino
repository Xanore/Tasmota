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
#define XSNS_88 88

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
 * Compile
 *    Add make sure to add #define XSNS_88_USE_RC_TOUCH_BTN to add this functionality to the build
 * 
 * Configuration
 *    - Use Tasmota template {"NAME":"WEMOS D1 Touch","GPIO":[1,1,7360,1,1,7392,1,1,1,1,1,1,1,1],"FLAG":0,"BASE":18}
 *      OR
 *    - Configure a digital GPIO pin as 'Touch Btn Out'. This should connect to only the resistor.
 *    - Configure a digital GPIO pin as 'Touch Btn In'. This should connect the the resistor and 
 *      the wire going to the object
 *    NOTE: WEMOS D1 - GPIO D4 and D1 work fine. When selecting a pin wich is combined with RX/TX 
 *          the ESP8266 might keep rebooting.
 * 
 * Electric circuit
 * 
 *                  Resistor
 *    GPIO D4 >-------<10K>---+-----> Touch wire to object
 *    'Touch Btn Out'         |
 *                            |
 *                            |
 *    GPIO D1 <---------------+
 *    'Touch Btn In'  
 * 
 *   NOTE: Make sure the 0V of the DC circuit is linked to GND to get a stable reading.
 * 
 * Commands
 *
 * RCTLogRawData {0..1}
 *            get/set logging of raw sensor data. Data is send every 1 seconds and can be
 *            used. Setting is not persistent.
 *
 * RCTThreshold {0..1000}
 *            get/set the threshold (integer) for the capacitive sensor to toggle a 
 *            touched/released signal. typically this is set around 100 but might differ 
 *            depending on the size of the touch area. Setting is persistent. 
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
 *********************************************************************************************/

#include <CapacitiveSensor.h>

#define RCTOUCH_BASELINE_UPDATE_MS 1000
#define RCTOUCH_BASELINE_DIVISOR 64
#define RCTOUCH_STUCK_TOUCH_MS (30UL * 60UL * 1000UL)
#define RCTOUCH_CALIBRATION_SAMPLES 4

struct xsns_88_rcTouchBtn
{
  bool present = false;
  bool ready = false;
  CapacitiveSensor *sensor;
  uint8_t readSensorSamples = 0;
  bool calibrated = false;
  int32_t rawTouchRead = 0;
  int32_t rawTouchBaseline = 0;
  int32_t filteredRawTouchRead = 0;
  bool touched = false;
  bool hold = false;
  bool setting_logRawTouch = false;
  uint32_t holdTill_ms = 0;
  uint32_t touchStarted_ms = 0;
  uint32_t lastBaselineUpdate_ms = 0;
} rcTouchBtn_data;

bool RCTouchReadRaw(int32_t *raw)
{
  if (!rcTouchBtn_data.sensor) {
    return false;
  }

  long sensor_value = rcTouchBtn_data.sensor->capacitiveSensorRaw(rcTouchBtn_data.readSensorSamples);

  if (sensor_value < 0) {
    AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_DEBUG "RC Touch invalid reading %ld"), sensor_value);
    return false;
  }

  *raw = sensor_value;
  return true;
}

bool RCTouchCalibrate()
{
  int64_t raw_sum = 0;
  uint8_t raw_count = 0;
  int32_t raw = 0;

  for (uint8_t i = 0; i < RCTOUCH_CALIBRATION_SAMPLES; i++) {
    if (RCTouchReadRaw(&raw)) {
      raw_sum += raw;
      raw_count++;
    }
  }

  if (!raw_count) {
    return false;
  }

  rcTouchBtn_data.rawTouchRead = raw_sum / raw_count;
  rcTouchBtn_data.rawTouchBaseline = rcTouchBtn_data.rawTouchRead;
  rcTouchBtn_data.filteredRawTouchRead = 0;
  rcTouchBtn_data.calibrated = true;
  rcTouchBtn_data.touched = false;
  rcTouchBtn_data.hold = false;
  rcTouchBtn_data.holdTill_ms = 0;
  rcTouchBtn_data.touchStarted_ms = 0;
  rcTouchBtn_data.lastBaselineUpdate_ms = millis();
  AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_APPLICATION "RC Touch calibrated at raw value %ld"), rcTouchBtn_data.rawTouchBaseline);

  return true;
}

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

      // Init touch threshold to something usefull
      if (Settings->rct_threshold == 0)
        Settings->rct_threshold = 100;

      RCTouchCalibrate();
      rcTouchBtn_data.ready = true;
      AddLog(LOG_LEVEL_DEBUG, PSTR(D_LOG_DEBUG "RC Touch Initialized on IO: OUT=%d, IN=%d"), Pin(GPIO_RC_TOUCH_BTN_OUT), Pin(GPIO_RC_TOUCH_BTN_IN));
    }
  }
}

int32_t GetFilteredSensorValue()
{
  int32_t raw = 0;
  if (!RCTouchReadRaw(&raw)) {
    return rcTouchBtn_data.filteredRawTouchRead;
  }

  rcTouchBtn_data.rawTouchRead = raw;
  rcTouchBtn_data.filteredRawTouchRead = raw - rcTouchBtn_data.rawTouchBaseline;

  return rcTouchBtn_data.filteredRawTouchRead;
}

void RCTouchUpdateBaseline(bool touched)
{
  if (touched || !rcTouchBtn_data.calibrated) {
    return;
  }

  uint32_t now = millis();
  if (now - rcTouchBtn_data.lastBaselineUpdate_ms < RCTOUCH_BASELINE_UPDATE_MS) {
    return;
  }
  rcTouchBtn_data.lastBaselineUpdate_ms = now;

  int32_t diff = rcTouchBtn_data.rawTouchRead - rcTouchBtn_data.rawTouchBaseline;
  if (0 == diff) {
    return;
  }

  int32_t adjustment = diff / RCTOUCH_BASELINE_DIVISOR;
  if (0 == adjustment) {
    adjustment = (diff > 0) ? 1 : -1;
  }
  rcTouchBtn_data.rawTouchBaseline += adjustment;
}

void RCTouchCheckStuck(bool touched)
{
  if (!touched) {
    rcTouchBtn_data.touchStarted_ms = 0;
    return;
  }

  uint32_t now = millis();
  if (!rcTouchBtn_data.touchStarted_ms) {
    rcTouchBtn_data.touchStarted_ms = now;
    return;
  }

  if (now - rcTouchBtn_data.touchStarted_ms > RCTOUCH_STUCK_TOUCH_MS) {
    AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_APPLICATION "RC Touch stuck touch detected; recalibrating"));
    RCTouchCalibrate();
  }
}

void GetTouchButtonStatus()
{
  if (!rcTouchBtn_data.present || !rcTouchBtn_data.ready)
    return;

  if (TasmotaGlobal.uptime < 4)
  {
    return;
  } // Block GPIO for 4 seconds after poweron to workaround Wemos D1 / Obi RTS circuit

  int32_t sensorValue = GetFilteredSensorValue();
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
  RCTouchUpdateBaseline(rcTouchBtn_data.touched);
  RCTouchCheckStuck(rcTouchBtn_data.touched);

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
#define D_CMND_RCT_CALIBRATE "Calibrate"

const char kRCTCommands[] PROGMEM = D_PRFX_RC_TOUCH "|" // Prefix
    D_CMND_RCT_THRESHOLD "|" D_CMND_RCT_LOGRAWDATA "|" D_CMND_RCT_CALIBRATE;

void (*const RCTCommand[])(void) PROGMEM = {
    &CmndTouchThreshold,
    &CmndLogRawData,
    &CmndTouchCalibrate};

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

void CmndTouchCalibrate()
{
  ResponseCmndNumber(RCTouchCalibrate() ? 1 : 0);
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
      AddLog(LOG_LEVEL_INFO, PSTR(D_LOG_APPLICATION "RC Touch RAW value %ld, baseline %ld, delta %ld, touched %d"),
             rcTouchBtn_data.rawTouchRead, rcTouchBtn_data.rawTouchBaseline, rcTouchBtn_data.filteredRawTouchRead, rcTouchBtn_data.touched);
    }
    break;
  case FUNC_JSON_APPEND:
    if (rcTouchBtn_data.present && rcTouchBtn_data.ready) {
      ResponseAppend_P(PSTR(",\"RCTouch\":{\"Raw\":%ld,\"Baseline\":%ld,\"Delta\":%ld,\"Threshold\":%d,\"Touched\":%d}"),
                       rcTouchBtn_data.rawTouchRead, rcTouchBtn_data.rawTouchBaseline, rcTouchBtn_data.filteredRawTouchRead,
                       Settings->rct_threshold, rcTouchBtn_data.touched);
    }
    break;
#ifdef USE_WEBSERVER
  case FUNC_WEB_SENSOR:
    if (rcTouchBtn_data.present && rcTouchBtn_data.ready) {
      WSContentSend_PD(PSTR("{s}RC Touch Raw{m}%ld{e}"
                            "{s}RC Touch Baseline{m}%ld{e}"
                            "{s}RC Touch Delta{m}%ld{e}"
                            "{s}RC Touch Threshold{m}%d{e}"
                            "{s}RC Touch State{m}%s{e}"),
                       rcTouchBtn_data.rawTouchRead, rcTouchBtn_data.rawTouchBaseline, rcTouchBtn_data.filteredRawTouchRead,
                       Settings->rct_threshold, rcTouchBtn_data.touched ? D_JSON_TOUCHED : D_JSON_RELEASED);
    }
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
