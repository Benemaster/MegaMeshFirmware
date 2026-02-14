// Unified ESP32 LoRa firmware
// - First-boot: opens USB Serial + Bluetooth for configuration
// - Saves configuration in Preferences
// - Initializes LoRa (SX1262 or similar) via LoRaLib
// - Starts basic mesh scaffolding (placeholder for your protocol)
//
// Note: Adjust Module(...) constructor if your LoRaLib version differs.

#include <Arduino.h>
#include <RadioLib.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <Preferences.h>
// ESP-IDF BT helpers for explicit discoverability
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"

// Note: use ESP-IDF GAP enums `ESP_BT_CONNECTABLE`, `ESP_BT_GENERAL_DISCOVERABLE`,
// `ESP_BT_NON_CONNECTABLE`, `ESP_BT_NON_DISCOVERABLE` for scan mode control.

// BLE GATT UART objects
BLEAdvertising *pAdvertising = nullptr;
BLECharacteristic *pTXChar = nullptr;
BLECharacteristic *pRXChar = nullptr;
// also expose a fixed, well-known NUS-like service so web apps can request it
BLECharacteristic *pTXCharBase = nullptr;
BLECharacteristic *pRXCharBase = nullptr;
Preferences prefs;

struct LoraConfig
{
    uint32_t magic;
    uint8_t deviceType; // 0=Heltec onboard, 1=WROOM+external
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
    bool btEnabled;
};

const uint32_t CFG_MAGIC = 0x4C4F5241; // 'LORA'
LoraConfig cfg;

bool configSaved = false;
bool radioInitialized = false;

// LoRa radio pointer (instantiate when initializing)
SX1262 *radioPtr = nullptr;
bool btActive = false;

// Mesh placeholders
uint16_t nodeId = 0; // derived from MAC
bool meshRunning = false;

// forward declaration for BLE callback usage
void handleCommand(String raw, bool fromBT);

