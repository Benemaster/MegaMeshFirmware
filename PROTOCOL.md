# MegaMesh Protocol – Visual Documentation

## 1. Node Architecture

```mermaid
graph TB
    subgraph "User Interfaces"
        SERIAL["Serial Console<br/>(USB CDC)"]
        BLE_APP["BLE App / Web Client<br/>(Chrome/Edge/Android)"]
    end

    subgraph "ESP32-S3 Node"
        BLE_STACK["BLE UART Service<br/>(NUS 6E400001)"]
        CMD_QUEUE["Command Ring Buffer<br/>(16 slots)"]
        CMD_HANDLER["handleSerialLine()<br/>Command Parser"]
        MESH_LOGIC["Mesh Protocol Engine"]
        ENCRYPT["AES-128-CTR<br/>Encryption"]
        NVS["NVS Flash<br/>Persistent Settings"]
        OLED["OLED Display<br/>SSD1306 128x64"]
        INBOX["Offline Inbox<br/>(10 messages)"]
    end

    subgraph "Radio Hardware"
        SX1262["SX1262 LoRa Radio<br/>869.4 MHz / SF9 / BW125"]
        PA["GC1109 PA<br/>(V4 only, +27 dBm)"]
        ANT["Antenna"]
    end

    SERIAL -->|"USB CDC"| CMD_HANDLER
    BLE_APP <-->|"GATT Notify/Write"| BLE_STACK
    BLE_STACK -->|"RX Char"| CMD_QUEUE
    CMD_QUEUE --> CMD_HANDLER
    BLE_STACK <--|"TX Char (Notify)"| MESH_LOGIC
    CMD_HANDLER --> MESH_LOGIC
    MESH_LOGIC <--> ENCRYPT
    MESH_LOGIC <--> NVS
    MESH_LOGIC --> OLED
    MESH_LOGIC <--> INBOX
    MESH_LOGIC <--> SX1262
    SX1262 <--> PA
    PA <--> ANT
```

---

## 2. Packet Structure

```
┌──────────────────────────── MeshHeader (13 bytes, packed) ──────────────────────────┐
│ magic   │ ver │ origin  │ msgId   │ dest    │ hops │ maxHops │ flags │ payloadLen │
│ 2 bytes │ 1 B │ 2 bytes │ 2 bytes │ 2 bytes │ 1 B  │ 1 B     │ 1 B   │ 1 B        │
│ 0x4D48  │  1  │ nodeId  │ seq++   │ target  │  0…n │  1…15   │ enc?  │ 0…180      │
└──────────┴─────┴─────────┴─────────┴─────────┴──────┴─────────┴───────┴────────────┘
                                                                          │
                                                         ┌───────────────┘
                                                         ▼
                                                 ┌──────────────┐
                                                 │   Payload    │
                                                 │  0–180 bytes │
                                                 │  (plaintext  │
                                                 │  or AES-CTR) │
                                                 └──────────────┘
```

**Flags:**

- `0x01` = `MESH_FLAG_ENCRYPTED` → payload is AES-128-CTR encrypted

**Special destinations:**

- `0xFFFF` = broadcast (all nodes)
- Any other value = directed to specific node

---

## 3. Multi-Hop Mesh Routing

```mermaid
sequenceDiagram
    participant A as Node A<br/>(Origin)
    participant B as Node B<br/>(Relay)
    participant C as Node C<br/>(Relay)
    participant D as Node D<br/>(Destination)

    Note over A: User sends:<br/>/msg 0xD "Hello"
    A->>B: MeshHeader{origin=A, dest=D, hops=0, maxHops=7} + "Hello"
    Note over B: Not for me,<br/>hops < maxHops → relay
    B->>C: MeshHeader{origin=A, dest=D, hops=1, maxHops=7} + "Hello"
    Note over C: Not for me,<br/>hops < maxHops → relay
    C->>D: MeshHeader{origin=A, dest=D, hops=2, maxHops=7} + "Hello"
    Note over D: dest == myId → deliver
    D->>C: ACK{origin=D, dest=A} "#MESH_ACK:A:msgId"
    C->>B: relay ACK (hops+1)
    B->>A: relay ACK (hops+1)
    Note over A: ACK received →<br/>remove from outbound buffer
```

---

## 4. Discovery Protocol (Optimized)

