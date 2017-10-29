
#include <Arduino.h>
#include <FS.h>

#include <FastLED.h>
FASTLED_USING_NAMESPACE

#include <ArduinoJson.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
// #include <PubSubClient.h>
#include <Ticker.h>
#include <WiFiManager.h>

#include "GradientPalettes.h"

/* -------- FAST LED START -------- */
#define DATA_PIN D2  // for Huzzah: Pins w/o special function:  #4, #5, #12, #13, #14; // #16 does not work :(
#define LED_TYPE WS2811
#define COLOR_ORDER RGB
#define NUM_LEDS 50

#define MILLI_AMPS 1000        // IMPORTANT: set here the max milli-Amps of your power supply 5V 2A = 2000
#define FRAMES_PER_SECOND 200  // here you can control the speed. With the Access Point / Web Server the animations run a bit slower.

CRGB leds[NUM_LEDS];

uint8_t patternIndex = 0;

const uint8_t brightnessCount = 5;
uint8_t brightnessMap[brightnessCount] = {16, 32, 64, 128, 255};
int brightnessIndex = 0;
uint8_t brightness = brightnessMap[brightnessIndex];

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

// ten seconds per color palette makes a good demo 20-120 is better for deployment
#define SECONDS_PER_PALETTE 10

///////////////////////////////////////////////////////////////////////

// Forward declarations of an array of cpt-city gradient palettes, and
// a count of how many there are.  The actual color palette definitions
// are at the bottom of this file.
extern const TProgmemRGBGradientPalettePtr gGradientPalettes[];
extern const uint8_t gGradientPaletteCount;

// Current palette number from the 'playlist' of color palettes
uint8_t gCurrentPaletteNumber = 0;

CRGBPalette16 gCurrentPalette(CRGB::Black);
CRGBPalette16 gTargetPalette(gGradientPalettes[0]);

uint8_t currentPatternIndex = 0;  // Index number of which pattern is current
bool autoplayEnabled = false;

uint8_t autoPlayDurationSeconds = 10;
unsigned int autoPlayTimeout = 0;

uint8_t currentPaletteIndex = 0;

uint8_t gHue = 0;  // rotating "base color" used by many of the patterns

CRGB solidColor = CRGB::Blue;

uint8_t power = 1;
/* -------- FAST LED END -------- */

char mqtt_server[40];
char mqtt_port[6] = "1883";
bool shouldSaveConfig = false;

Ticker ticker;

WiFiClient espClient;
WiFiManager wifiManager;

// PubSubClient mqttClient(espClient);
// long lastMsg = 0;
// char msg[50];
// int value = 0;

ESP8266WebServer server(80);

/**
 * Toggle built-in led state
 **/
void toggleLed() {
    digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));  // set pin to the opposite state
}

void ICACHE_FLASH_ATTR configFileRead() {
    Serial.println(F("Mounting FS..."));

    if (SPIFFS.begin()) {
        Serial.println(F("Mounted file system"));
        if (SPIFFS.exists("/config.json")) {
            // file exists, reading and loading
            Serial.println(F("Reading config file"));
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile) {
                Serial.println(F("Opened config file"));
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);
                DynamicJsonBuffer jsonBuffer;
                JsonObject &json = jsonBuffer.parseObject(buf.get());
                json.printTo(Serial);
                Serial.print("\n");
                if (json.success()) {
                    Serial.println(F("Parsed json"));

                    strcpy(mqtt_server, json[F("mqtt_server")]);
                    strcpy(mqtt_port, json[F("mqtt_port")]);
                } else {
                    Serial.println(F("Failed to load json config"));
                }
            }
        }
    } else {
        Serial.println(F("Failed to mount FS"));
    }
}

void ICACHE_FLASH_ATTR configFileWrite() {
    if (shouldSaveConfig) {
        Serial.println(F("Saving config"));
        DynamicJsonBuffer jsonBuffer;
        JsonObject &json = jsonBuffer.createObject();
        json[F("mqtt_server")] = mqtt_server;
        json[F("mqtt_port")] = mqtt_port;

        File configFile = SPIFFS.open("/config.json", "w");
        if (!configFile) {
            Serial.println(F("Failed to open config file for writing"));
        }

        json.printTo(Serial);
        json.printTo(configFile);
        Serial.print("\n");
        configFile.close();
    }
}

