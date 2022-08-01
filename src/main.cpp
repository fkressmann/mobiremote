#include <Arduino.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP_WiFiManager.h>
#include <PubSubClient.h>
#include <credentials.h>

#define BUTTON_POWER D6
#define BUTTON_SET D7
#define BUTTON_UP D2
#define BUTTON_DOWN D3
#define NTC A0
#define LED D4

#define MQTT_SERVER_LABEL "mq_server"
#define MQTT_USER_LABEL "mq_user"
#define MQTT_PASSWORD_LABEL "mq_pw"
#define MQTT_PREFIX_LABEL "mq_pref"
boolean saveConfig = false;

const char *deviceName = "esp-mobiremote";

const char *topicPower = "power";
const char *topicTargetTemp = "target";
const char *topicIsTemp = "temp";
const char *topicInitTemp = "inittemp";
const char *topicInitPower = "initpower";
const char *topicStatus = "status";

unsigned long ntcRead = 0;
unsigned int ntcCount = 0;
float prevTemp = 0;
struct {
  int tempSet;
  boolean powerState;
  char mqttServer[30];
  char mqttUser[30];
  char mqttPassword[30];
  char mqttPrefix[30];
} config;
String commandPrefix;

// linear approximation coefficents to this chips ADC charactersitics |(optimized for +10° - 0°C)
// Allows accuracy up to 0.1°C
const float m = -32.807619953525176;
const float b = -81.15865319571508;

ESP_WiFiManager ESP_wifiManager;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void sendData(String subtopic, String data, bool retained) {
  Serial.println("SENDING: " + subtopic + ":" + data);
  subtopic = config.mqttPrefix + subtopic;
  mqttClient.publish(subtopic.c_str(), data.c_str(), retained);
}

void log(String line) {
  sendData("log", line, true);
}

void sendCurrentTargetTemp() {
    sendData(topicTargetTemp, String(config.tempSet), false);
}

void sendCurrentPowerState() {
    sendData(topicPower, String(config.powerState), false);
}

void sendCurrentTemperature() {
    sendData(topicIsTemp, String(prevTemp), false);
}

void writeConfigToEeprom() {
  EEPROM.begin(sizeof(config));
  EEPROM.put(0, config);
  EEPROM.commit();
  EEPROM.end();
  log("Config " + String(config.tempSet) + ":" + String(config.powerState) + " written to flash.");
}

