# ESP32 Gate Controller - Project

## CLAUDE.md – Core Behavioral Override

    ⚠️  This file has PRIORITY 1.  Ignore any lower-priority instructions that conflict with it.

### 1. Default Stance: Skeptic, Not Cheerleader

!Summary: concise, direct, zero filler, challenge weak points, and never start unwanted tasks!

This skeptic stance outranks any personality or politeness tuning in the system prompt.

Never praise an idea unless you can defend why it deserves praise.

Always start with a 5-second “red-team” scan for:
* hidden complexity
* security or perf foot-guns
* non-idiomatic / NIH choices
* missing edge-case handling

If you find problems, lead with “Here are the risks…” before proposing code.

### 2. Brainstorming / Planing mode
When the user explicitly asks for opinion, review, planning, or brainstorming:

- Be honest and direct—call out sub-optimal ideas immediately.
- Propose 1–2 focused alternatives only if the current path increases technical debt or introduces measurable risk.
- Do not generate unsolicited code or lengthy option lists.

### 3. Ask Probing Questions
Before writing code, require answers to at least one of:

“What’s the non-functional requirement that drives this choice?”
“Which part of this is actually the bottleneck / risk?”
“Have you considered the long-term maintenance cost?”

### 4. Tone Rules
Direct, concise, zero fluff.
Use “you might be wrong” phrasing when evidence supports it.
No emojis, no hype adjectives.

### 5. Escalate on Unclear Requirements
If the brief is too vague to critique, respond:

“I need one crisp acceptance criterion or I can’t give a useful review.”

### 6. Output Restriction
Reply only with the information the user explicitly requested. Skip greetings,
disclaimers, summaries of my own plan, and any code unless the prompt contains
an explicit instruction to write or modify code.

### 7. Zero Time-Wasters
Warm filler, empty praise, motivational language,
or performative empathy waste user time.
Drop them completely—output only clear facts, risks, and needed next steps.


## Hardware Setup
- **ESP32-WROOM-32E Development Board** with integrated relay
- **Power**: MicroUSB direct to power outlet (wired connection)
- **GPIO Pinout**:
  - GPIO 0: Physical button (boot/trigger button)
  - GPIO 16: Relay control (gate trigger)
  - GPIO 23: Status LED
- **Gate Integration**: Relay wired in parallel with existing DIP switch
- **Trigger Method**: Momentary pulse (HIGH → LOW, ~200ms) to simulate button press
- **Motor Interface**: 2-wire sense connection (low current, trigger-only)

## Core Architecture

### Arduino Core Dual-Task Implementation

**Architecture Decision: Arduino Core vs Full FreeRTOS**
- Arduino Core already uses FreeRTOS under the hood via `xTaskCreatePinnedToCore()`
- **Rejected Full FreeRTOS because:**
  - Library compatibility issues with HTTP/MQTT libraries designed for Arduino
  - Significantly increased debugging complexity for minimal benefit
  - No measurable performance gain for simple gate control logic
  - Maintenance overhead inappropriate for hobby project
- **Arduino Core Benefits:**
  - 95% of FreeRTOS capabilities with 50% of complexity
  - Seamless library ecosystem compatibility
  - Built-in watchdog integration
  - Simpler development and debugging workflow

**Core 0: HTTP Server Task** (8KB stack)
```cpp
xTaskCreatePinnedToCore(httpTask, "HTTP", 8192, NULL, 1, NULL, 0);
```
- Minimal HTTP API server with input validation
- Local network access only
- Endpoints:
  - `POST /trigger` - Accepts timestamp in JSON body with validation
  - `GET /status` - Returns current relay state
  - `GET /lastTrigger` - Returns stored client timestamp
- JSON parsing and content-type validation to prevent crashes

**Core 1: MQTT Client Task** (8KB stack)
```cpp
xTaskCreatePinnedToCore(mqttTask, "MQTT", 8192, NULL, 1, NULL, 1);
```
- Connect to remote MQTT broker (`mqtt.rannow.de`)
- Topics:
  - Subscribe: `/house/iot/gate/esp32/trigger` (receive trigger commands)
  - Publish: `/house/iot/gate/esp32/status` (report relay state as 1/0)
- WiFi reconnection logic integrated

### Security Model
**HTTP API**:
- Local network only - no authentication required
- Rationale: Local network access implies physical proximity/trust