void configFileWriteCallback() {
    Serial.println(F("Should save config"));
    shouldSaveConfig = true;
}

/**
 * Callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
 **/
void configModeCallback(WiFiManager *myWiFiManager) {
    Serial.println(F("Entered config mode"));
    Serial.println(WiFi.softAPIP());
    // if you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());
    // entered config mode, make led toggle faster
    ticker.attach(0.2, toggleLed);
}



/** Palettes **/
typedef struct {
    CRGBPalette16 palette;
    String name;
} PaletteAndName;
typedef PaletteAndName PaletteAndNameList[];

const CRGBPalette16 palettes[] = {RainbowColors_p, RainbowStripeColors_p, CloudColors_p, LavaColors_p, OceanColors_p, ForestColors_p, PartyColors_p, HeatColors_p};

const uint8_t paletteCount = ARRAY_SIZE(palettes);

const String paletteNames[paletteCount] = {
    "Rainbow", "Rainbow Stripe", "Cloud", "Lava", "Ocean", "Forest", "Party", "Heat",
};


/** Patterns **/
void ICACHE_FLASH_ATTR rainbow() {
    // FastLED's built-in rainbow generator
    fill_rainbow(leds, NUM_LEDS, gHue, 10);
}

void ICACHE_FLASH_ATTR addGlitter(fract8 chanceOfGlitter) {
    if (random8() < chanceOfGlitter) {
        leds[random16(NUM_LEDS)] += CRGB::White;
    }
}

void ICACHE_FLASH_ATTR rainbowWithGlitter() {
    // built-in FastLED rainbow, plus some random sparkly glitter
    rainbow();
    addGlitter(80);
}

void ICACHE_FLASH_ATTR confetti() {
    // random colored speckles that blink in and fade smoothly
    fadeToBlackBy(leds, NUM_LEDS, 10);
    int pos = random16(NUM_LEDS);
    //  leds[pos] += CHSV( gHue + random8(64), 200, 255);
    leds[pos] += ColorFromPalette(palettes[currentPaletteIndex], gHue + random8(64));
}

void ICACHE_FLASH_ATTR sinelon() {
    // a colored dot sweeping back and forth, with fading trails
    fadeToBlackBy(leds, NUM_LEDS, 20);
    int pos = beatsin16(13, 0, NUM_LEDS - 1);
    //  leds[pos] += CHSV( gHue, 255, 192);
    leds[pos] += ColorFromPalette(palettes[currentPaletteIndex], gHue, 192);
}

void ICACHE_FLASH_ATTR bpm() {
    // colored stripes pulsing at a defined Beats-Per-Minute (BPM)
    uint8_t BeatsPerMinute = 62;
    CRGBPalette16 palette = palettes[currentPaletteIndex];
    uint8_t beat = beatsin8(BeatsPerMinute, 64, 255);
    for (int i = 0; i < NUM_LEDS; i++) {  // 9948
        leds[i] = ColorFromPalette(palette, gHue + (i * 2), beat - gHue + (i * 10));
    }
}

void ICACHE_FLASH_ATTR juggle() {
    // eight colored dots, weaving in and out of sync with each other
    fadeToBlackBy(leds, NUM_LEDS, 20);
    byte dothue = 0;
    for (int i = 0; i < 8; i++) {
        //    leds[beatsin16(i + 7, 0, NUM_LEDS)] |= CHSV(dothue, 200, 255);
        leds[beatsin16(i + 7, 0, NUM_LEDS)] |= ColorFromPalette(palettes[currentPaletteIndex], dothue);
        dothue += 32;
    }
}

