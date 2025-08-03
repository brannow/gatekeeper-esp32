#ifndef CONFIG_H
#define CONFIG_H

#include <IPAddress.h>

// GPIO Pin Definitions
constexpr uint8_t RELAY_PIN = 16;
constexpr uint8_t LED_PIN = 23;
constexpr uint8_t BUTTON_PIN = 0;

// WiFi Configuration
extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;

// Static IP Configuration
extern const IPAddress STATIC_IP;
extern const IPAddress GATEWAY;
extern const IPAddress SUBNET;
extern const IPAddress DNS_SERVER;

// MQTT Configuration
extern const char* MQTT_BROKER;
extern const int MQTT_PORT;
extern const char* MQTT_USER;
extern const char* MQTT_PASSWORD;
extern const char* MQTT_CLIENT_ID;

// MQTT Topics
extern const char* MQTT_TRIGGER_TOPIC;
extern const char* MQTT_STATUS_TOPIC;

// Safety Timeouts
constexpr unsigned long RELAY_PULSE_DURATION_MS = 200;
constexpr unsigned long DUPLICATE_WINDOW_SECONDS = 5;
constexpr unsigned long WATCHDOG_TIMEOUT_SECONDS = 30;

#endif