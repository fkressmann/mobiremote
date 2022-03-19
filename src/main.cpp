#include <Arduino.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <PubSubClient.h>
#include <credentials.h>

#define BUTTON_POWER D6
#define BUTTON_SET D7
#define BUTTON_UP D2
#define BUTTON_DOWN D3
#define NTC A0
#define LED D4

const String commandPrefix = MQTT_PREFIX + String("cmnd");
const char *deviceName = "esp-mobiremote";
unsigned long ntcRead = 0;
unsigned int ntcCount = 0;
struct {
  int tempSet;
  boolean powerState;
} config;

ESP8266WiFiMulti wifiMulti;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void sendData(String subtopic, String data, bool retained) {
  Serial.println("SENDING: " + subtopic + ":" + data);
  subtopic = MQTT_PREFIX + subtopic;
  mqttClient.publish(subtopic.c_str(), data.c_str(), retained);
}

void log(String line) {
  sendData("log", line, true);
}

void writeSettingsToEeprom() {
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
    if (mqttClient.connect(deviceName, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("connected");
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
  writeSettingsToEeprom();
}

void handleNewPowerState(boolean newState) {
  if (config.powerState == newState) {
    log("Power is already " + String(newState));
  } else {
    pressButton(BUTTON_POWER, true);
    config.powerState = newState;
    writeSettingsToEeprom();
  }
}

float adcToTemperature(float adcReading) {
  // 3.3v / 1024 (10bit ADC)
  float voltage = 0.00322265625 * adcReading;
  float resistance = -((voltage * 10000) / (voltage - 3.3));
  // Steinhart–Hart equation
  float temp = (1 / ((log(resistance / 10000) / 3977) + (1 / (25 + 273.15)))) - 273.15;
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
    sendData("temp", String(temp), false);
    ntcRead = 0;
    ntcCount = 0;
  }
}

void sendInvalidCommandMessage(char *slashPointer, char *pl) {
  log("Invalid command: " + String(slashPointer) + "=" + String(pl));
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

  if (strcmp(slashPointer + 1, "set") == 0) {
    handleNewSetTemp(plInt);
  } else if (strcmp(slashPointer + 1, "inittemp") == 0) {
    config.tempSet = plInt;
    writeSettingsToEeprom();
  } else if (strcmp(slashPointer + 1, "initpwr") == 0) {
    config.powerState = plInt;
    writeSettingsToEeprom();
  } else if (strcmp(slashPointer + 1, "pwr") == 0) {
    handleNewPowerState(plInt);
  } else {
    sendInvalidCommandMessage(slashPointer, pl);
  }
  digitalWrite(LED, HIGH);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  WiFi.mode(WIFI_STA);
  WiFi.hostname(deviceName);
  wifiMulti.addAP(WIFI_SSID_1, WIFI_PSK_1);
  wifiMulti.addAP(WIFI_SSID_2, WIFI_PSK_2);
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(1000);
    Serial.println("WiFi running...");
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

  mqttClient.setServer(MQTT_SERVER, 1883);
  mqttClient.setCallback(callback);
  reconnectMqtt();

  // Read settings from EEPROM
  EEPROM.begin(sizeof(config));
  EEPROM.get(0, config);
  EEPROM.end();
  log("Config " + String(config.tempSet) + ":" + String(config.powerState) + " loaded from flash.");
  //

  Serial.println("Setup finished, looping now");
}

void loop() {
  ArduinoOTA.handle();
  reconnectMqtt();
  mqttClient.loop();
  if(millis() % 10 == 0) {
    handleNtc();
  }
}