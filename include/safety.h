#ifndef SAFETY_H
#define SAFETY_H

#include <Arduino.h>

// GPIO Hardware Control Functions
void initializeGPIO();
void triggerRelayPulse();
void setStatusLED(bool state);
bool readPhysicalButton();

// Watchdog Functions
void initializeWatchdog();
void resetWatchdog();

// Safety State
extern bool relayActive;
extern unsigned long lastTriggerTimestamp;

#endif