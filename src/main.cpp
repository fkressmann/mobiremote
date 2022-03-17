#include <Arduino.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <PubSubClient.h>
#include <credentials.h>

const String commandPrefix = MQTT_PREFIX + String("cmnd");
const char *deviceName = "esp-mobiremote";
int tempSet;

ESP8266WiFiMulti wifiMulti;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void writeSettingsToEeprom() {
  EEPROM.begin(sizeof(tempSet));
  EEPROM.put(0, tempSet);
  EEPROM.commit();
  EEPROM.end();
}

void sendData(String subtopic, String data, bool retained) {
  Serial.println("SENDING: " + subtopic + ":" + data);
  subtopic = MQTT_PREFIX + subtopic;
  mqttClient.publish(subtopic.c_str(), data.c_str(), retained);
}

void log(String line) {
  Serial.println(line);
  sendData("log", line, true);
}

void reconnectMqtt() {
  if (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // if you MQTT broker has clientID,username and password
    // please change following line to    if (client.connect(clientId,userName,passWord))
    if (mqttClient.connect(deviceName, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("connected");
      mqttClient.subscribe(commandPrefix.c_str());
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

void callback(char *topic, byte *payload, unsigned int length) {
  char pl[length];
  for (unsigned int i = 0; i < length; i++){
    pl[i] = payload[i];
  }
  tempSet = atoi(pl);
  writeSettingsToEeprom();
  log(String(tempSet) + " written to flash.");
  // Handle MQTT
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

  // Read settings from EEPROM
  EEPROM.begin(sizeof(tempSet));
  EEPROM.get(0, tempSet);
  EEPROM.end();
  Serial.println("TempSet from EEPROM: " + String(tempSet));
  //

  mqttClient.setServer(MQTT_SERVER, 1883);
  mqttClient.setCallback(callback);

  ArduinoOTA.setHostname(deviceName);
  ArduinoOTA.begin();
  Serial.println("Setup finished, looping now");
}

void loop() {
  ArduinoOTA.handle();
  reconnectMqtt();
  mqttClient.loop();
}