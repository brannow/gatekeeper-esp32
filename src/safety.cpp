#include "safety.h"
#include "config.h"
#include <esp_task_wdt.h>

bool relayActive = false;
unsigned long lastTriggerTimestamp = 0;

void initializeGPIO() {
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    
    Serial.println("GPIO initialized - relay OFF, LED OFF");
}

void triggerRelayPulse() {
    relayActive = true;
    digitalWrite(RELAY_PIN, HIGH);
    setStatusLED(true);
    
    Serial.println("Relay pulse START");
    
    delay(RELAY_PULSE_DURATION_MS);
    
    digitalWrite(RELAY_PIN, LOW);
    setStatusLED(false);
    relayActive = false;
    
    Serial.println("Relay pulse END");
}

void setStatusLED(bool state) {
    digitalWrite(LED_PIN, state ? HIGH : LOW);
}

bool readPhysicalButton() {
    return digitalRead(BUTTON_PIN) == LOW;
}

void initializeWatchdog() {
    esp_task_wdt_init(WATCHDOG_TIMEOUT_SECONDS, true);
    esp_task_wdt_add(NULL);
    Serial.println("Watchdog initialized - 30s timeout");
}

void resetWatchdog() {
    esp_task_wdt_reset();
}