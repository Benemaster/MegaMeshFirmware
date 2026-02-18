# MegaMesh Configurator App — Agent Instructions

## Goal

Build a single-page **web app** (HTML + vanilla JS, no build toolchain required) and/or a cross-platform **Android/Flutter app** that lets a user configure an ESP32 LoRa node over **Bluetooth Low Energy (Web Bluetooth / Flutter blue_plus)**. The app talks to the firmware in `testing/esp32_lora_unified.ino`.

---

## BLE Transport

### Discovery

```js
// Web Bluetooth
const device = await navigator.bluetooth.requestDevice({
  filters: [{ namePrefix: "ESP32-LoRaCfg" }],
  optionalServices: ["6e400001-b5a3-f393-e0a9-e50e24dcca9e"],
});
```

- Device name pattern: `ESP32-LoRaCfg-<hex16>` where `<hex16>` is the lower 16 bits of the MAC.
- Fixed service UUID (always present): `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- RX characteristic (write, send commands TO device): `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- TX characteristic (notify, receive events FROM device): `6e400003-b5a3-f393-e0a9-e50e24dcca9e`

### Sending a command

```js
async function sendCmd(cmd) {
  const data = new TextEncoder().encode(cmd + "\n");
  await rxChar.writeValue(data);
}
```

### Receiving events

```js
await txChar.startNotifications();
txChar.addEventListener("characteristicvaluechanged", (ev) => {
  const text = new TextDecoder().decode(ev.target.value);
  const obj = JSON.parse(text);
  handleEvent(obj);
});
```

---

## Firmware Protocol

All messages are UTF-8 JSON lines.

### Event flow on first boot / config mode

1. Firmware sends `{"evt":"first_boot"}` or `{"evt":"boot"}` immediately after reset.
2. Firmware sends `{"evt":"config_mode"}` when entering setup.
3. Firmware sends a **`setup_info`** event — this is the main handshake the app must parse:

```json
{
  "evt": "setup_info",
  "device": "heltec",
  "fields": [
    { "k": "device", "v": "heltec", "opts": "heltec|wroom" },
    { "k": "cs", "v": 18, "type": "pin" },
    { "k": "reset", "v": 14, "type": "pin" },
    { "k": "busy", "v": 26, "type": "pin" },
    { "k": "dio", "v": 33, "type": "pin" },
    { "k": "freq", "v": 868.0, "unit": "MHz", "min": 150, "max": 960 },
    {
      "k": "bw",
      "v": 125.0,
      "unit": "kHz",
      "opts": "7.8|10.4|15.6|20.8|31.25|41.7|62.5|125|250|500"
    },
    { "k": "sf", "v": 7, "min": 5, "max": 12 },
    { "k": "cr", "v": 5, "min": 5, "max": 8 },
    { "k": "sync", "v": "0x12", "type": "hex" },
    { "k": "preamble", "v": 8, "min": 2, "max": 65535 },
    {
      "k": "tcxo",
      "v": 0.0,
      "unit": "V",
      "opts": "0.0|1.6|1.7|1.8|2.2|2.4|2.7|3.3"
    },
    { "k": "dio2", "v": 0, "opts": "0|1" },
    { "k": "bt", "v": 1, "opts": "0|1" }
  ],
  "cmds": "device <heltec|wroom> | set <k> <v> | save | init | show | reboot"
}
```

4. The firmware periodically (every 5 s) sends a `cfg_status` event until configuration is complete:

```json
{
  "evt": "cfg_status",
  "saved": false,
  "radio_ok": false,
  "hint": "send save when all fields are correct"
}
```

5. After `save` then `init`, the firmware sends:

```json
{"evt":"cfg_saved"}
{"evt":"radio_ready"}       // or {"evt":"radio_err","code":-2}
{"evt":"config_done"}
{"evt":"mesh_started","nodeId":12345}
```

### All firmware events the app must handle

| `evt`              | Description                                 |
| ------------------ | ------------------------------------------- |
| `first_boot`       | No config stored; config mode starts        |
| `boot`             | Normal boot with stored config              |
| `config_mode`      | Device waiting for configuration            |
| `setup_info`       | Full field manifest (see above)             |
| `cfg_status`       | Periodic reminder while in config mode      |
| `defaults_applied` | `device heltec/wroom` was accepted          |
| `ok`               | Last `set` command accepted                 |
| `cfg_saved`        | `save` command accepted, NVS written        |
| `auto_init`        | Device is auto-initialising radio           |
| `radio_ready`      | Radio initialised successfully              |
| `radio_err`        | Radio init failed; `code` is RadioLib error |
| `radio_not_ready`  | `startmesh` rejected (radio not ready)      |
| `config_done`      | Configuration loop exited                   |
| `mesh_started`     | Mesh running; `nodeId` field present        |
| `cfg_loaded`       | Stored config loaded on normal boot         |
| `bt_on` / `bt_off` | BLE advertising toggled                     |
| `rx`               | LoRa packet received: `len`, `data` (hex)   |
| `fwd`              | Packet forwarded: `delay` ms                |
| `fwd_err`          | Forward failed: `code`                      |
| `rebooting`        | Device is about to restart                  |
| `unknown_cmd`      | Unrecognised command                        |

### Commands the app sends

