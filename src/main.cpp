#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "config.h"
#include "safety.h"

// Configuration variable definitions
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const IPAddress STATIC_IP(192, 168, 1, 210);
const IPAddress GATEWAY(192, 168, 1, 1);
const IPAddress SUBNET(255, 255, 255, 0);
const IPAddress DNS_SERVER(1, 1, 1, 1);

const char* MQTT_BROKER = "mqtt.rannow.de";
const int MQTT_PORT = 8883;
const char* MQTT_USER = "YOUR_MQTT_USER";
const char* MQTT_PASSWORD = "YOUR_MQTT_PASSWORD";
const char* MQTT_CLIENT_ID = "esp32_gate_controller";

const char* MQTT_TRIGGER_TOPIC = "/house/iot/gate/esp32/trigger";
const char* MQTT_STATUS_TOPIC = "/house/iot/gate/esp32/status";

WebServer server(80);
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

bool parseTimestamp(const char* input, unsigned long& result) {
    if (!input) return false;
    
    char buffer[33];
    strncpy(buffer, input, 32);
    buffer[32] = '\0';
    
    size_t len = strlen(buffer);
    if (len == 0 || len > 32) return false;
    
    for (size_t i = 0; i < len; i++) {
        if (!isdigit(buffer[i])) return false;
    }
    
    char* endptr;
    result = strtoul(buffer, &endptr, 10);
    
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

void httpTask(void* parameter) {
    Serial.println("HTTP Task started on Core 0");
    
    server.on("/trigger", handleTrigger);
    server.on("/status", handleStatus);
    server.on("/lastTrigger", handleLastTrigger);
    server.begin();
    
    Serial.println("HTTP Server started on port 80");
    
    while (true) {
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

void mqttTask(void* parameter) {
    Serial.println("MQTT Task started on Core 1");
    
    wifiClient.setInsecure();
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(onMqttMessage);
    
    while (true) {
        if (!mqttClient.connected()) {
            Serial.print("Connecting to MQTT broker...");
            if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
                Serial.println(" connected");
                mqttClient.subscribe(MQTT_TRIGGER_TOPIC);
                Serial.print("Subscribed to: ");
                Serial.println(MQTT_TRIGGER_TOPIC);
            } else {
                Serial.print(" failed, rc=");
                Serial.print(mqttClient.state());
                Serial.println(" retrying in 5 seconds");
                delay(5000);
            }
        }
        
        mqttClient.loop();
        resetWatchdog();
        delay(100);
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
    //xTaskCreatePinnedToCore(mqttTask, "MQTT", 8192, NULL, 1, NULL, 1);
    
    Serial.println("Phase 1 dual tasks started");
}

void loop() {
    resetWatchdog();
    delay(5000);
}