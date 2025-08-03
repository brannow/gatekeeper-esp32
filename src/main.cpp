#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "config.h"
#include "safety.h"

// Configuration variable definitions
const char* WIFI_SSID = "WIFI_SSID";
const char* WIFI_PASSWORD = "WIFI_PW";

const IPAddress STATIC_IP(192, 168, 1, 210);
const IPAddress GATEWAY(192, 168, 1, 1);
const IPAddress SUBNET(255, 255, 255, 0);
const IPAddress DNS_SERVER(1, 1, 1, 1);

const char* MQTT_BROKER = "BROKER_DOMAIN_OR_IP";
const int MQTT_PORT = 8883;
const char* MQTT_USER = "USER";
const char* MQTT_PASSWORD = "PASSWORD";
const char* MQTT_CLIENT_ID = "esp32_gate_controller";

const char* MQTT_TRIGGER_TOPIC = "iot/house/gate/esp32/trigger";
const char* MQTT_STATUS_TOPIC = "iot/house/gate/esp32/status";

WebServer server(80);
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

// Connection monitoring variables
unsigned long lastWiFiCheck = 0;
unsigned long lastMqttCheck = 0;
int wifiRetryCount = 0;
int mqttRetryCount = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000;  // 30 seconds
const unsigned long MQTT_CHECK_INTERVAL = 60000;  // 60 seconds
const unsigned long MAX_BACKOFF_DELAY = 300000;   // 5 minutes max

bool parseTimestamp(const char* input, unsigned long& result) {
    if (!input) return false;
    
    char buffer[33];
    strncpy(buffer, input, 32);
    buffer[32] = '\0';
    
    size_t len = strlen(buffer);
    if (len == 0 || len > 32) return false;
    
    // Validate numeric only
    for (size_t i = 0; i < len; i++) {
        if (!isdigit(buffer[i])) return false;
    }
    
    // Handle milliseconds: truncate to 10 digits if 13+ digits
    if (len >= 13) {
        buffer[10] = '\0';
        len = 10;
    }
    
    char* endptr;
    unsigned long long temp = strtoull(buffer, &endptr, 10);
    
    // Overflow detection after truncation
    if (temp > ULONG_MAX) {
        return false; // Still too large even after truncation
    }
    
    result = (unsigned long)temp;
    return (*endptr == '\0');
}

bool isValidTrigger(unsigned long clientTimestamp) {
    unsigned long age = abs((long)(clientTimestamp - lastTriggerTimestamp));
    if (age < DUPLICATE_WINDOW_SECONDS) return false;
    lastTriggerTimestamp = clientTimestamp;
    return true;
}

void handleTrigger() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    
    if (server.method() == HTTP_OPTIONS) {
        server.send(200, "text/plain", "");
        return;
    }
    
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    
    String body = server.arg("plain");
    if (body.length() == 0) {
        server.send(400, "text/plain", "Invalid timestamp format");
        return;
    }
    
    unsigned long timestamp;
    if (!parseTimestamp(body.c_str(), timestamp)) {
        server.send(400, "text/plain", "Invalid timestamp format");
        return;
    }
    
    if (!isValidTrigger(timestamp)) {
        server.send(429, "text/plain", "Duplicate request");
        return;
    }
    
    triggerRelayPulse();
    server.send(200, "text/plain", "OK");
    Serial.print("HTTP trigger - timestamp: ");
    Serial.println(timestamp);
}

void handleStatus() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", relayActive ? "1" : "0");
}

void handleLastTrigger() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", String(lastTriggerTimestamp));
}

void checkWiFiConnection() {
    unsigned long now = millis();
    
    if (now - lastWiFiCheck < WIFI_CHECK_INTERVAL) return;
    lastWiFiCheck = now;
    
    if (WiFi.status() == WL_CONNECTED) {
        if (wifiRetryCount > 0) {
            Serial.println("WiFi reconnected successfully");
            wifiRetryCount = 0; // Reset retry count on success
        }
        return;
    }
    
    // WiFi disconnected - attempt reconnection with exponential backoff
    unsigned long backoffDelay = min(MAX_BACKOFF_DELAY, (unsigned long)(5000 * pow(2, min(wifiRetryCount, 6))));
    
    Serial.printf("WiFi disconnected. Retry %d, waiting %lu ms\n", wifiRetryCount + 1, backoffDelay);
    
    delay(backoffDelay);
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    // Wait for connection attempt (max 15 seconds)
    unsigned long connectStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - connectStart) < 15000) {
        delay(500);
        resetWatchdog();
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("WiFi reconnected - IP: ");
        Serial.println(WiFi.localIP());
        wifiRetryCount = 0;
    } else {
        wifiRetryCount++;
        Serial.printf("WiFi reconnection failed (attempt %d)\n", wifiRetryCount);
    }
}

