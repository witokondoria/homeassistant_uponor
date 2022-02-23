#define VERSION "1"
#include "secrets.h"

//OTA
#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>

//WIFI
const char* ssid                     = SECRET_SSID;
const char* password                 = SECRET_WIFI_PASSWORD;
const char* hostname                 = "D1UPONOR";

//MQTT
#include <PubSubClient.h>
const char* mqttUser                 = "uponor";
const char* mqttPassword             = SECRET_MQTT_PASSWORD;
const char* mqttServer               = MQTT_SERVER;
const int mqttPort                   = 1883;
const char* ha_topic                 = "uponor/";
const char* ha_state_topic           = "~/state";
const char* ha_avty_topic            = "uponor/availability";
int skippedStreams                   = 4;

#include <ArduinoJson.h>
StaticJsonDocument<256> doc;
char buffer[256];

//RemoteDBG
#include <RemoteDebug.h>
#include <RemoteDebugger.h>

//RS485
#define QUERY_MSG_LEN                  8
#define RESPONSE_TEMP_MSG_LEN         16
#define RESPONSE_ACK_MSG_LEN          30
#include <FastCRC.h>

struct Thermostat {
   int     id;
   byte    code;
   float   setpoint;
   float   temperature;
   int     count;
   struct  Thermostat *next;
};

struct Thermostat *thermoList = NULL;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 180000); //UTC+1, resync each 3 minutes
FastCRC16 CRC16;
RemoteDebug Debug;

byte msg[RESPONSE_ACK_MSG_LEN];
int msg_index = 0;

void wifiSetup() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(hostname);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
  }
}

void mqttSetup() {
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setKeepAlive (60);
  mqttConnect();
}

void mqttConnect() {
  mqttClient.connect(hostname, mqttUser, mqttPassword, ha_avty_topic, 1, true, "offline");
  mqttClient.publish(ha_avty_topic, "online", true);
  mqttClient.subscribe("uponor/power/set");
}

void OTASetup() {
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.onEnd([]() {
    ESP.restart();
  });
  ArduinoOTA.begin();
}

void ntpSetup() {
  timeClient.begin();
  timeClient.update();
}

void info() {
  debugI("Uponor mqtt bridge v%s", VERSION);
}

void remoteDebugSetup() {
  Debug.begin(hostname);
  Debug.setResetCmdEnabled(true);
  //Debug.showProfiler(true);
  Debug.showColors(true);
  Debug.initDebugger(debugGetDebuggerEnabled, debugHandleDebugger, debugGetHelpDebugger, debugProcessCmdDebugger);
  debugInitDebugger(&Debug);
  debugAddFunctionVoid("info", &info);
}

void thermoSetup(){

  struct Thermostat *next, *thermo = NULL;
  for (int i=(sizeof(thermoCodes)/sizeof(thermoCodes[0]))-1; i >= 0; i--) {
    thermo = (struct Thermostat *)malloc(sizeof(Thermostat));
    thermo->code = (byte)thermoCodes[i];
    thermo->id = i;
    thermo->count = 0;
    thermo->setpoint = 0.0;
    thermo->temperature = 0.0;
    thermo->next = next;
    next = thermo;
  }
  thermoList = thermo;
}

void haDiscovery(int thermostat) {
  DynamicJsonDocument haConfig(1024);

  haConfig["name"]                 = "uponor" + String(thermostat);
  String id                        = "ESP" + String (ESP.getChipId(), HEX);
  id                               = id + ":";
  id                               = id +  String(thermostat);
  haConfig["~"]                    = String(ha_topic) + String(thermostat);
  haConfig["unique_id"]            = id;
  haConfig["avty_t"]               = ha_avty_topic;
  haConfig["modes"][0]             = "off";
  haConfig["modes"][1]             = "heat";
  haConfig["mode_stat_t"]          = ha_state_topic;
  haConfig["mode_stat_tpl"]        = F(" {{ value_json.mode if (value_json.mode is defined) else 'off' }}");
  haConfig["temp_stat_t"]          = ha_state_topic;

  String temp_stat_tpl_str         = F(" {{value_json.setpointTemperature if (value_json is defined and value_json.setpointTemperature is defined) else 20 }}");
  haConfig["temp_stat_tpl"]        = temp_stat_tpl_str;

  haConfig["curr_temp_t"]          = ha_state_topic;
  haConfig["curr_temp_tpl"]        = F(" {{ value_json.temperature if (value_json is defined and value_json.temperature is defined) else 21 }}");
  haConfig["min_temp"]             = "18";
  haConfig["max_temp"]             = "24";
  haConfig["temp_step"]            = "0.5";

  JsonObject haConfigDevice        = haConfig.createNestedObject("device");

  haConfigDevice["ids"]            = "T146";
  haConfigDevice["name"]           = "uponor";
  haConfigDevice["sw"]             = VERSION;
  haConfigDevice["mdl"]            = "heating floor";
  haConfigDevice["mf"]             = "uponor";

  String mqttOutput;
  serializeJson(haConfig, mqttOutput);
  String config_topic = "homeassistant/climate/uponor/" + String (thermostat);
  config_topic = config_topic + String ("/config");

  mqttClient.beginPublish(config_topic.c_str(), mqttOutput.length(), true);
  mqttClient.print(mqttOutput);
  mqttClient.endPublish();
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  wifiSetup();
  OTASetup();
  remoteDebugSetup();
  ntpSetup();
  mqttSetup();
  thermoSetup();
  for (int i = 0; i < sizeof(thermoCodes)/sizeof(thermoCodes[0]); i++) {
    haDiscovery(i);
  }
  Serial.begin(19200);
}

