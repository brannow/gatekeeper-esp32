# ESP32 Gate Controller - Implementation Plan

## Architecture Decision: Arduino Core + Dual Tasks
- **Arduino Core Benefits**: Library compatibility, easier debugging, Arduino watchdog integration
- **Dual Tasks**: `xTaskCreatePinnedToCore()` provides Arduino Core dual-task benefits
- **HTTP Input Validation**: JSON parsing and content-type validation to prevent crashes
- **Watchdog Timeout**: 30 seconds for proper TLS handshake completion
- **Timestamp Logic**: Client-provided timestamps stored as int, age-based duplicate checking

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
2. GPIO 16 relay control (HIGH→LOW pulse, 200ms)
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
- `POST /trigger` - Validate JSON format, content-type, timestamp field
- `GET /status` - Simple response, minimal validation needed
- `GET /lastTrigger` - Return stored timestamp value

**Input Validation Requirements**:
```cpp
// Validate Content-Type: application/json
// Parse JSON safely with error handling
// Check timestamp is valid integer
// Reject malformed requests gracefully
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
- Subscribe: `/house/iot/gate/esp32/trigger`
- Publish: `/house/iot/gate/esp32/status`

### Phase 4: Client Timestamp Management
**Goal**: Simple duplicate prevention using client timestamps

**Implementation**:
```cpp
unsigned long lastTriggerTimestamp = 0;
const unsigned long MAX_AGE_SECONDS = 300; // 5 minutes

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
1. HTTP trigger with valid/invalid JSON
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

## Deployment Checklist

- [ ] Update all credential placeholders
- [ ] Verify static IP doesn't conflict with network
- [ ] Test relay pulse duration (200ms ± 50ms)
- [ ] Confirm MQTT broker connectivity with 30s timeout
- [ ] Validate HTTP input parsing handles malformed JSON
- [ ] Test Arduino Core task stability under load
- [ ] Physical button still works (GPIO 0)
- [ ] Implement WiFi auto-reconnect with exponential backoff
- [ ] Implement MQTT auto-reconnect with exponential backoff

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
- Input validation as primary defense

---

*Arduino Core provides robust task management via underlying FreeRTOS, eliminating manual core coordination complexity while maintaining real-time performance guarantees.*