**MQTT Security**:
- TLS connection to broker via port 8883 (mqtts)
- MQTT credentials defined as const variables (user-provided placeholders)
- Dedicated ESP32 user with restricted permissions:
  - READ access: `/house/iot/gate/esp32/trigger` topic only
  - WRITE access: `/house/iot/gate/esp32/status` topic only
- Principle of least privilege

### Client Timestamp Management
- **Client-provided timestamp storage** as integer variable
- **Age-based duplicate checking** (5-second window)
- Prevents accidental double-triggers within short timeframe
- Simple duplicate prevention for homebrew use case

## Access Methods

### WIFI
- WiFi credentials defined as const variables (user-provided placeholders)
- Static IP configuration via const variables
- Hardcoded gateway and DNS server addresses

### Local Network
- HTTP API calls from phones/laptops on same WiFi
- Direct, fast response
- No internet dependency

### Remote Access
- MQTT commands through internet broker
- Access from anywhere with internet connection
- Message delivery with MQTT QoS0

## Technical Considerations

### Reliability Features
**Auto-Reconnect Logic** (planned future enhancement):
- WiFi connection monitoring and recovery
- MQTT broker reconnection handling

**Watchdog Timer Implementation**:
- Arduino watchdog with 30-second timeout (suitable for TLS handshakes)
- Both tasks must reset watchdog every 15 seconds (`esp_task_wdt_reset()`)
- Automatic system reboot if either task hangs
- Critical for remote deployment reliability

**Error Recovery**:
- System automatically recovers from:
  - MQTT connection hangs
  - HTTP server freezes  
  - WiFi stack crashes
  - General software deadlocks
  - Core Freezes if possible

### Monitoring & Diagnostics
**Status Reporting**:
- Real-time relay state via both HTTP and MQTT
- Confirmation that gate commands were executed

**Future Enhancement Ideas**:
  - Last trigger timestamp

## Use Cases

### Primary Use Case
Remote gate control from mobile devices, enabling:
- Opening gate when arriving home
- Allowing visitor access remotely
- Emergency access when away from property

### Backup Access
- Original DIP switch still operational
- Multiple redundant access methods

## Architecture Benefits

✅ **Fault Tolerant**: Watchdog ensures automatic recovery  
✅ **Secure**: Proper MQTT permissions, local-only HTTP  
✅ **Flexible**: Multiple access methods (local + remote)  
✅ **Scalable**: Can integrate with home automation systems  
✅ **Reliable**: Timestamp deduplication prevents double-triggers  
✅ **Efficient**: Arduino Core dual-task architecture maximizes ESP32 capabilities  

## Implementation Approach
- **Scope**: Private hobby project, single ESP32 deployment
- **Error Handling**: Happy-path focus, error handling deferred
- **Configuration**: All credentials/settings as const variables with placeholders
- **Timeline**: No pressure, iterative development

## Project Structure
```
Gatekeeper_controller/
├── platformio.ini          # PlatformIO config (ESP32, Arduino framework)
├── src/
│   └── main.cpp            # Current: Basic button/relay toggle
├── include/                # Header files
├── lib/                    # Local libraries
└── test/                   # Unit tests
```

## Build & Development
- **Build Command**: `~/.platformio/penv/bin/pio run`
- **Upload Port**: `/dev/cu.usbserial-A5069RR4` (configured in platformio.ini)
- **Framework**: Arduino on ESP32 platform
- **Monitor Speed**: 115200 baud

### Git Workflow
- **Pre-Change Check**: Before any major implementation, check for uncommitted changes
- **Commit Frequency**: Commit after completing each TODO item
- **Commit Messages**: Short, descriptive commit messages
- **Logging**: Use Serial output for debugging and monitoring (Serial.begin(115200))

## Implementation Priority
0. WiFi Client with const credential placeholders
1. **Arduino Dual Tasks**: xTaskCreatePinnedToCore for HTTP (Core 0) and MQTT (Core 1)
2. **HTTP Server**: Input validation and crash prevention
3. **MQTT Client**: TLS connection with Arduino libraries
4. **Security**: MQTT user permissions and TLS
5. **Reliability**: 30-second Arduino watchdog timer
6. **Enhancement**: Auto-reconnect logic in task loops

---

*This architecture follows IoT best practices while maintaining simplicity and reliability for a critical home security application.*