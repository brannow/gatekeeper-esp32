#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "safety.h"

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
    
    Serial.println("Phase 0 foundation ready");
}

void loop() {
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