void mqttPublish(struct Thermostat *thermo) {

  if (thermo->temperature < thermo->setpoint) {
    doc["mode"] = "heat";
  } else {
    doc["mode"] = "off";
  }
  doc["temperature"] = thermo->temperature;
  doc["setpointTemperature"] = thermo->setpoint;
  doc["power"] = "on";

  memset(&buffer[0], 0, sizeof(buffer));
  serializeJson(doc, buffer);

  String topic = "uponor/";
  topic = topic + String (thermo->id);
  topic = topic + String ("/state");

  if ((thermo->temperature > 0.0) && (thermo->setpoint > 0.0)) {
    if (thermo->count == 1) {
      mqttClient.beginPublish(topic.c_str(), strlen(buffer), false);
      mqttClient.print(buffer);
      if (!mqttClient.endPublish()) {
        debugE("Failure publishing");
      }
    } else if (thermo->count > skippedStreams) {
      thermo->count = 0;
    }
    thermo->count ++;
  }
}

float fahrenheit2celsius(byte upper, byte lower, bool r) {
    char htemp[5];
    memset(&htemp[0], 0, sizeof(htemp));
    char tmp[3];
    memset(&tmp[0], 0, sizeof(tmp));
    sprintf(tmp, "%02X", upper);
    strncat ((char *)&htemp, (char *)&tmp, 2);
    sprintf(tmp, "%02X", lower);
    strncat ((char *)&htemp, (char *)&tmp, 2);
    //temp from hex to dec
    int itemp = (int) strtol (htemp, 0, 16);
    //temp from Fahreinheit to celsius
    float ftemp = itemp / 10.0;
    float ctemp = (ftemp - 32) * 0.5556;
    if (r) {
      return 0.5*round(2.0 * ctemp);
    } else {
      return (int)(ctemp * 10) / 10.0;
    }
}

void printStream() {
  char out[90];
  memset(&out[0], 0, sizeof(out));
  char hex[3];
  memset(&hex[0], 0, sizeof(hex));
  for (int i = 0; i < msg_index - 1; i++) {
    sprintf(hex, "%02X:", msg[i]);
    strncat ((char *)&out, (char *)&hex, 3);
  }
  out[strlen(out) - 1] = '\0';

  struct Thermostat *thermo = thermoList;
  while (thermo->code != msg[3]) {
    thermo = thermo->next;
  }

  if (msg_index == RESPONSE_ACK_MSG_LEN + 1) {
    float ctemp = fahrenheit2celsius(msg[msg_index - 11], msg[msg_index - 10], true);
    debugD("%s - (setpoint temp: %2.1f °C)", out , ctemp);
    thermo->setpoint = ctemp;
  } else if (msg_index == RESPONSE_TEMP_MSG_LEN) {
    float ctemp = fahrenheit2celsius(msg[5], msg[6], false);
    debugD("%s                                              - (temp: %2.1f °C)", out , ctemp);
    thermo->temperature = ctemp;
    mqttPublish(thermo);
  } else {
    debugV("%s", out);
  }
}

void append(int elem) {
  byte hex = byte(elem);
  msg[msg_index] = hex;
  msg_index ++;

  if ((msg_index >= QUERY_MSG_LEN) && (CRC16.modbus(msg, msg_index - 1) == 0)) {
      //Got a valid stream
      printStream();
      memset(&msg[0], 0, sizeof(msg));
      msg_index = 0;
  }
}

void blink(int ms) {
    digitalWrite(LED_BUILTIN, LOW);
    delay (ms);
    digitalWrite(LED_BUILTIN, HIGH);
}

void rs485loop() {
  if (Serial.available() > 0) {
    int data = Serial.read();

    if (((msg_index == 0) && (data != 0x11)) ||
         ((msg_index == 1) && (data != 0x05)) ||
         ((msg_index == 2) && (data != 0x95)) ||
         (msg_index > RESPONSE_ACK_MSG_LEN)) {
      // To drop stream
      memset(&msg[0], 0, sizeof(msg));
      msg_index = 0;
    } else {
      append(data);
    }
  } else {
   debugV("Waiting for data on 485 interface");
   blink(500);
  }
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  ArduinoOTA.handle();
  Debug.handle();
  rs485loop();
  if (!mqttClient.connected()) {
    mqttConnect();
  }
  mqttClient.loop();
}