// Pride2015 by Mark Kriegsman:
// https://gist.github.com/kriegsman/964de772d64c502760e5
// This function draws rainbows with an ever-changing,
// widely-varying set of parameters.
void ICACHE_FLASH_ATTR pride() {
    static uint16_t sPseudotime = 0;
    static uint16_t sLastMillis = 0;
    static uint16_t sHue16 = 0;

    uint8_t sat8 = beatsin88(87, 220, 250);
    uint8_t brightdepth = beatsin88(341, 96, 224);
    uint16_t brightnessthetainc16 = beatsin88(203, (25 * 256), (40 * 256));
    uint8_t msmultiplier = beatsin88(147, 23, 60);

    uint16_t hue16 = sHue16;  // gHue * 256;
    uint16_t hueinc16 = beatsin88(113, 1, 3000);

    uint16_t ms = millis();
    uint16_t deltams = ms - sLastMillis;
    sLastMillis = ms;
    sPseudotime += deltams * msmultiplier;
    sHue16 += deltams * beatsin88(400, 5, 9);
    uint16_t brightnesstheta16 = sPseudotime;

    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        hue16 += hueinc16;
        uint8_t hue8 = hue16 / 256;

        brightnesstheta16 += brightnessthetainc16;
        uint16_t b16 = sin16(brightnesstheta16) + 32768;

        uint16_t bri16 = (uint32_t)((uint32_t)b16 * (uint32_t)b16) / 65536;
        uint8_t bri8 = (uint32_t)(((uint32_t)bri16) * brightdepth) / 65536;
        bri8 += (255 - brightdepth);

        CRGB newcolor = CHSV(hue8, sat8, bri8);

        nblend(leds[i], newcolor, 64);
    }
}

// ColorWavesWithPalettes by Mark Kriegsman:
// https://gist.github.com/kriegsman/8281905786e8b2632aeb
// This function draws color waves with an ever-changing,
// widely-varying set of parameters, using a color palette.
void ICACHE_FLASH_ATTR colorwaves() {
    static uint16_t sPseudotime = 0;
    static uint16_t sLastMillis = 0;
    static uint16_t sHue16 = 0;

    // uint8_t sat8 = beatsin88( 87, 220, 250);
    uint8_t brightdepth = beatsin88(341, 96, 224);
    uint16_t brightnessthetainc16 = beatsin88(203, (25 * 256), (40 * 256));
    uint8_t msmultiplier = beatsin88(147, 23, 60);

    uint16_t hue16 = sHue16;  // gHue * 256;
    uint16_t hueinc16 = beatsin88(113, 300, 1500);

    uint16_t ms = millis();
    uint16_t deltams = ms - sLastMillis;
    sLastMillis = ms;
    sPseudotime += deltams * msmultiplier;
    sHue16 += deltams * beatsin88(400, 5, 9);
    uint16_t brightnesstheta16 = sPseudotime;

    for (uint16_t i = 0; i < NUM_LEDS; i++) {
        hue16 += hueinc16;
        uint8_t hue8 = hue16 / 256;
        uint16_t h16_128 = hue16 >> 7;
        if (h16_128 & 0x100) {
            hue8 = 255 - (h16_128 >> 1);
        } else {
            hue8 = h16_128 >> 1;
        }

        brightnesstheta16 += brightnessthetainc16;
        uint16_t b16 = sin16(brightnesstheta16) + 32768;

        uint16_t bri16 = (uint32_t)((uint32_t)b16 * (uint32_t)b16) / 65536;
        uint8_t bri8 = (uint32_t)(((uint32_t)bri16) * brightdepth) / 65536;
        bri8 += (255 - brightdepth);

        uint8_t index = hue8;
        // index = triwave8( index);
        index = scale8(index, 240);

        CRGB newcolor = ColorFromPalette(gCurrentPalette, index, bri8);

        nblend(leds[i], newcolor, 128);
    }
}

// Alternate rendering function just scrolls the current palette
// across the defined LED strip.
void ICACHE_FLASH_ATTR palettetest() {
    static uint8_t startindex = 0;
    startindex--;
    fill_palette(leds, NUM_LEDS, startindex, (256 / NUM_LEDS) + 1, gCurrentPalette, 255, LINEARBLEND);
}

typedef void (*Pattern)();
typedef struct {
    Pattern pattern;
    String name;
} PatternAndName;
typedef PatternAndName PatternAndNameList[];

// List of patterns to cycle through.  Each is defined as a separate function below.
PatternAndNameList patterns = {
    {colorwaves, "Color Waves"}, {palettetest, "Palette Test"}, {pride, "Pride"},   {rainbow, "Rainbow"}, {rainbowWithGlitter, "Rainbow With Glitter"},
    {confetti, "Confetti"},      {sinelon, "Sinelon"},          {juggle, "Juggle"}, {bpm, "BPM"},         {showSolidColor, "Solid Color"},
};

