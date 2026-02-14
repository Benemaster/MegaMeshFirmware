// Main sketch that loads LoRa configuration from Preferences
// and initializes LoRaLib SX1262 using the saved settings.

#include <Arduino.h>
#include <LoRaLib.h>
#include <Preferences.h>

Preferences prefs;

struct LoraConfig
{
    uint32_t magic;
    uint8_t deviceType;
    uint8_t csPin;
    uint8_t resetPin;
    uint8_t busyPin;
    uint8_t dioPin;
    float frequency;
    float bandwidth;
    uint8_t spreadingFactor;
    uint8_t codingRate;
    uint8_t syncWord;
    uint16_t preambleLength;
    float tcxoVoltage;
    bool useDio2AsRfSwitch;
};

const uint32_t CFG_MAGIC = 0x4C4F5241; // 'LORA'
LoraConfig cfg;

void setDefaultsHeltec()
{
    cfg.deviceType = 0;
    cfg.csPin = 18;
    cfg.resetPin = 14;
    cfg.busyPin = 26;
    cfg.dioPin = 33;
    cfg.frequency = 868.0;
    cfg.bandwidth = 125.0;
    cfg.spreadingFactor = 7;
    cfg.codingRate = 5;
    cfg.syncWord = 0x12;
    cfg.preambleLength = 8;
    cfg.tcxoVoltage = 0.0;
    cfg.useDio2AsRfSwitch = false;
}

bool loadConfig()
{
    prefs.begin("lora", true);
    size_t len = prefs.getBytesLength("cfg");
    if (len == sizeof(cfg))
    {
        prefs.getBytes("cfg", &cfg, sizeof(cfg));
        prefs.end();
        if (cfg.magic == CFG_MAGIC)
            return true;
    }
    prefs.end();
    return false;
}

void printConfig()
{
    Serial.println("--- LoRa Config (main) ---");
    Serial.print("Device: ");
    Serial.println(cfg.deviceType == 0 ? "Heltec" : "SX1262/Custom");
    Serial.print("CS: ");
    Serial.println(cfg.csPin);
    Serial.print("RESET: ");
    Serial.println(cfg.resetPin);
    Serial.print("BUSY: ");
    Serial.println(cfg.busyPin);
    Serial.print("DIO: ");
    Serial.println(cfg.dioPin);
    Serial.print("Freq: ");
    Serial.println(cfg.frequency);
    Serial.print("BW: ");
    Serial.println(cfg.bandwidth);
    Serial.print("SF: ");
    Serial.println(cfg.spreadingFactor);
    Serial.print("CR: ");
    Serial.println(cfg.codingRate);
    Serial.print("Sync: 0x");
    Serial.println(cfg.syncWord, HEX);
    Serial.print("Preamble: ");
    Serial.println(cfg.preambleLength);
    Serial.print("TCXO V: ");
    Serial.println(cfg.tcxoVoltage);
    Serial.print("Use DIO2 as RF switch: ");
    Serial.println(cfg.useDio2AsRfSwitch ? "true" : "false");
    Serial.println("-------------------------");
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Main: loading LoRa config...");
    if (!loadConfig())
    {
        Serial.println("No saved config found — applying Heltec defaults.");
        setDefaultsHeltec();
    }
    else
    {
        Serial.println("Loaded saved LoRa config.");
    }
    printConfig();

    Serial.println("Initializing SX1262 module...");
    // Use LoRaLib Module constructor with CS, RESET, BUSY, DIO
    SX1262 radio = new Module(cfg.csPin, cfg.resetPin, cfg.busyPin, cfg.dioPin);
    int state = radio.begin(
        cfg.frequency,
        cfg.bandwidth,
        cfg.spreadingFactor,
        cfg.codingRate,
        cfg.syncWord,
        cfg.preambleLength,
        cfg.tcxoVoltage,
        cfg.useDio2AsRfSwitch);

    if (state == ERR_NONE)
    {
        Serial.println("SX1262 initialized successfully.");
    }
    else
    {
        Serial.print("SX1262 init failed, error: ");
        Serial.println(state);
    }
}

void loop()
{
    // Your main application logic goes here. This file's responsibility is
    // to load saved configuration and initialize the LoRa radio.
    delay(1000);
}