void httpTask(void* parameter) {
    Serial.println("HTTP Task started on Core 0");
    
    server.on("/trigger", handleTrigger);
    server.on("/status", handleStatus);
    server.on("/lastTrigger", handleLastTrigger);
    server.begin();
    
    Serial.println("HTTP Server started on port 80");
    
    while (true) {
        // WiFi connection monitoring
        checkWiFiConnection();
        
        server.handleClient();
        
        static unsigned long lastButtonCheck = 0;
        static bool lastButtonState = false;
        
        unsigned long now = millis();
        
        if (now - lastButtonCheck > 50) {
            bool currentButtonState = readPhysicalButton();
            
            if (!lastButtonState && currentButtonState) {
                Serial.println("Physical button pressed - triggering relay");
                triggerRelayPulse();
                lastTriggerTimestamp = now;
            }
            
            lastButtonState = currentButtonState;
            lastButtonCheck = now;
        }
        
        resetWatchdog();
        delay(10);
    }
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic, MQTT_TRIGGER_TOPIC) != 0) return;
    
    char buffer[33];
    size_t copyLen = min((size_t)length, (size_t)32);
    strncpy(buffer, (char*)payload, copyLen);
    buffer[copyLen] = '\0';
    
    unsigned long timestamp;
    if (!parseTimestamp(buffer, timestamp)) {
        Serial.println("MQTT: Invalid timestamp format");
        return;
    }
    
    if (!isValidTrigger(timestamp)) {
        Serial.println("MQTT: Duplicate request");
        return;
    }
    
    triggerRelayPulse();
    Serial.print("MQTT trigger - timestamp: ");
    Serial.println(timestamp);
    
    mqttClient.publish(MQTT_STATUS_TOPIC, relayActive ? "1" : "0");
}

void checkMqttConnection() {
    unsigned long now = millis();
    
    if (now - lastMqttCheck < MQTT_CHECK_INTERVAL) return;
    lastMqttCheck = now;
    
    // Don't attempt MQTT if WiFi is down
    if (WiFi.status() != WL_CONNECTED) {
        if (mqttClient.connected()) {
            Serial.println("WiFi down - disconnecting MQTT");
            mqttClient.disconnect();
        }
        return;
    }
    
    if (mqttClient.connected()) {
        if (mqttRetryCount > 0) {
            Serial.println("MQTT reconnected successfully");
            mqttRetryCount = 0; // Reset retry count on success
        }
        return;
    }
    
    // MQTT disconnected - attempt reconnection with exponential backoff
    unsigned long backoffDelay = min(MAX_BACKOFF_DELAY, (unsigned long)(10000 * pow(2, min(mqttRetryCount, 5))));
    
    Serial.printf("MQTT disconnected. Retry %d, waiting %lu ms\n", mqttRetryCount + 1, backoffDelay);
    
    delay(backoffDelay);
    
    Serial.print("Connecting to MQTT broker...");
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
        Serial.println(" connected");
        mqttClient.subscribe(MQTT_TRIGGER_TOPIC);
        Serial.print("Subscribed to: ");
        Serial.println(MQTT_TRIGGER_TOPIC);
        mqttRetryCount = 0;
    } else {
        mqttRetryCount++;
        Serial.printf(" failed, rc=%d (attempt %d)\n", mqttClient.state(), mqttRetryCount);
    }
}

void mqttTask(void* parameter) {
    Serial.println("MQTT Task started on Core 1");
    
    // Sync time for certificate validation
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.print("Waiting for NTP time sync: ");
    time_t now = time(nullptr);
    while (now < 8 * 3600 * 2) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }
    Serial.println();
    Serial.printf("Current time: %s", ctime(&now));
    
    // Let's Encrypt ISRG Root X1 certificate
    const char* isrg_root_x1 = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)EOF";
    
    wifiClient.setCACert(isrg_root_x1);
    
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(onMqttMessage);
    mqttClient.setKeepAlive(60); // Match MQTTX keepalive
    
    // Initial MQTT connection
    if (WiFi.status() == WL_CONNECTED) {
        checkMqttConnection();
    }
    
    while (true) {
        // MQTT connection monitoring and auto-reconnect
        checkMqttConnection();
        
        if (mqttClient.connected()) {
            mqttClient.loop();
        }
        
        resetWatchdog();
        delay(1000);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Gate Controller starting...");
    
    initializeGPIO();
    initializeWatchdog();
    
    WiFi.mode(WIFI_STA);
    WiFi.config(STATIC_IP, GATEWAY, SUBNET, DNS_SERVER);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
        resetWatchdog();
    }
    
    Serial.println();
    Serial.print("WiFi connected - IP: ");
    Serial.println(WiFi.localIP());
    
    xTaskCreatePinnedToCore(httpTask, "HTTP", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(mqttTask, "MQTT", 8192, NULL, 1, NULL, 1);
    
    Serial.println("Phase 1 dual tasks started");
}

void loop() {
    resetWatchdog();
    delay(5000);
}