void checkUser()
{

    // platz für User gebundene settings oder ähnliches
}
// Enable Bluetooth and set device name + discoverable mode
void enableBluetoothVisible(const char *name)
{
    // Start BLE GATT UART (NUS-like) with per-device UUIDs derived from MAC
    if (!pAdvertising)
    {
        uint64_t mac = ESP.getEfuseMac();
        uint16_t node = (uint16_t)(mac & 0xFFFF);
        uint32_t base = 0x6E400000 | (uint32_t)node;
        char svc[64], rx[64], tx[64];
        sprintf(svc, "%08X-B5A3-F393-E0A9-E50E24DCCA9E", base | 0x0001);
        sprintf(rx, "%08X-B5A3-F393-E0A9-E50E24DCCA9E", base | 0x0002);
        sprintf(tx, "%08X-B5A3-F393-E0A9-E50E24DCCA9E", base | 0x0003);

        String devName = String(name) + "-" + String(node, HEX);
        BLEDevice::init(devName.c_str());
        BLEServer *pServer = BLEDevice::createServer();
        BLEService *pService = pServer->createService(BLEUUID(svc));

        pRXChar = pService->createCharacteristic(BLEUUID(rx), BLECharacteristic::PROPERTY_WRITE);
        class LocalRXCB : public BLECharacteristicCallbacks
        {
            void onWrite(BLECharacteristic *c) override
            {
                auto tmp = c->getValue();
                String s = String(tmp.c_str());
                if (s.length())
                {
                    // treat BLE writes as CLI input
                    handleCommand(s, true);
                }
            }
        };
        pRXChar->setCallbacks(new LocalRXCB());

        pTXChar = pService->createCharacteristic(BLEUUID(tx), BLECharacteristic::PROPERTY_NOTIFY);
        // add CCCD so clients can enable notifications
        pTXChar->addDescriptor(new BLE2902());

        pService->start();
        // also create a fixed base service so browsers can request a stable UUID
        BLEService *pBase = pServer->createService(BLEUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E"));
        pRXCharBase = pBase->createCharacteristic(BLEUUID("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"), BLECharacteristic::PROPERTY_WRITE);
        pRXCharBase->setCallbacks(new LocalRXCB());
        pTXCharBase = pBase->createCharacteristic(BLEUUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"), BLECharacteristic::PROPERTY_NOTIFY);
        pTXCharBase->addDescriptor(new BLE2902());
        pBase->start();
        pAdvertising = BLEDevice::getAdvertising();
        pAdvertising->addServiceUUID(BLEUUID(svc));
        pAdvertising->addServiceUUID(BLEUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E"));
        pAdvertising->setScanResponse(true);
        pAdvertising->start();
        btActive = true;
    }
}

void disableBluetooth()
{
    if (pAdvertising)
    {
        pAdvertising->stop();
        pAdvertising = nullptr;
        pTXChar = nullptr;
        pRXChar = nullptr;
        btActive = false;
    }
}

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
    cfg.btEnabled = true;
}

void setDefaultsWroom()
{
    cfg.deviceType = 1;
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
    cfg.btEnabled = true;
}

void saveConfig()
{
    cfg.magic = CFG_MAGIC;
    prefs.begin("lora", false);
    prefs.putBytes("cfg", &cfg, sizeof(cfg));
    prefs.end();
    configSaved = true;
    sendMsg("{\"evt\":\"cfg_saved\"}");
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
        {
            configSaved = true;
            return true;
        }
    }
    prefs.end();
    return false;
}

String readLine(Stream &s)
{
    String line = s.readStringUntil('\n');
    line.trim();
    return line;
}

// send a message to USB serial and to BT if active
void sendMsg(const String &s)
{
    Serial.println(s);
    // also notify via BLE TX characteristic when connected
    if (btActive && pTXChar)
    {
        pTXChar->setValue(s);
        pTXChar->notify();
    }
}

void sendConfig()
{
    String j = "{";
    j += "\"device\":\"" + String(cfg.deviceType == 0 ? "heltec" : "wroom") + "\"";
    j += ",\"cs\":" + String(cfg.csPin);
    j += ",\"reset\":" + String(cfg.resetPin);
    j += ",\"busy\":" + String(cfg.busyPin);
    j += ",\"dio\":" + String(cfg.dioPin);
    j += ",\"freq\":" + String(cfg.frequency);
    j += ",\"bw\":" + String(cfg.bandwidth);
    j += ",\"sf\":" + String(cfg.spreadingFactor);
    j += ",\"cr\":" + String(cfg.codingRate);
    j += ",\"sync\":\"0x" + String(cfg.syncWord, HEX) + "\"";
    j += ",\"preamble\":" + String(cfg.preambleLength);
    j += ",\"tcxo\":" + String(cfg.tcxoVoltage);
    j += ",\"dio2\":" + String(cfg.useDio2AsRfSwitch ? 1 : 0);
    j += ",\"bt\":" + String(cfg.btEnabled ? 1 : 0);
    j += "}";
    // print config to USB serial (BLE notifications already handled by sendMsg)
    Serial.println(j);
}

void handleCommand(String raw, bool fromBT = false)
{
    String cmd = raw;
    cmd.toLowerCase();
    if (cmd == "bt on")
    {
        cfg.btEnabled = true;
        // enable immediately and make discoverable
        enableBluetoothVisible("ESP32-LoRaCfg");
        sendMsg("{\"evt\":\"bt_on\"}");
        return;
    }
    if (cmd == "bt off")
    {
        cfg.btEnabled = false;
        disableBluetooth();
        sendMsg("{\"evt\":\"bt_off\"}");
        return;
    }
    if (cmd == "help")
    {
        sendMsg("{\"cmds\":\"help,show,device,set,save,init,startmesh,reboot\"}");
        return;
    }
    if (cmd == "show")
    {
        sendConfig();
        return;
    }
    if (cmd.startsWith("device "))
    {
        if (cmd.indexOf("heltec") > 0)
            setDefaultsHeltec();
        else
            setDefaultsWroom();
        sendMsg("{\"evt\":\"defaults_applied\"}");
        return;
    }
    if (cmd.startsWith("set "))
    {
        int sp = cmd.indexOf(' ', 4);
        if (sp < 0)
        {
            Serial.println("Invalid set syntax");
            return;
        }
        String key = cmd.substring(4, sp);
        String val = cmd.substring(sp + 1);
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
        sendMsg("{\"evt\":\"ok\"}");
        return;
    }
    if (cmd == "save")
    {
        saveConfig();
        return;
    }
    if (cmd == "init")
    {
        // initialize radio (RadioLib: Module + SX1262)
        if (radioPtr)
        {
            delete radioPtr;
            radioPtr = nullptr;
        }
        // Module(cs, irq(DIO0), rst, gpio(BUSY))
        Module *mod = new Module(cfg.csPin, cfg.dioPin, cfg.resetPin, cfg.busyPin);
        radioPtr = new SX1262(mod);
        int16_t state = radioPtr->begin(cfg.frequency, cfg.bandwidth, cfg.spreadingFactor, cfg.codingRate, cfg.syncWord, 10, cfg.preambleLength, cfg.tcxoVoltage, false);
        if (state == RADIOLIB_ERR_NONE)
        {
            radioInitialized = true;
            sendMsg("{\"evt\":\"radio_ready\"}");
        }
        else
        {
            sendMsg(String("{\"evt\":\"radio_err\",\"code\":") + state + "}");
        }
        return;
    }
    if (cmd == "startmesh")
    {
        if (!radioInitialized)
        {
            sendMsg("{\"evt\":\"radio_not_ready\"}");
            return;
        }
        uint64_t mac = ESP.getEfuseMac();
        nodeId = (uint16_t)(mac & 0xFFFF);
        sendMsg(String("{\"evt\":\"mesh_started\",\"nodeId\":") + nodeId + "}");
        // Mesh run handled in loop() via receive/forward
        return;
    }
    if (cmd == "reboot")
    {
        sendMsg("{\"evt\":\"rebooting\"}");
        delay(200);
        ESP.restart();
        return;
    }
    sendMsg("{\"evt\":\"unknown_cmd\"}");
}

void checkInputs()
{
    if (Serial.available())
    {
        String l = readLine(Serial);
        if (l.length())
            handleCommand(l, false);
    }
}

void startConfigMode()
{
    sendMsg("{\"evt\":\"config_mode\"}");
    // remain in configuration loop until saved and initialized
    unsigned long started = millis();
    while (true)
    {
        checkInputs();
        // if config saved and radio initialized, break
        if (configSaved && radioInitialized)
            break;
        // If saved but not initialized, try auto init
        if (configSaved && !radioInitialized)
        {
            sendMsg("{\"evt\":\"auto_init\"}");
            handleCommand("init");
        }
        delay(50);
    }
    // after config mode exit, start mesh automatically
    uint64_t mac = ESP.getEfuseMac();
    nodeId = (uint16_t)(mac & 0xFFFF);
    meshRunning = true;
}

void setup()
{
    Serial.begin(115200);
    // attempt to load config before deciding whether to start Bluetooth
    loadConfig();
    if (cfg.btEnabled)
    {
        enableBluetoothVisible("ESP32-LoRaCfg");
        sendMsg("{\"evt\":\"boot\"}");
    }

    // try to load stored config
    if (!configSaved)
    {
        sendMsg("{\"evt\":\"first_boot\"}");
        // apply safe defaults for Heltec to show something
        setDefaultsHeltec();
        // ensure Bluetooth active for first-boot interaction
        if (!btActive)
        {
            enableBluetoothVisible("ESP32-LoRaCfg");
        }
        startConfigMode();
    }
    else
    {
        sendMsg("{\"evt\":\"cfg_loaded\"}");
        sendConfig();
        // auto-init radio
        handleCommand("init");
    }

    // if radio initialized, optionally start mesh
    if (radioInitialized)
    {
        sendMsg("{\"evt\":\"radio_ready\"}");
        // auto-start mesh
        uint64_t mac = ESP.getEfuseMac();
        nodeId = (uint16_t)(mac & 0xFFFF);
        meshRunning = true;
    }
}

// Simple CRC16 for duplicate detection
static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    while (len--)
    {
        crc ^= (uint16_t)(*data++) << 8;
        for (uint8_t i = 0; i < 8; i++)
        {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

// very small duplicate cache
#define DUP_CACHE_SIZE 24
static uint16_t dup_cache[DUP_CACHE_SIZE];
static uint8_t dup_head = 0;

bool isDuplicate(uint16_t crc)
{
    for (uint8_t i = 0; i < DUP_CACHE_SIZE; i++)
        if (dup_cache[i] == crc && crc != 0)
            return true;
    // insert
    dup_cache[dup_head++] = crc;
    if (dup_head >= DUP_CACHE_SIZE)
        dup_head = 0;
    return false;
}

String toHex(const uint8_t *buf, size_t len)
{
    String s;
    s.reserve(len * 2 + 4);
    for (size_t i = 0; i < len; i++)
    {
        uint8_t hi = (buf[i] >> 4) & 0x0F;
        uint8_t lo = buf[i] & 0x0F;
        s += (char)(hi < 10 ? '0' + hi : 'A' + hi - 10);
        s += (char)(lo < 10 ? '0' + lo : 'A' + lo - 10);
    }
    return s;
}

// Basic incoming packet handler: dedupe, print, and re-broadcast
void handleIncoming(uint8_t *buf, size_t len)
{
    uint16_t crc = crc16_ccitt(buf, len);
    if (isDuplicate(crc))
    {
        // ignore duplicates
        return;
    }
    // send event to USB/BT
    String j = "{";
    j += "\"evt\":\"rx\",";
    j += "\"len\":" + String(len) + ",";
    j += "\"data\":\"" + toHex(buf, len) + "\"";
    j += "}";
    sendMsg(j);

    // simple forwarding: retransmit the same payload after a small random backoff
    if (radioPtr)
    {
        // short jitter to reduce collisions
        int backoff = random(20, 120);
        delay(backoff);
        int16_t st = radioPtr->transmit(buf, len);
        if (st == RADIOLIB_ERR_NONE)
        {
            sendMsg(String("{\"evt\":\"fwd\",\"delay\":") + backoff + "}");
        }
        else
        {
            sendMsg(String("{\"evt\":\"fwd_err\",\"code\":") + st + "}");
        }
    }
}

void loop()
{
    // keep servicing serial/bt inputs
    checkInputs();
    // mesh runtime: poll radio and forward received packets
    if (meshRunning && radioInitialized && radioPtr)
    {
        // attempt to receive (RadioLib API)
        uint8_t buf[256];
        size_t received = 0;
        // RadioLib blocking receive with timeout (ms) - adjust timeout as needed
        // SX126x::receive signature is (uint8_t* data, size_t len, RadioLibTime_t timeout)
        int16_t state = radioPtr->receive(buf, sizeof(buf), 100);
        if (state > 0)
        {
            // some RadioLib versions return number of bytes received
            size_t received = (size_t)state;
            handleIncoming(buf, received);
        }
        else if (state == RADIOLIB_ERR_NONE)
        {
            // success but no length returned; attempt to assume full buffer until a higher-level API is available
            // best-effort: treat as zero-length (ignore) to avoid misreads
        }
        // Note: if no compatible receive method is present, replace the above with
        // your LoRaLib's non-blocking receive call and call handleIncoming() when data arrives.
    }
    delay(20);
}
