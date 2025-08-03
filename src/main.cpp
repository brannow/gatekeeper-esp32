#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "config.h"
#include "safety.h"

WebServer server(80);

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
    if (!parseTimestamp(body.c_str(), &timestamp)) {
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
    server.send(200, "text/plain", relayActive ? "1" : "0");
}

void handleLastTrigger() {
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

void mqttTask(void* parameter) {
    Serial.println("MQTT Task started on Core 1");
    
    while (true) {
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