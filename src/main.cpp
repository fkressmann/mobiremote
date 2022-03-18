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
#define LED D4

const String commandPrefix = MQTT_PREFIX + String("cmnd");
const char *deviceName = "esp-mobiremote";
int tempSet;

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
  EEPROM.begin(sizeof(tempSet));
  EEPROM.put(0, tempSet);
  EEPROM.commit();
  EEPROM.end();
  log(String(tempSet) + " written to flash.");
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
  if (newSetTemp == tempSet || newSetTemp < -10 || newSetTemp > 20) {
    log("Bad newTemp: " + String(newSetTemp) + " (oldTemp: " + String(tempSet) + ")");
    return;
  }
  int delta = newSetTemp - tempSet;
  changeSetTemperature(delta);
  tempSet = newSetTemp;
  writeSettingsToEeprom();
}

void callback(char *topic, byte *payload, unsigned int length) {
  digitalWrite(LED, LOW);
  char pl[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    pl[i] = payload[i];
  }
  pl[length] = '\0';

  char *slashPointer = strrchr(topic, '/');

  if (strcmp(slashPointer + 1, "set") == 0) {
    handleNewSetTemp(atoi(pl));
  } else if (strcmp(slashPointer + 1, "init") == 0) {
    tempSet = atoi(pl);
    writeSettingsToEeprom();
  } else if (strcmp(slashPointer + 1, "pwr") == 0) {
    pressButton(BUTTON_POWER, true);
  } else {
    log("Invalid command: " + String(slashPointer) + "=" + String(pl));
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

  // Read settings from EEPROM
  EEPROM.begin(sizeof(tempSet));
  EEPROM.get(0, tempSet);
  EEPROM.end();
  Serial.println("TempSet from EEPROM: " + String(tempSet));
  //

  pinMode(BUTTON_POWER, OUTPUT);
  pinMode(BUTTON_SET, OUTPUT);
  pinMode(BUTTON_UP, OUTPUT);
  pinMode(BUTTON_DOWN, OUTPUT);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  mqttClient.setServer(MQTT_SERVER, 1883);
  mqttClient.setCallback(callback);

  Serial.println("Setup finished, looping now");
}

void loop() {
  ArduinoOTA.handle();
  reconnectMqtt();
  mqttClient.loop();
}