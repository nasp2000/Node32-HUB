// telnet_server.h
// Telnet server declarations for ESP32.
// Global variables and function prototypes; implementation in telnet_server.cpp.

#ifndef CHATGPT_SRC_TELNET_SERVER_H_
#define CHATGPT_SRC_TELNET_SERVER_H_

#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "feature_flags.h"
#include "telnet_transport.h"

#if ENABLE_ETHERNET==1
#include <Ethernet.h>
#endif

#if ENABLE_TELNET==1
extern String telnetUser;
extern String telnetPass;
extern const int TELNET_PORT;
extern WiFiServer* telnetWifiServer;
#if ENABLE_ETHERNET==1
extern EthernetServer* telnetEthernetServer;
#endif
extern UnifiedTelnetClient telnetClient;
extern bool telnetAuthenticated;
extern String telnetInputBuffer;
extern unsigned long telnetAuthTimeout;
extern const unsigned long TELNET_AUTH_TIMEOUT_MS;
extern int telnetAuthState;
extern bool wasTelnetClientConnected;
extern unsigned long lastTelnetActivityMillis;
extern bool telnetCompactMode;

bool startTelnetServer();
void stopTelnetServer();
void sendTelnet(const String &msg);
void handleTelnetClient(unsigned long currentMillis);
void syncTelnetNetworkBindings();

#else // ENABLE_TELNET==0

extern UnifiedTelnetClient telnetClient;
extern bool telnetAuthenticated;

static inline bool startTelnetServer() { return false; }
static inline void sendTelnet(const String &msg) { (void)msg; }
static inline void handleTelnetClient(unsigned long currentMillis) { (void)currentMillis; }
static inline void stopTelnetServer() {}
static inline void syncTelnetNetworkBindings() {}

#endif // ENABLE_TELNET

#endif // CHATGPT_SRC_TELNET_SERVER_H_