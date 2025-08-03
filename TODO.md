# ESP32 Gate Controller - Implementation Plan

## Architecture Decision: Arduino Core + Dual Tasks
- **Arduino Core Benefits**: Library compatibility, easier debugging, Arduino watchdog integration
- **Dual Tasks**: `xTaskCreatePinnedToCore()` provides Arduino Core dual-task benefits
- **HTTP Input Validation**: Numeric validation, fixed buffer sizes, bounds checking, no dynamic allocation
- **Watchdog Timeout**: 30 seconds for proper TLS handshake completion
- **Timestamp Logic**: Client-provided timestamps stored as int, age-based duplicate checking

## File Structure (Safety-Critical Design)

**Decision: Minimal files for gate safety**
- **main.cpp**: All networking, HTTP, MQTT logic (single-file debugging)
- **safety.cpp**: GPIO control, watchdog, relay timing only
- **config.h**: Credentials, pins, safety timeouts
- **safety.h**: Hardware function declarations

**Rationale**: Gate malfunction = safety hazard. Fewer files = easier emergency debugging.

## Implementation Strategy

### Phase 0: Foundation (WiFi + Basic GPIO)
**Goal**: Establish reliable WiFi connection and relay control

```cpp
// Critical placeholders
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"; 
const IPAddress STATIC_IP(192, 168, 1, 100);
const IPAddress GATEWAY(192, 168, 1, 1);
```

**Tasks**:
1. WiFi client with static IP configuration
2. GPIO 16 relay control (HIGH→LOW pulse, 300ms)
3. GPIO 23 status LED for visual feedback  
4. Serial logging at 115200 baud

### Phase 1: Arduino Core Dual Tasks
**Goal**: Leverage Arduino Core + `xTaskCreatePinnedToCore()` for simplicity

**Task Structure**:
```cpp
void setup() {
  xTaskCreatePinnedToCore(httpTask, "HTTP", 8192, NULL, 1, NULL, 0);  // Core 0
  xTaskCreatePinnedToCore(mqttTask, "MQTT", 8192, NULL, 1, NULL, 1);  // Core 1
}
```

**Benefits**: Arduino library compatibility, easier debugging, built-in watchdog support

### Phase 2: HTTP Server with Input Validation
**Goal**: Crash-resistant local network gate control

**Endpoints with Validation**:
- `POST /trigger` - Validate raw timestamp string (numeric, max 32 chars)
- `GET /status` - Returns "1" or "0", minimal validation needed
- `GET /lastTrigger` - Return stored timestamp as raw string

**Input Validation Requirements**:
```cpp
// Fixed-size buffer: char timestampBuffer[33]; // 32 + null terminator
// strncpy() with explicit null termination
// Check timestamp string is numeric (isdigit check)
// Validate length <= 32 characters before processing
// No malloc/free - stack allocation only
// Return plain text errors: "Invalid timestamp format", "Duplicate request"
```

### Phase 3: MQTT Client Arduino Task
**Goal**: Remote access via secure MQTT broker

```cpp
// MQTT Configuration  
const char* MQTT_BROKER = "mqtt.rannow.de";
const int MQTT_PORT = 8883; // TLS
const char* MQTT_USER = "YOUR_MQTT_USER";
const char* MQTT_PASSWORD = "YOUR_MQTT_PASSWORD";
```

**Topics**:
- Subscribe: `/house/iot/gate/esp32/trigger` (raw timestamp strings)
- Publish: `/house/iot/gate/esp32/status` (1/0 values)
- Message format: raw timestamp strings (consistent with HTTP API)

### Phase 4: Client Timestamp Management
**Goal**: Simple duplicate prevention using client timestamps

**Implementation**:
```cpp
unsigned long lastTriggerTimestamp = 0;
const unsigned long MAX_AGE_SECONDS = 300; // 5 minutes

bool parseTimestamp(const char* input, unsigned long& result) {
    // Fixed buffer, bounds checking
    char buffer[33]; // 32 + null terminator
    strncpy(buffer, input, 32);
    buffer[32] = '\0'; // Explicit null termination
    
    // Validate numeric only
    for (int i = 0; buffer[i] != '\0'; i++) {
        if (!isdigit(buffer[i])) return false;
    }
    
    // Safe strtoul with endptr check
    char* endptr;
    result = strtoul(buffer, &endptr, 10);
    
    // Ensure entire string was consumed
    return (*endptr == '\0');
}

bool isValidTrigger(unsigned long clientTimestamp) {
    unsigned long age = abs(clientTimestamp - lastTriggerTimestamp);
    if (age < 5) return false; // Too recent, likely duplicate
    lastTriggerTimestamp = clientTimestamp;
    return true;
}
```

**Logic**: Store client timestamp, reject if too close to previous (5 second window)

### Phase 5: Hardware Watchdog (30s timeout)
**Goal**: Automatic recovery suitable for TLS operations

**Configuration**:
- Watchdog timeout: 30 seconds (allows TLS handshake completion)
- All tasks must reset watchdog every 15 seconds
- Automatic ESP32 reset if any task hangs

**Arduino Watchdog Integration**: Built-in watchdog monitors task health

## Build & Test Plan

### Local Testing
```bash
~/.platformio/penv/bin/pio run
~/.platformio/penv/bin/pio upload --upload-port /dev/cu.usbserial-A5069RR4
~/.platformio/penv/bin/pio device monitor --baud 115200
```

### Integration Tests
1. HTTP trigger with valid/invalid timestamp strings (numeric validation)
2. MQTT trigger from mosquitto_pub
3. Malformed HTTP requests (crash testing)
4. Arduino Core task monitoring via Serial output
5. Watchdog timeout simulation (30s)

