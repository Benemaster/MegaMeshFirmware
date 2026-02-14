// ESP32 LoRa configuration sketch
// Supports configuration over USB Serial or BluetoothSerial
// Uses LoRaLib for SX1262 initialization (install LoRaLib from Library Manager)

#include <Arduino.h>
#include <LoRaLib.h>
#include <BluetoothSerial.h>
#include <Preferences.h>

BluetoothSerial SerialBT;
Preferences prefs;

struct LoraConfig
{
    uint32_t magic;     // validation
    uint8_t deviceType; // 0 = heltec, 1 = sx1262/custom
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
    // Heltec LoRa V3 typical pinout (change if needed)
    cfg.csPin = 18;
    cfg.resetPin = 14;
    cfg.busyPin = 26;
    cfg.dioPin = 33;
    cfg.frequency = 868.0;
    cfg.bandwidth = 125.0;
    cfg.spreadingFactor = 7;
    cfg.codingRate = 5; // CR 4/5 -> usually encoded 5
    cfg.syncWord = 0x12;
    cfg.preambleLength = 8;
    cfg.tcxoVoltage = 0.0;
    cfg.useDio2AsRfSwitch = false;
}

void setDefaultsSX1262()
{
    cfg.deviceType = 1;
    // Example SX1262 TCXO module pins (user may override)
    cfg.csPin = 5;
    cfg.resetPin = 14;
    cfg.busyPin = 12;
    cfg.dioPin = 13;
    cfg.frequency = 868.0;
    cfg.bandwidth = 125.0;
    cfg.spreadingFactor = 9;
    cfg.codingRate = 7;
    cfg.syncWord = 0x12;
    cfg.preambleLength = 22;
    cfg.tcxoVoltage = 1.6;
    cfg.useDio2AsRfSwitch = false;
}

void saveConfig()
{
    prefs.begin("lora", false);
    prefs.putBytes("cfg", &cfg, sizeof(cfg));
    prefs.end();
    Serial.println("Config saved.");
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
    Serial.println("--- LoRa Config ---");
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
    Serial.println("-------------------");
}

// Very small parser utilities
String readLine(Stream &s)
{
    String line;
    while (s.available())
    {
        char c = s.read();
        if (c == '\r')
            continue;
        if (c == '\n')
            break;
        line += c;
    }
    return line;
}

void handleCommand(String cmd)
{
    cmd.trim();
    if (cmd.length() == 0)
        return;
    cmd.toLowerCase();
    if (cmd == "help")
    {
        Serial.println("Commands: help, show, device heltec|sx1262, set <key> <value>, init, save, load, reboot");
        return;
    }
    if (cmd == "show")
    {
        printConfig();
        return;
    }
    if (cmd.startsWith("device "))
    {
        if (cmd.indexOf("heltec") > 0)
            setDefaultsHeltec();
        else
            setDefaultsSX1262();
        Serial.println("Device defaults applied. Use 'show' then 'save' if OK.");
        return;
    }
    if (cmd.startsWith("set "))
    {
        // format: set key value
        int sp = cmd.indexOf(' ', 4);
        String key, val;
        if (sp > 0)
        {
            key = cmd.substring(4, sp);
            val = cmd.substring(sp + 1);
        }
        else
        {
            Serial.println("Invalid set syntax");
            return;
        }
        key.trim();
        val.trim();
        if (key == "cs")
            cfg.csPin = val.toInt();
        else if (key == "reset")
            cfg.resetPin = val.toInt();
        else if (key == "busy")
            cfg.busyPin = val.toInt();
        else if (key == "dio")
            cfg.dioPin = val.toInt();
        else if (key == "freq")
            cfg.frequency = val.toFloat();
        else if (key == "bw")
            cfg.bandwidth = val.toFloat();
        else if (key == "sf")
            cfg.spreadingFactor = val.toInt();
        else if (key == "cr")
            cfg.codingRate = val.toInt();
        else if (key == "sync")
        {
            if (val.startsWith("0x"))
                cfg.syncWord = (uint8_t)strtoul(val.c_str() + 2, NULL, 16);
            else
                cfg.syncWord = val.toInt();
        }
        else if (key == "preamble")
            cfg.preambleLength = val.toInt();
        else if (key == "tcxo")
            cfg.tcxoVoltage = val.toFloat();
        else if (key == "dio2")
            cfg.useDio2AsRfSwitch = (val == "1" || val == "true");
        else
        {
            Serial.println("Unknown key");
            return;
        }
        Serial.println("OK");
        return;
    }
    if (cmd == "save")
    {
        cfg.magic = CFG_MAGIC;
        saveConfig();
        return;
    }
    if (cmd == "load")
    {
        if (loadConfig())
            Serial.println("Config loaded.");
        else
            Serial.println("No valid saved config");
        return;
    }
    if (cmd == "init")
    {
        Serial.println("Initializing radio with current settings...");
        // Use LoRaLib initialization pattern provided by you. Adjust pins/order if your setup requires.
        // Example user snippet:
        // SX1262 radio = new Module(8, 14, 12, 13);
        // int state = radio.begin(868.0, 125.0, 9, 7, 0x12, 22, 1.6, false);

        // NOTE: The exact constructor parameter order depends on LoRaLib version.
        // Replace below line with the correct Module(...) call if needed.
        {
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
                Serial.println("Radio initialized successfully!");
            else
            {
                Serial.print("Radio init error: ");
                Serial.println(state);
            }
        }
        return;
    }
    if (cmd == "reboot")
    {
        Serial.println("Rebooting...");
        delay(200);
        ESP.restart();
        return;
    }
    Serial.println("Unknown command. Type 'help'.");
}

void checkSerials()
{
    // read lines from Serial and SerialBT
    while (Serial.available())
    {
        String line = Serial.readStringUntil('\n');
        handleCommand(line);
    }
    while (SerialBT.available())
    {
        String line = SerialBT.readStringUntil('\n');
        handleCommand(line);
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.println("ESP32 LoRa Configurator starting...");
    SerialBT.begin("ESP32-LoRaCfg");
    Serial.println("Bluetooth started: ESP32-LoRaCfg");

    if (!loadConfig())
    {
        // no stored config, set heltec defaults
        setDefaultsHeltec();
        Serial.println("No saved config found, applied Heltec defaults.");
    }
    else
    {
        Serial.println("Loaded saved config.");
    }
    printConfig();
    Serial.println("Type 'help' for commands.");
}

void loop()
{
    checkSerials();
    delay(20);
}
