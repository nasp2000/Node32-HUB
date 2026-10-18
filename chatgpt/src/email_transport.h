#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "feature_flags.h"
#include "eth_w5500.h"

enum class EmailTlsTransportKind : uint8_t {
    None = 0,
    WiFi,          // TLS over WiFi
    EthernetPlain  // Ethernet with IP: TLS via ESP_SSLClient (W5500/RMII)
};

struct EmailTlsClientConfig {
    bool insecure;
    uint32_t timeoutMs;
    uint32_t handshakeTimeoutSeconds;
};

inline bool emailTransportHasWiFiConnectivity() {
    return WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

inline bool emailTransportHasEthernetConnectivity() {
#if ENABLE_ETHERNET==1
    return ethernetLocalIP() != IPAddress(0, 0, 0, 0);
#else
    return false;
#endif
}

inline EmailTlsTransportKind emailTransportGetTlsTransportKind() {
    if (emailTransportHasWiFiConnectivity()) {
        return EmailTlsTransportKind::WiFi;
    }
    if (emailTransportHasEthernetConnectivity()) {
        return EmailTlsTransportKind::EthernetPlain;
    }
    return EmailTlsTransportKind::None;
}

inline bool emailTransportCanCreateTlsClient() {
    return emailTransportGetTlsTransportKind() == EmailTlsTransportKind::WiFi;
}

// Current firmware can send email over WiFi (TLS) or Ethernet (TLS via ESP_SSLClient).
inline bool emailTransportCanSendEmail() {
    return emailTransportGetTlsTransportKind() != EmailTlsTransportKind::None;
}

inline String emailTransportUnavailableReason() {
    switch (emailTransportGetTlsTransportKind()) {
        case EmailTlsTransportKind::WiFi:
            return String();
        case EmailTlsTransportKind::EthernetPlain:
            return String(F("Ethernet active"));
        default:
            return String(F("no network connected"));
    }
}

inline String emailTransportActiveLabel() {
    switch (emailTransportGetTlsTransportKind()) {
        case EmailTlsTransportKind::WiFi:
            return String(F("WiFi"));
        case EmailTlsTransportKind::EthernetPlain:
            return String(F("Ethernet"));
        default:
            return String(F("None"));
    }
}

// Board-specific TLS Ethernet client factory (defined in ethernet.cpp where the
// actual EthernetClient type is known — WIZnet on S3/W5500, WiFi alias on P4).
// Returns nullptr when the Ethernet stack is not available.
Client* emailTransportCreateEthernetClient(const EmailTlsClientConfig& config);
bool   emailTransportConfigureEthernetClient(Client& client, const EmailTlsClientConfig& config);

// --- WiFi TLS clients (WiFiClientSecure) ---

inline WiFiClientSecure* emailTransportCreateTlsClient(const EmailTlsClientConfig &config) {
    if (!emailTransportCanCreateTlsClient()) {
        return nullptr;
    }

    WiFiClientSecure* client = new WiFiClientSecure();
    if (!client) {
        return nullptr;
    }

    if (config.insecure) {
        client->setInsecure();
    }
    client->setTimeout(config.timeoutMs);
    client->setHandshakeTimeout(config.handshakeTimeoutSeconds);
    return client;
}

inline bool emailTransportConfigureTlsClient(WiFiClientSecure &client,
                                             const EmailTlsClientConfig &config) {
    if (!emailTransportCanCreateTlsClient()) {
        return false;
    }

    if (config.insecure) {
        client.setInsecure();
    }
    client.setTimeout(config.timeoutMs);
    client.setHandshakeTimeout(config.handshakeTimeoutSeconds);
    return true;
}

inline WiFiClientSecure* emailTransportCreateTlsClient(bool tlsInsecure,
                                                       uint32_t timeoutMs = 30000,
                                                       uint32_t handshakeTimeoutSeconds = 60) {
    EmailTlsClientConfig config = {tlsInsecure, timeoutMs, handshakeTimeoutSeconds};
    return emailTransportCreateTlsClient(config);
}

inline bool emailTransportConfigureTlsClient(WiFiClientSecure &client,
                                             bool tlsInsecure,
                                             uint32_t timeoutMs = 30000,
                                             uint32_t handshakeTimeoutSeconds = 60) {
    EmailTlsClientConfig config = {tlsInsecure, timeoutMs, handshakeTimeoutSeconds};
    return emailTransportConfigureTlsClient(client, config);
}

// --- Generic transport-agnostic client (WiFi or Ethernet) ---

inline Client* emailTransportCreateClient(const EmailTlsClientConfig &config) {
    EmailTlsTransportKind kind = emailTransportGetTlsTransportKind();
    if (kind == EmailTlsTransportKind::WiFi) {
        return emailTransportCreateTlsClient(config);
    }
#if ENABLE_ETHERNET==1
    if (kind == EmailTlsTransportKind::EthernetPlain) {
        return emailTransportCreateEthernetClient(config);
    }
#endif
    return nullptr;
}

inline bool emailTransportConfigureClient(Client &client, const EmailTlsClientConfig &config) {
    EmailTlsTransportKind kind = emailTransportGetTlsTransportKind();
    if (kind == EmailTlsTransportKind::WiFi) {
        WiFiClientSecure &sec = static_cast<WiFiClientSecure&>(client);
        if (config.insecure) sec.setInsecure();
        sec.setTimeout(config.timeoutMs);
        sec.setHandshakeTimeout(config.handshakeTimeoutSeconds);
        return true;
    }
#if ENABLE_ETHERNET==1
    if (kind == EmailTlsTransportKind::EthernetPlain) {
        return emailTransportConfigureEthernetClient(client, config);
    }
#endif
    return false;
}

inline Client* emailTransportCreateClient(bool tlsInsecure,
                                          uint32_t timeoutMs = 30000,
                                          uint32_t handshakeTimeoutSeconds = 60) {
    EmailTlsClientConfig config = {tlsInsecure, timeoutMs, handshakeTimeoutSeconds};
    return emailTransportCreateClient(config);
}

inline bool emailTransportConfigureClient(Client &client,
                                          bool tlsInsecure,
                                          uint32_t timeoutMs = 30000,
                                          uint32_t handshakeTimeoutSeconds = 60) {
    EmailTlsClientConfig config = {tlsInsecure, timeoutMs, handshakeTimeoutSeconds};
    return emailTransportConfigureClient(client, config);
}
