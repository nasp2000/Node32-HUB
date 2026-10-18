#ifndef CHATGPT_SRC_TELNET_TRANSPORT_H_
#define CHATGPT_SRC_TELNET_TRANSPORT_H_

#pragma once

#include <Arduino.h>
#include <WiFiClient.h>
#include "feature_flags.h"

#if ENABLE_ETHERNET==1
#include <Ethernet.h>
#endif

class UnifiedTelnetClient {
public:
    enum class Transport : uint8_t {
        None = 0,
        WiFi,
        Ethernet
    };

    UnifiedTelnetClient() = default;

    UnifiedTelnetClient& operator=(const WiFiClient& client) {
        wifiClient_ = client;
        transport_ = Transport::WiFi;
        return *this;
    }

#if ENABLE_ETHERNET==1
    UnifiedTelnetClient& operator=(const EthernetClient& client) {
        ethernetClient_ = client;
        transport_ = Transport::Ethernet;
        return *this;
    }
#endif

    bool connected() {
        switch (transport_) {
            case Transport::WiFi:
                return wifiClient_ && wifiClient_.connected();
#if ENABLE_ETHERNET==1
            case Transport::Ethernet:
                return ethernetClient_ && ethernetClient_.connected();
#endif
            default:
                return false;
        }
    }

    explicit operator bool() {
        return connected();
    }

    void stop() {
        switch (transport_) {
            case Transport::WiFi:
                wifiClient_.stop();
                break;
#if ENABLE_ETHERNET==1
            case Transport::Ethernet:
                ethernetClient_.stop();
                break;
#endif
            default:
                break;
        }
        transport_ = Transport::None;
    }

    int available() {
        switch (transport_) {
            case Transport::WiFi:
                return wifiClient_.available();
#if ENABLE_ETHERNET==1
            case Transport::Ethernet:
                return ethernetClient_.available();
#endif
            default:
                return 0;
        }
    }

    int read() {
        switch (transport_) {
            case Transport::WiFi:
                return wifiClient_.read();
#if ENABLE_ETHERNET==1
            case Transport::Ethernet:
                return ethernetClient_.read();
#endif
            default:
                return -1;
        }
    }

    size_t write(uint8_t value) {
        switch (transport_) {
            case Transport::WiFi:
                return wifiClient_.write(value);
#if ENABLE_ETHERNET==1
            case Transport::Ethernet:
                return ethernetClient_.write(value);
#endif
            default:
                return 0;
        }
    }

    size_t print(const String& value) {
        switch (transport_) {
            case Transport::WiFi:
                return wifiClient_.print(value);
#if ENABLE_ETHERNET==1
            case Transport::Ethernet:
                return ethernetClient_.print(value);
#endif
            default:
                return 0;
        }
    }

    size_t print(const char* value) {
        switch (transport_) {
            case Transport::WiFi:
                return wifiClient_.print(value);
#if ENABLE_ETHERNET==1
            case Transport::Ethernet:
                return ethernetClient_.print(value);
#endif
            default:
                return 0;
        }
    }

    size_t print(char value) {
        switch (transport_) {
            case Transport::WiFi:
                return wifiClient_.print(value);
#if ENABLE_ETHERNET==1
            case Transport::Ethernet:
                return ethernetClient_.print(value);
#endif
            default:
                return 0;
        }
    }

    size_t println() {
        switch (transport_) {
            case Transport::WiFi:
                return wifiClient_.println();
#if ENABLE_ETHERNET==1
            case Transport::Ethernet:
                return ethernetClient_.println();
#endif
            default:
                return 0;
        }
    }

    size_t println(const String& value) {
        switch (transport_) {
            case Transport::WiFi:
                return wifiClient_.println(value);
#if ENABLE_ETHERNET==1
            case Transport::Ethernet:
                return ethernetClient_.println(value);
#endif
            default:
                return 0;
        }
    }

    Transport transport() const {
        return transport_;
    }

private:
    Transport transport_ = Transport::None;
    WiFiClient wifiClient_;
#if ENABLE_ETHERNET==1
    EthernetClient ethernetClient_;
#endif
};

#endif // CHATGPT_SRC_TELNET_TRANSPORT_H_