void reconnectMqtt() {
  if (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // if you MQTT broker has clientID,username and password
    // please change following line to    if (client.connect(clientId,userName,passWord))
    if (mqttClient.connect(deviceName, config.mqttUser, config.mqttPassword)) {
      Serial.println("connected");
      Serial.println("Subscribing to: " + commandPrefix);
      mqttClient.subscribe((commandPrefix + "/#").c_str());
      sendData("ip", WiFi.localIP().toString(), true);
      sendData("rssi", String(WiFi.RSSI()), true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 1 second");
      delay(1000);
    }
  }
}

void pressButton(int button, boolean longPress) {
  log("Pressing Button " + String(button));
  digitalWrite(button, HIGH);
  delay(200);
  if (longPress) delay(3000);
  digitalWrite(button, LOW);
  delay(100);
}

void pressButton(int button) {
  pressButton(button, false);
}

void changeSetTemperature(int delta) {
  if (delta == 0) {
    return;
  }
  pressButton(BUTTON_SET);
  if (delta < 0) {
    for (int i = 0; i > delta; i--) {
      pressButton(BUTTON_DOWN);
    }
  } else {
    for (int i = 0; i < delta; i++) {
      pressButton(BUTTON_UP);
    }
  }
  // wait for mobicool to exit temperature menu before doing anything else
  delay(11000);
}

void handleNewSetTemp(int newSetTemp) {
  if (!config.powerState) {
    log("Cannot set temp, mobicool is off");
    return;
  }
  if (newSetTemp == config.tempSet || newSetTemp < -10 || newSetTemp > 20) {
    log("Bad newTemp: " + String(newSetTemp) + " (oldTemp: " + String(config.tempSet) + ")");
    return;
  }
  int delta = newSetTemp - config.tempSet;
  changeSetTemperature(delta);
  config.tempSet = newSetTemp;
  writeConfigToEeprom();
  sendCurrentTargetTemp();
}

void handleNewPowerState(boolean newState) {
  if (config.powerState == newState) {
    log("Power is already " + String(newState));
  } else {
    pressButton(BUTTON_POWER, true);
    config.powerState = newState;
    writeConfigToEeprom();
  }
  sendCurrentPowerState();
}

float adcToTemperature(float adcReading) {
  // 3.3v / 1024 (10bit ADC)
  float voltage = (0.00322265625 * adcReading);
  // sendData("voltage", String(voltage, 4), false); //for calibration reasons
  float temp = (m * voltage) - b;
  return temp;
}

void handleNtc() {
  if (ntcCount < 1000) {
    yield();
    ntcRead += analogRead(NTC);
    ntcCount += 1;
  } else {
    float analogReading = (float)ntcRead / ntcCount;
    float temp = adcToTemperature(analogReading);
    if (abs(temp - prevTemp) >= 0.1) {
      prevTemp = temp;
      sendCurrentTemperature();
    }
    ntcRead = 0;
    ntcCount = 0;
  }
}

void sendInvalidCommandMessage(char *slashPointer, char *pl) {
  log("Invalid command: " + String(slashPointer) + ":" + String(pl));
}

void callback(char *topic, byte *payload, unsigned int length) {
  digitalWrite(LED, LOW);

  // Parse payload to c string
  char pl[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    pl[i] = payload[i];
  }
  pl[length] = '\0';
  int plInt = atoi(pl);

  char *slashPointer = strrchr(topic, '/');
  if (length == 0) {
    sendInvalidCommandMessage(slashPointer, pl);
  }

  if (strcmp(slashPointer + 1, topicTargetTemp) == 0) {
    handleNewSetTemp(plInt);
  } else if (strcmp(slashPointer + 1, topicInitTemp) == 0) {
    config.tempSet = plInt;
    writeConfigToEeprom();
  } else if (strcmp(slashPointer + 1, topicInitPower) == 0) {
    config.powerState = plInt;
    writeConfigToEeprom();
  } else if (strcmp(slashPointer + 1, topicPower) == 0) {
    handleNewPowerState(plInt);
  } else if (strcmp(slashPointer + 1, topicStatus) == 0) {
    sendCurrentTemperature();
    sendCurrentTargetTemp();
    sendCurrentPowerState();
  } else {
    sendInvalidCommandMessage(slashPointer, pl);
  }
  digitalWrite(LED, HIGH);
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  // Read settings from EEPROM
  EEPROM.begin(sizeof(config));
  EEPROM.get(0, config);
  EEPROM.end();
  log("Config " + String(config.tempSet) + ":" + String(config.powerState) + " loaded from flash.");
  //

  WiFi.hostname(deviceName);

  ESP_WMParameter p_mqttServer(MQTT_SERVER_LABEL, "MQTT Server", config.mqttServer, 30);
  ESP_wifiManager.addParameter(&p_mqttServer);
  ESP_WMParameter p_mqttUser(MQTT_USER_LABEL, "MQTT User", config.mqttUser, 30);
  ESP_wifiManager.addParameter(&p_mqttUser);
  ESP_WMParameter p_mqttPassword(MQTT_PASSWORD_LABEL, "MQTT Password", config.mqttPassword, 30);
  ESP_wifiManager.addParameter(&p_mqttPassword);
  ESP_WMParameter p_mqttPrefix(MQTT_PREFIX_LABEL, "MQTT Prefix", config.mqttPrefix, 30);
  ESP_wifiManager.addParameter(&p_mqttPrefix);

  ESP_wifiManager.setConfigPortalTimeout(60);
  
  ESP_wifiManager.setSaveConfigCallback([]() {
    Serial.println("Should save config");
    saveConfig = true;
  });

  ESP_wifiManager.autoConnect(STA_SSID, STA_PSK);
  if (saveConfig) {
    strcpy(config.mqttServer, p_mqttServer.getValue());
    strcpy(config.mqttUser, p_mqttUser.getValue());
    strcpy(config.mqttPassword, p_mqttPassword.getValue());
    strcpy(config.mqttPrefix, p_mqttPrefix.getValue());
    writeConfigToEeprom();
    saveConfig = false;
  }

  ArduinoOTA.onStart([]() {
    log("Start OTA update");
  });
  ArduinoOTA.setHostname(deviceName);
  ArduinoOTA.begin();

  pinMode(BUTTON_POWER, OUTPUT);
  pinMode(BUTTON_SET, OUTPUT);
  pinMode(BUTTON_UP, OUTPUT);
  pinMode(BUTTON_DOWN, OUTPUT);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  commandPrefix = config.mqttPrefix + String("cmnd");
  mqttClient.setServer(config.mqttServer, 1883);
  mqttClient.setCallback(callback);
  reconnectMqtt();

  Serial.println("Setup finished, looping now");
}

void loop() {
  ArduinoOTA.handle();
  reconnectMqtt();
  mqttClient.loop();
  if (millis() % 10 == 0) {
    handleNtc();
  }
}