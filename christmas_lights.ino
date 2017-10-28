#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <FastLED.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

const char* mqtt_server = "broker.mqtt-dashboard.com";

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;

void setup() {
    client.setServer(mqtt_server, 1883);
}

void loop() {
	
}