const uint8_t patternCount = ARRAY_SIZE(patterns);

void ICACHE_FLASH_ATTR setPower(uint8_t value) { power = value == 0 ? 0 : 1; }

void ICACHE_FLASH_ATTR setPattern(int value) {
    // don't wrap around at the ends
    if (value < 0)
        value = 0;
    else if (value >= patternCount)
        value = patternCount - 1;

    currentPatternIndex = value;

    if (autoplayEnabled == 0) {
        EEPROM.write(1, currentPatternIndex);
        EEPROM.commit();
    }
}

void ICACHE_FLASH_ATTR setSolidColor(uint8_t r, uint8_t g, uint8_t b) {
    solidColor = CRGB(r, g, b);

    EEPROM.write(2, r);
    EEPROM.write(3, g);
    EEPROM.write(4, b);

    setPattern(patternCount - 1);
}

void ICACHE_FLASH_ATTR setSolidColor(CRGB color) { setSolidColor(color.r, color.g, color.b); }

// increase or decrease the current pattern number, and wrap around at the ends
void ICACHE_FLASH_ATTR adjustPattern(bool up) {
    if (up)
        currentPatternIndex++;
    else
        currentPatternIndex--;

    // wrap around at the ends
    if (currentPatternIndex < 0) currentPatternIndex = patternCount - 1;
    if (currentPatternIndex >= patternCount) currentPatternIndex = 0;

    if (autoplayEnabled) {
        EEPROM.write(1, currentPatternIndex);
        EEPROM.commit();
    }
}

void setPalette(int value) {
    // don't wrap around at the ends
    if (value < 0)
        value = 0;
    else if (value >= paletteCount)
        value = paletteCount - 1;

    currentPaletteIndex = value;

    EEPROM.write(5, currentPaletteIndex);
    EEPROM.commit();
}

// adjust the brightness, and wrap around at the ends
void adjustBrightness(bool up) {
    if (up)
        brightnessIndex++;
    else
        brightnessIndex--;

    // wrap around at the ends
    if (brightnessIndex < 0)
        brightnessIndex = brightnessCount - 1;
    else if (brightnessIndex >= brightnessCount)
        brightnessIndex = 0;

    brightness = brightnessMap[brightnessIndex];

    FastLED.setBrightness(brightness);

    EEPROM.write(0, brightness);
    EEPROM.commit();
}

void setBrightness(int value) {
    // don't wrap around at the ends
    if (value > 255)
        value = 255;
    else if (value < 0)
        value = 0;

    brightness = value;

    FastLED.setBrightness(brightness);

    EEPROM.write(0, brightness);
    EEPROM.commit();
}

void showSolidColor() { fill_solid(leds, NUM_LEDS, solidColor); }



/** Commands **/
void ICACHE_FLASH_ATTR sendAll() {
    String json = "{";

    json += "\"power\":" + String(power) + ",";
    json += "\"brightness\":" + String(brightness) + ",";

    json += "\"currentPattern\":{";
    json += "\"index\":" + String(currentPatternIndex);
    json += ",\"name\":\"" + patterns[currentPatternIndex].name + "\"}";

    json += ",\"currentPalette\":{";
    json += "\"index\":" + String(currentPaletteIndex);
    json += ",\"name\":\"" + paletteNames[currentPaletteIndex] + "\"}";

    json += ",\"solidColor\":{";
    json += "\"r\":" + String(solidColor.r);
    json += ",\"g\":" + String(solidColor.g);
    json += ",\"b\":" + String(solidColor.b);
    json += "}";

    json += ",\"patterns\":[";
    for (uint8_t i = 0; i < patternCount; i++) {
        json += "\"" + patterns[i].name + "\"";
        if (i < patternCount - 1) json += ",";
    }
    json += "]";

    json += ",\"palettes\":[";
    for (uint8_t i = 0; i < paletteCount; i++) {
        json += "\"" + paletteNames[i] + "\"";
        if (i < paletteCount - 1) json += ",";
    }
    json += "]";

    json += "}";

    server.send(200, "text/json", json);
    json = String();
}