| Command                           | Description                               |
| --------------------------------- | ----------------------------------------- |
| `device heltec` or `device wroom` | Apply hardware defaults                   |
| `set <key> <value>`               | Set a single field (see field list above) |
| `save`                            | Persist config to flash                   |
| `init`                            | Initialise radio with current config      |
| `show`                            | Get compact JSON config                   |
| `info`                            | Get full `setup_info` event again         |
| `bt on` / `bt off`                | Toggle BLE advertising                    |
| `startmesh`                       | Start mesh (only if radio ready)          |
| `reboot`                          | Restart the ESP32                         |

---

## App UX Requirements

### Connection screen

- Button: **Scan & Connect** (triggers `requestDevice`)
- Show scanning spinner
- On connect: start notifications, send `info\n` to fetch `setup_info`

### Configuration wizard (shown while `evt == config_mode`)

Parse the `fields` array from `setup_info` and render dynamically:

- `opts` field → dropdown / segmented control
- `type == "pin"` → number input, 0–39
- `min`/`max` present → number input with range validation
- `type == "hex"` → text input, validate `/^0x[0-9A-Fa-f]{1,2}$/`
- `unit` field → show unit label next to input

Step the user through fields or show them all at once in a form.

**Workflow buttons:**

1. **Apply defaults** — sends `device heltec` or `device wroom`
2. After each field change — sends `set <k> <v>`; wait for `{"evt":"ok"}` before enabling next field (or batch and send all)
3. **Save** — sends `save`; wait for `{"evt":"cfg_saved"}`
4. **Init Radio** — sends `init`; wait for `{"evt":"radio_ready"}` or `{"evt":"radio_err",...}`
5. On success → show **Configuration complete** screen with nodeId

### Status bar (always visible while connected)

Show: `saved: yes/no | radio: ok/error | mesh: running/stopped`

Update from `cfg_status` events.

### Live monitor (optional, but recommended)

Display a scrollable log of received `evt` JSON lines in real time.

---

## Web App Tech Stack (recommended)

- Single HTML file, no dependencies (or minimal: lit-html / Alpine.js)
- Web Bluetooth API (Chrome/Edge on desktop+Android; not Safari)
- CSS: simple responsive grid, mobile-first
- Host as `index.html` in a new `configurator/` folder in this repo

## Android App Tech Stack (recommended)

- **Flutter** with package `flutter_blue_plus: ^1.x`
- Targets Android 6.0+ (API 23+), `BLUETOOTH_SCAN` + `BLUETOOTH_CONNECT` permissions
- Dart models auto-generated from the `fields` schema in `setup_info`
- Place source in `configurator_android/` in this repo

---

## Implementation Checklist

### Web app (`configurator/index.html`)

- [ ] BLE connect/disconnect button and status indicator
- [ ] Send `info` on connect, parse `setup_info` event
- [ ] Render a dynamic form from the `fields` array
- [ ] Send `device` command when device type selector changes
- [ ] Send `set k v` for each field change, show ack spinner
- [ ] Save button → `save` → wait for `cfg_saved`
- [ ] Init button → `init` → wait for `radio_ready` / `radio_err`
- [ ] Show error details on `radio_err`
- [ ] Show `mesh_started` confirmation with nodeId
- [ ] Live event log panel
- [ ] Reboot button

### Android app (`configurator_android/`)

- [ ] Scaffold Flutter project
- [ ] BLE scan screen with device list filtered to `ESP32-LoRaCfg-*`
- [ ] Notification listener → JSON event dispatcher
- [ ] `ConfigModel` data class with all fields
- [ ] Config form screen, fields driven by `setup_info`
- [ ] Save / Init / Reboot actions
- [ ] Status snackbars for each event type
- [ ] Event log screen

---

## File layout to create

```
MegaMeshFirmware/
├── configurator/
│   ├── index.html          # Web BLE configurator (self-contained)
│   └── README.md           # Build/run instructions
└── configurator_android/
    ├── pubspec.yaml
    ├── lib/
    │   ├── main.dart
    │   ├── ble_service.dart        # BLE connect + notify
    │   ├── config_model.dart       # LoraConfig data class
    │   ├── screens/
    │   │   ├── scan_screen.dart
    │   │   ├── config_screen.dart
    │   │   └── monitor_screen.dart
    │   └── widgets/
    │       └── field_editor.dart   # Dynamic field widget factory
    └── android/
        └── app/src/main/AndroidManifest.xml  # BLE permissions
```

---

## Important Notes for the Agent

1. The firmware emits JSON events **one object per line** over BLE notifications. Parse each notification independently.
2. BLE MTU is typically 20–512 bytes. Long JSON strings (like `setup_info`) may arrive in multiple notifications. Buffer incoming data and split on `}` boundary or use a length-prefix strategy if needed.
3. The `setup_info` fields array is the **single source of truth** for what to render — do not hard-code field names in the UI.
4. On `radio_err`, display the RadioLib numeric code with a lookup table:
   - `-2` = ERR_UNKNOWN
   - `-4` = ERR_CHIP_NOT_FOUND
   - `-6` = ERR_INVALID_BANDWIDTH
   - `-7` = ERR_INVALID_SPREADING_FACTOR
   - `-701` = ERR_SPI_CMD_FAILED
5. The app should reconnect automatically on BLE disconnect.
6. `bt off` will terminate the BLE connection. Warn the user before sending it.
