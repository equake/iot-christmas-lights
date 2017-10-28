#include <Arduino.h>
#include <FS.h>

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <FastLED.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <WiFiManager.h>

char mqtt_server[40];
char mqtt_port[6] = "1883";
bool shouldSaveConfig = false;

Ticker ticker;

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;


void setup() {
    Serial.begin(115200);
    Serial.println("Merry Christmas!");

    pinMode(BUILTIN_LED, OUTPUT);   //set led pin as output
    ticker.attach(0.6, toggleLed);

    configFileRead();

    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
    
    WiFiManager wifiManager;
    // wifiManager.resetSettings();     //reset settings - for testing
    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setSaveConfigCallback(configFileWriteCallback);
    wifiManager.setConfigPortalTimeout(120);    
    if (!wifiManager.autoConnect("HO-HO-HO")) {
        Serial.println("Failed to connect and hit timeout");
        ESP.reset();    //reset and try again,
        delay(1000);
    }

    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    configFileWrite();

    Serial.println("Connected... yeey :)");

    ticker.detach();    
    digitalWrite(BUILTIN_LED, LOW);     //keep LED on

    Serial.println(mqtt_server);
    Serial.println(mqtt_port);
    
    // client.setServer(mqtt_server, atoi(mqtt_port));
}

void loop() {
	
}

/** 
 * Toggle built-in led state
 **/
void toggleLed() {
    digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));  // set pin to the opposite state
}

void configFileRead() {
    Serial.println("Mounting FS...");

    if (SPIFFS.begin()) {
        Serial.println("Mounted file system");
        if (SPIFFS.exists("/config.json")) {
            //file exists, reading and loading
            Serial.println("Reading config file");
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile) {
                Serial.println("Opened config file");
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);
                DynamicJsonBuffer jsonBuffer;
                JsonObject& json = jsonBuffer.parseObject(buf.get());
                json.printTo(Serial);
                Serial.print("\n");
                if (json.success()) {
                    Serial.println("Parsed json");

                    strcpy(mqtt_server, json["mqtt_server"]);
                    strcpy(mqtt_port, json["mqtt_port"]);

                } else {
                    Serial.println("Failed to load json config");
                }
            }
        }
    } else {
        Serial.println("Failed to mount FS");
    }
}

void configFileWrite() {
    if (shouldSaveConfig) {
        Serial.println("Saving config");
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.createObject();
        json["mqtt_server"] = mqtt_server;
        json["mqtt_port"] = mqtt_port;
    
        File configFile = SPIFFS.open("/config.json", "w");
        if (!configFile) {
            Serial.println("Failed to open config file for writing");
        }
    
        json.printTo(Serial);
        json.printTo(configFile);
        Serial.print("\n");
        configFile.close();
    }
}

void configFileWriteCallback() {
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

/**
 * Callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
 **/
void configModeCallback(WiFiManager *myWiFiManager) {
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    //if you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());
    //entered config mode, make led toggle faster
    ticker.attach(0.2, toggleLed);
}