void ICACHE_FLASH_ATTR sendPower() {
    String json = String(power);
    server.send(200, "text/json", json);
    json = String();
}

void ICACHE_FLASH_ATTR sendPattern() {
    String json = "{";
    json += "\"index\":" + String(currentPatternIndex);
    json += ",\"name\":\"" + patterns[currentPatternIndex].name + "\"";
    json += "}";
    server.send(200, "text/json", json);
    json = String();
}

void ICACHE_FLASH_ATTR sendPalette() {
    String json = "{";
    json += "\"index\":" + String(currentPaletteIndex);
    json += ",\"name\":\"" + paletteNames[currentPaletteIndex] + "\"";
    json += "}";
    server.send(200, "text/json", json);
    json = String();
}

void ICACHE_FLASH_ATTR sendBrightness() {
    String json = String(brightness);
    server.send(200, "text/json", json);
    json = String();
}

void ICACHE_FLASH_ATTR sendSolidColor() {
    String json = "{";
    json += "\"r\":" + String(solidColor.r);
    json += ",\"g\":" + String(solidColor.g);
    json += ",\"b\":" + String(solidColor.b);
    json += "}";
    server.send(200, "text/json", json);
    json = String();
}


/** Here we go! **/
void ICACHE_FLASH_ATTR setup() {
    Serial.begin(115200);
    Serial.println("Merry Christmas!");

    pinMode(BUILTIN_LED, OUTPUT);  // set led pin as output
    ticker.attach(0.6, toggleLed);

    configFileRead();

    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);

    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setSaveConfigCallback(configFileWriteCallback);
    wifiManager.setConfigPortalTimeout(180);
    if (!wifiManager.autoConnect("HO-HO-HO")) {
        Serial.println(F("Failed to connect and hit timeout"));
        ESP.reset();  // reset and try again,
        delay(1000);
    }

    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    configFileWrite();

    Serial.println(F("Connected... yeey :)"));

    ticker.detach();
    digitalWrite(BUILTIN_LED, LOW);  // keep LED on

    Serial.println(mqtt_server);
    Serial.println(mqtt_port);

    FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);  // for WS2812 (Neopixel)
    FastLED.setCorrection(TypicalLEDStrip);
    FastLED.setBrightness(brightness);
    FastLED.setMaxPowerInVoltsAndMilliamps(5, MILLI_AMPS);
    fill_solid(leds, NUM_LEDS, solidColor);
    FastLED.show();

    EEPROM.begin(512);
    loadSettings();

    FastLED.setBrightness(brightness);

    Serial.println();
    Serial.print(F("Heap: "));
    Serial.println(system_get_free_heap_size());
    Serial.print(F("Boot Vers: "));
    Serial.println(system_get_boot_version());
    Serial.print(F("CPU: "));
    Serial.println(system_get_cpu_freq());
    Serial.print(F("SDK: "));
    Serial.println(system_get_sdk_version());
    Serial.print(F("Chip ID: "));
    Serial.println(system_get_chip_id());
    Serial.print(F("Flash ID: "));
    Serial.println(spi_flash_get_id());
    Serial.print(F("Flash Size: "));
    Serial.println(ESP.getFlashChipRealSize());
    Serial.print(F("Vcc: "));
    Serial.println(ESP.getVcc());
    Serial.println();

    // server.on("/all", HTTP_GET, []() { sendAll(); });

    server.on(F("/power"), HTTP_GET, []() { sendPower(); });

    server.on(F("/power"), HTTP_POST, []() {
        String value = server.arg(F("value"));
        setPower(value.toInt());
        sendPower();
    });

    server.on(F("/solidColor"), HTTP_GET, []() { sendSolidColor(); });

    server.on(F("/solidColor"), HTTP_POST, []() {
        String r = server.arg("r");
        String g = server.arg("g");
        String b = server.arg("b");
        setSolidColor(r.toInt(), g.toInt(), b.toInt());
        sendSolidColor();
    });

    server.on(F("/pattern"), HTTP_GET, []() { sendPattern(); });

    server.on(F("/pattern"), HTTP_POST, []() {
        String value = server.arg(F("value"));
        setPattern(value.toInt());
        sendPattern();
    });

    server.on(F("/patternUp"), HTTP_POST, []() {
        adjustPattern(true);
        sendPattern();
    });

    server.on(F("/patternDown"), HTTP_POST, []() {
        adjustPattern(false);
        sendPattern();
    });

    server.on(F("/brightness"), HTTP_GET, []() { sendBrightness(); });

    server.on(F("/brightness"), HTTP_POST, []() {
        String value = server.arg(F("value"));
        setBrightness(value.toInt());
        sendBrightness();
    });

    server.on(F("/brightnessUp"), HTTP_POST, []() {
        adjustBrightness(true);
        sendBrightness();
    });

    server.on(F("/brightnessDown"), HTTP_POST, []() {
        adjustBrightness(false);
        sendBrightness();
    });

    server.on(F("/palette"), HTTP_GET, []() { sendPalette(); });

    server.on(F("/palette"), HTTP_POST, []() {
        String value = server.arg(F("value"));
        setPalette(value.toInt());
        sendPalette();
    });

    server.on(F("/factoryReset"), HTTP_GET, []() {
        Serial.println(F("Resetting to Factory Defaults"));
        delay(500);
        wifiManager.resetSettings();
        delay(500);
        ESP.reset();
        delay(1000);
    });

    // server.serveStatic("/index.htm", SPIFFS, "/index.htm");
    // server.serveStatic("/fonts", SPIFFS, "/fonts", "max-age=86400");
    // server.serveStatic("/js", SPIFFS, "/js");
    // server.serveStatic("/css", SPIFFS, "/css", "max-age=86400");
    // server.serveStatic("/images", SPIFFS, "/images", "max-age=86400");
    // server.serveStatic("/", SPIFFS, "/index.htm");

    server.begin();
    Serial.println(F("HTTP server started"));

    // mqttClient.setServer(mqtt_server, atoi(mqtt_port));
    // Serial.println("HTTP server started");

    autoPlayTimeout = millis() + (autoPlayDurationSeconds * 1000);
}

