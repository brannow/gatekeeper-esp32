#ifndef CONFIG_H
#define CONFIG_H

#include <IPAddress.h>

// GPIO Pin Definitions
constexpr uint8_t RELAY_PIN = 16;
constexpr uint8_t LED_PIN = 23;
constexpr uint8_t BUTTON_PIN = 0;

// WiFi Configuration
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Static IP Configuration
const IPAddress STATIC_IP(192, 168, 1, 100);
const IPAddress GATEWAY(192, 168, 1, 1);
const IPAddress SUBNET(255, 255, 255, 0);
const IPAddress DNS_SERVER(8, 8, 8, 8);

// MQTT Configuration
const char* MQTT_BROKER = "mqtt.rannow.de";
const int MQTT_PORT = 8883;
const char* MQTT_USER = "YOUR_MQTT_USER";
const char* MQTT_PASSWORD = "YOUR_MQTT_PASSWORD";
const char* MQTT_CLIENT_ID = "esp32_gate_controller";

// MQTT Topics
const char* MQTT_TRIGGER_TOPIC = "/house/iot/gate/esp32/trigger";
const char* MQTT_STATUS_TOPIC = "/house/iot/gate/esp32/status";

// Safety Timeouts
constexpr unsigned long RELAY_PULSE_DURATION_MS = 200;
constexpr unsigned long DUPLICATE_WINDOW_SECONDS = 5;
constexpr unsigned long WATCHDOG_TIMEOUT_SECONDS = 30;

#endif