## Arduino Core Task Priorities

**Priority 3 (Highest)**: Watchdog monitoring
**Priority 2 (Normal)**: HTTP Server, MQTT Client  
**Priority 1 (Low)**: WiFi monitoring, status updates

## Phase 6: Auto-Reconnect Logic (Future Enhancement)
**Goal**: Robust connection recovery with minimal resource usage

**WiFi Auto-Reconnect**:
```cpp
// In main task loop
if (WiFi.status() != WL_CONNECTED) {
    unsigned long backoffDelay = min(60000, 1000 * pow(2, wifiRetryCount));
    delay(backoffDelay);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifiRetryCount++;
}
```

**MQTT Auto-Reconnect**:
```cpp
// In MQTT task loop
if (!mqttClient.connected()) {
    unsigned long backoffDelay = min(60000, 1000 * pow(2, mqttRetryCount));
    delay(backoffDelay);
    mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
    mqttRetryCount++;
}
```

**Connection Health Checks**:
- Check WiFi status every 30 seconds
- Check MQTT connection every 60 seconds
- Reset retry counters on successful connection
- Max backoff: 60 seconds

## Progress Status

### Phase 0: Foundation - COMPLETED ✅
- [x] Created include/config.h with credential placeholders and pin definitions
- [x] Created include/safety.h and src/safety.cpp for GPIO control isolation  
- [x] Replaced main.cpp with WiFi client and momentary 200ms relay pulse
- [x] Implemented 30s watchdog with proper reset calls
- [x] Physical button debouncing with 50ms timing
- [x] Static IP configuration with placeholders

### Phase 1: Arduino Core Dual Tasks - COMPLETED ✅
- [x] Implement xTaskCreatePinnedToCore for HTTP (Core 0) and MQTT (Core 1)
- [x] Move WiFi and button logic to httpTask on Core 0
- [x] Add task-specific watchdog reset calls in both tasks

### Phase 2: HTTP Server - COMPLETED ✅
- [x] HTTP server on Core 0 with input validation
- [x] POST /trigger endpoint with timestamp validation (numeric check, 32 char limit)
- [x] GET /status and GET /lastTrigger endpoints
- [x] Fixed-size buffer validation with bounds checking
- [x] Duplicate prevention using 5-second window

### Phase 3: MQTT Client - COMPLETED ✅
- [x] MQTT client on Core 1 with TLS connection (port 8883)
- [x] Subscribe to iot/house/gate/esp32/trigger topic
- [x] Publish to iot/house/gate/esp32/status topic
- [x] Same timestamp validation as HTTP API
- [x] Let's Encrypt ISRG Root X1 certificate validation
- [x] NTP time synchronization for certificate validation

### Phase 4: Network Resilience - COMPLETED ✅
- [x] WiFi auto-reconnect with exponential backoff (30s monitoring)
- [x] MQTT auto-reconnect with exponential backoff (60s monitoring)
- [x] Cascading recovery (WiFi down → MQTT disconnect → WiFi up → MQTT reconnect)
- [x] Smart backoff delays (5s → 300s max for WiFi, 10s → 300s max for MQTT)
- [x] Connection timeout protection (15s WiFi, no MQTT hanging)
- [x] Watchdog protection during all reconnection attempts

## Implementation Complete - All Core Phases Done ✅

### Core Features Implemented:
- ✅ **Phase 0**: WiFi client, static IP, 300ms relay pulse, 30s watchdog
- ✅ **Phase 1**: Arduino Core dual tasks (HTTP on Core 0, MQTT on Core 1)  
- ✅ **Phase 2**: HTTP Server with input validation and crash prevention
- ✅ **Phase 3**: MQTT Client with TLS certificate validation
- ✅ **Phase 4**: Network resilience with robust auto-reconnect

### Ready for Deployment

## Deployment Checklist

- [x] Update all credential placeholders (configured in main.cpp)
- [x] Verify static IP doesn't conflict with network (192.168.1.210)
- [x] Test relay pulse duration (300ms implemented)
- [x] Confirm MQTT broker connectivity with Let's Encrypt certificate validation
- [x] Validate HTTP input parsing handles malformed timestamp strings (numeric check, length limits, overflow protection)
- [x] Test Arduino Core task stability under load
- [x] Physical button still works (GPIO 0)
- [x] Implement robust WiFi auto-reconnect with exponential backoff
- [x] Implement robust MQTT auto-reconnect with exponential backoff
- [x] Fix timestamp overflow deadlock (13-digit milliseconds → 10-digit seconds)
- [x] Add CORS headers for PWA compatibility
- [x] Test end-to-end HTTP and MQTT triggers
- [x] Wire relay to NO + COM terminals for momentary gate trigger

## Non-Functional Requirements

**Performance**: 
- HTTP response < 500ms
- MQTT publish within 1s of trigger
- Arduino Core context switching overhead < 1ms

**Reliability**:
- Input validation prevents crashes from malformed requests
- 30-second watchdog allows TLS handshake completion
- Arduino Core handles task scheduling and resource management

**Security**:
- TLS for MQTT (homebrew project, local trust model)
- Local HTTP only (no external exposure)
- Memory safety: fixed buffers, bounds checking, no dynamic allocation
- Input validation as primary defense (numeric check, length limits, buffer overflow prevention)

---

*Arduino Core provides robust task management via underlying FreeRTOS, eliminating manual core coordination complexity while maintaining real-time performance guarantees.*