```mermaid
sequenceDiagram
    participant A as Node A<br/>(Scanner)
    participant B as Node B<br/>(1 hop)
    participant C as Node C<br/>(2 hops)
    participant D as Node D<br/>(3 hops)

    Note over A: /scan or /scan deep
    A->>B: BROADCAST "#MESH_DISC_REQ" (hops=0)
    A->>B: (B receives directly)
    B->>C: relay DISC_REQ (hops=1)
    C->>D: relay DISC_REQ (hops=2)

    Note over D: Respond with BROADCAST<br/>(all nodes learn about D)
    D->>C: BROADCAST "#MESH_DISC_RESP:0xD:0xA" (hops=0)
    Note over C: updateStation(D, hops=1)<br/>Not my scan → no print
    C->>B: relay DISC_RESP (hops=1)
    Note over B: updateStation(D, hops=2)<br/>Not my scan → no print
    B->>A: relay DISC_RESP (hops=2)
    Note over A: requester == me!<br/>DISCOVERED 0xD hops=3<br/>updateStation(D, hops=3)

    Note over C: Respond too
    C->>B: BROADCAST "#MESH_DISC_RESP:0xC:0xA"
    C->>D: (D also learns about C)
    B->>A: relay → DISCOVERED 0xC hops=2

    Note over B: Respond too
    B->>A: BROADCAST "#MESH_DISC_RESP:0xB:0xA"
    B->>C: (C also learns about B)
    Note over A: DISCOVERED 0xB hops=1
```

**Key optimization:** Responses are broadcast, so _every_ node on the path passively builds its station table — not just the scanner.

---

## 5. Encryption Flow

```mermaid
flowchart LR
    subgraph "Sender"
        MSG["Plaintext Message"]
        KEY_SEL{"Key Selection"}
        AES_ENC["AES-128-CTR<br/>Encrypt"]
        TX["Transmit"]
    end

    subgraph "Key Types"
        PK["Personal Key<br/>(per-node, /mykey)"]
        PUBK["Public Key<br/>(MEGAMESHPUBLIC01)"]
    end

    subgraph "Receiver"
        RX["Receive"]
        DEC_SEL{"Has peer key<br/>for origin?"}
        AES_DEC1["Decrypt with<br/>Peer Key"]
        AES_DEC2["Decrypt with<br/>Public Key"]
        OUT["Plaintext"]
    end

    MSG --> KEY_SEL
    KEY_SEL -->|"/eto (directed)"| PK
    KEY_SEL -->|"/pub (broadcast)"| PUBK
    PK --> AES_ENC
    PUBK --> AES_ENC
    AES_ENC --> TX

    TX -.->|"LoRa"| RX
    RX --> DEC_SEL
    DEC_SEL -->|"Yes"| AES_DEC1
    DEC_SEL -->|"No + broadcast"| AES_DEC2
    AES_DEC1 --> OUT
    AES_DEC2 --> OUT
```

**IV construction** (unique per message, no nonce reuse):

```
IV[0..1]  = origin node ID
IV[2..3]  = destination node ID
IV[4..5]  = message ID (incrementing)
IV[6..15] = 0x00
```

---

## 6. Trace Route

```mermaid
sequenceDiagram
    participant A as Node A
    participant B as Node B
    participant C as Node C
    participant D as Node D

    Note over A: /traceroute 0xD
    A->>B: "#MESH_TRACE_REQ:0xA"
    Note over B: Append self before relay
    B->>C: "#MESH_TRACE_REQ:0xA>0xB"
    Note over C: Append self before relay
    C->>D: "#MESH_TRACE_REQ:0xA>0xB>0xC"
    Note over D: dest == me → respond
    D->>C: "#MESH_TRACE_RESP:0xA>0xB>0xC>0xD"
    C->>B: relay response
    B->>A: relay response
    Note over A: TRACEROUTE to 0xD:<br/>0xA>0xB>0xC>0xD
```

---

## 7. Sleep & Wakeup State Machine

```mermaid
stateDiagram-v2
    [*] --> Active: Boot / Reset

    Active --> Active: radioIrq / BLE cmd / Serial input
    Active --> SleepCheck: No work + sleep enabled

    SleepCheck --> Active: Button held / Display on / BLE connected
    SleepCheck --> LightSleep: 200ms idle guard passed

    LightSleep --> Active: DIO1 (LoRa RX)
    LightSleep --> Active: BOOT button press
    LightSleep --> Active: 5s timer (maintenance)

    state Active {
        [*] --> ProcessRadio
        ProcessRadio --> ProcessBLE
        ProcessBLE --> ProcessSerial
        ProcessSerial --> ProcessOutbound
        ProcessOutbound --> UpdateDisplay
        UpdateDisplay --> SaveSettings
        SaveSettings --> [*]
    }
```