void ICACHE_FLASH_ATTR loop() {
    // Add entropy to random number generator; we use a lot of it.
    random16_add_entropy(random(65535));

    server.handleClient();

    if (power == 0) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        FastLED.delay(15);
        return;
    }

    // EVERY_N_SECONDS(10) {
    //   Serial.print( F("Heap: ") ); Serial.println(system_get_free_heap_size());
    // }

    EVERY_N_MILLISECONDS(20) {
        gHue++;  // slowly cycle the "base color" through the rainbow
    }

    // change to a new cpt-city gradient palette
    EVERY_N_SECONDS(SECONDS_PER_PALETTE) {
        gCurrentPaletteNumber = addmod8(gCurrentPaletteNumber, 1, gGradientPaletteCount);
        gTargetPalette = gGradientPalettes[gCurrentPaletteNumber];
    }

    // slowly blend the current cpt-city gradient palette to the next
    EVERY_N_MILLISECONDS(40) { nblendPaletteTowardPalette(gCurrentPalette, gTargetPalette, 16); }

    if (autoplayEnabled && millis() > autoPlayTimeout) {
        adjustPattern(true);
        autoPlayTimeout = millis() + (autoPlayDurationSeconds * 1000);
    }

    // Call the current pattern function once, updating the 'leds' array
    patterns[currentPatternIndex].pattern();

    FastLED.show();

    // insert a delay to keep the framerate modest
    FastLED.delay(1000 / FRAMES_PER_SECOND);
}

void loadSettings() {
    brightness = EEPROM.read(0);

    currentPatternIndex = EEPROM.read(1);
    if (currentPatternIndex < 0)
        currentPatternIndex = 0;
    else if (currentPatternIndex >= patternCount)
        currentPatternIndex = patternCount - 1;

    byte r = EEPROM.read(2);
    byte g = EEPROM.read(3);
    byte b = EEPROM.read(4);

    if (r == 0 && g == 0 && b == 0) {
    } else {
        solidColor = CRGB(r, g, b);
    }

    currentPaletteIndex = EEPROM.read(5);
    if (currentPaletteIndex < 0)
        currentPaletteIndex = 0;
    else if (currentPaletteIndex >= paletteCount)
        currentPaletteIndex = paletteCount - 1;
}