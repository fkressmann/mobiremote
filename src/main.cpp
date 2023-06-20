#include <Arduino.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <credentials.h>

#define MCF_POWER D6
#define MCF_SET D7
#define MCF_UP D2
#define MCF_DOWN D3
#define BUTTON D5
#define ONE_WIRE_BUS D1
#define LED D4

#define MQTT_SERVER_LABEL "mq_server"
#define MQTT_USER_LABEL "mq_user"
#define MQTT_PASSWORD_LABEL "mq_pw"
#define MQTT_PREFIX_LABEL "mq_pref"

volatile boolean saveConfig = false;
volatile boolean buttonPressed = false;

const char *deviceName = "esp-mobiremote";

const char *topicPower = "power";
const char *topicTargetTemp = "target";
const char *topicIsTemp = "temp";
const char *topicInitTemp = "inittemp";
const char *topicInitPower = "initpower";
const char *topicStatus = "status";

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

WiFiManager wifiManager;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

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
  pressButton(MCF_SET);
  if (delta < 0) {
    for (int i = 0; i > delta; i--) {
      pressButton(MCF_DOWN);
    }
  } else {
    for (int i = 0; i < delta; i++) {
      pressButton(MCF_UP);
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
  if (newSetTemp == config.tempSet || newSetTemp < -10 || newSetTemp > 10) {
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
    pressButton(MCF_POWER, true);
    config.powerState = newState;
    writeConfigToEeprom();
  }
  sendCurrentPowerState();
}

void handleNtc() {
  DS18B20.requestTemperatures();
  float temp = DS18B20.getTempCByIndex(0);
  if (abs(temp - prevTemp) >= 0.1) {
    prevTemp = temp;
    sendCurrentTemperature();
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
    sendCurrentTargetTemp();
  } else if (strcmp(slashPointer + 1, topicInitPower) == 0) {
    config.powerState = plInt;
    writeConfigToEeprom();
    sendCurrentPowerState();
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

void startConfigPortal() {
  WiFi.disconnect();
  ESP.restart();
}

void IRAM_ATTR buttonIsr() {
  buttonPressed = true;
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  attachInterrupt(digitalPinToInterrupt(BUTTON), buttonIsr, FALLING);

  // Read settings from EEPROM
  EEPROM.begin(sizeof(config));
  EEPROM.get(0, config);
  EEPROM.end();
  log("Config " + String(config.tempSet) + ":" + String(config.powerState) + " loaded from flash.");
  Serial.printf("MQTT server: %s \n", config.mqttServer);
  Serial.printf("MQTT user: %s \n", config.mqttUser);
  Serial.printf("MQTT password: %s \n", config.mqttPassword);
  Serial.printf("MQTT prefix: %s \n", config.mqttPrefix);

  WiFi.hostname(deviceName);
  WiFi.persistent(true);

  WiFiManagerParameter p_mqttServer(MQTT_SERVER_LABEL, "MQTT Server", config.mqttServer, 30);
  wifiManager.addParameter(&p_mqttServer);
  WiFiManagerParameter p_mqttUser(MQTT_USER_LABEL, "MQTT User", config.mqttUser, 30);
  wifiManager.addParameter(&p_mqttUser);
  WiFiManagerParameter p_mqttPassword(MQTT_PASSWORD_LABEL, "MQTT Password", config.mqttPassword, 30);
  wifiManager.addParameter(&p_mqttPassword);
  WiFiManagerParameter p_mqttPrefix(MQTT_PREFIX_LABEL, "MQTT Prefix", config.mqttPrefix, 30);
  wifiManager.addParameter(&p_mqttPrefix);

  wifiManager.setConfigPortalTimeout(60);
  
  wifiManager.setSaveConfigCallback([]() {
    Serial.println("Should save config");
    saveConfig = true;
  });

  wifiManager.autoConnect(STA_SSID, STA_PSK);
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

  pinMode(MCF_POWER, OUTPUT);
  pinMode(MCF_SET, OUTPUT);
  pinMode(MCF_UP, OUTPUT);
  pinMode(MCF_DOWN, OUTPUT);
  pinMode(BUTTON, INPUT_PULLUP);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  DS18B20.begin();

  commandPrefix = config.mqttPrefix + String("cmnd");
  mqttClient.setServer(config.mqttServer, 1883);
  mqttClient.setCallback(callback);
  reconnectMqtt();

  // Init values in MQTT
  log("Startup complete");
  sendCurrentTargetTemp();
  sendCurrentPowerState();

  Serial.println("Setup finished, looping now");
}

void loop() {
  if (buttonPressed) startConfigPortal();
  ArduinoOTA.handle();
  reconnectMqtt();
  mqttClient.loop();
  if (millis() % 1000 == 0) {
    handleNtc();
  }
}