---

## 8. BLE Connection Lifecycle

```mermaid
sequenceDiagram
    participant APP as Phone / Web App
    participant NODE as MegaMesh Node

    Note over NODE: Boot → BLE advertising<br/>(3 min timeout)
    APP->>NODE: Connect (GATT)
    Note over NODE: bleConnected = true<br/>Flush offline inbox
    NODE->>APP: Notify: stored messages
    APP->>NODE: Write: "/scan"
    Note over NODE: Queue → handleSerialLine
    NODE->>APP: Notify: "SCAN gesendet..."
    NODE->>APP: Notify: "DISCOVERED ..."

    APP->>NODE: Write: "/msg 0xBEEF Hello"
    NODE->>APP: Notify: "TX to=0xBEEF ..."

    Note over APP: Disconnect
    APP--xNODE: Disconnect
    Note over NODE: bleConnected = false<br/>Restart advertising
    Note over NODE: Incoming msgs →<br/>stored in offline inbox
    Note over NODE: 3 min later →<br/>stop advertising<br/>(BOOT button to restart)
```

---

## 9. Reliable Delivery (ACK + Retry)

```mermaid
flowchart TD
    SEND["sendTextTo(dest, text)"] --> IS_DIR{"Directed &<br/>not control msg?"}
    IS_DIR -->|"No"| DONE["Done (fire-and-forget)"]
    IS_DIR -->|"Yes"| BUFFER["Buffer in outbound<br/>(8 slots)"]
    BUFFER --> WAIT["Wait 5s"]
    WAIT --> GOT_ACK{"ACK received?"}
    GOT_ACK -->|"Yes"| REMOVE["Remove from buffer"]
    GOT_ACK -->|"No"| RETRY{"retries < 10?"}
    RETRY -->|"Yes"| RESEND["Re-transmit"] --> WAIT
    RETRY -->|"No"| DROP["Drop message<br/>(delivery failed)"]
```

---

## 10. Control Message Reference

| Message            | Direction | Format                                       | Purpose                              |
| ------------------ | --------- | -------------------------------------------- | ------------------------------------ |
| `#MESH_DISC_REQ`   | Broadcast | `#MESH_DISC_REQ`                             | Station discovery scan               |
| `#MESH_DISC_RESP`  | Broadcast | `#MESH_DISC_RESP:0xNODE:0xREQUESTER`         | Discovery response (all nodes learn) |
| `#MESH_ACK`        | Directed  | `#MESH_ACK:originHex:msgId`                  | Delivery acknowledgement             |
| `#MESH_TRACE_REQ`  | Directed  | `#MESH_TRACE_REQ:0xOrigin[>0xRelay...]`      | Trace route request (path appended)  |
| `#MESH_TRACE_RESP` | Directed  | `#MESH_TRACE_RESP:0xA>0xB>...>0xDest`        | Trace route result                   |
| `#MESH_WX_REQ`     | Any       | `#MESH_WX_REQ`                               | Weather data request                 |
| `#MESH_WX_DATA`    | Directed  | `#MESH_WX_DATA:node=0x...,tempC=...,hum=...` | Weather data response                |

---

## 11. V3 vs V4 Hardware Differences

| Feature              | Heltec V3      | Heltec V4          |
| -------------------- | -------------- | ------------------ |
| MCU                  | ESP32-S3       | ESP32-S3           |
| LoRa Chip            | SX1262         | SX1262             |
| PA (Power Amplifier) | None           | GC1109 (+27 dBm)   |
| Max TX Power         | +22 dBm        | +27.7 dBm (via PA) |
| USB                  | Native CDC     | Native CDC         |
| UART Bridge          | yes            | No                 |
| NVS Persistence      | No (RAM only)  | Yes (flash-backed) |
| OLED                 | SSD1306 128x64 | SSD1306 128x64     |
| Battery ADC          | GPIO1 + div    | GPIO1 + div        |
| BOOT Button          | GPIO0          | GPIO0              |
