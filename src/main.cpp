#include <Arduino.h>

constexpr uint8_t BTN_PIN = 0;
constexpr uint8_t LED_PIN = 23;
constexpr uint8_t RELAY_PIN = 16;

bool relayState = false;

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  Serial.println("Ready – release button to toggle");
}

void loop() {
  static uint8_t btnState = HIGH;
  static uint8_t lastDebouncedState = HIGH;  // Track debounced state
  static uint32_t lastChange = 0;
  
  uint8_t current = digitalRead(BTN_PIN);
  
  if (current != btnState) {
    lastChange = millis();
    btnState = current;
  }
  
  // Check if state has been stable long enough
  if (millis() - lastChange > 50) {
    // Only trigger on transition from LOW to HIGH
    if (lastDebouncedState == LOW && btnState == HIGH) {
      relayState = !relayState;
      digitalWrite(LED_PIN, relayState);
      digitalWrite(RELAY_PIN, relayState);
      Serial.println(relayState ? "ON" : "OFF");
    }
    lastDebouncedState = btnState;  // Update debounced state
  }
  
  delay(10);  // Much shorter delay
}