#pragma once
#include <Arduino.h>
#include <stdint.h>

enum class NetworkMedium : uint8_t {
    None = 0,
    WiFi = 1,
    Ethernet = 2,
    Both = 3
};

enum class ConnectionState : uint8_t {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    Failed = 3
};

struct NetworkConfig {
    String hostname;
    String ssid;
    String password;
    bool useDhcp;
    IPAddress staticIp;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress dns1;
    IPAddress dns2;
};

struct NetworkStatus {
    ConnectionState wifiState;
    ConnectionState ethState;
    String wifiSsid;
    int32_t wifiRssi;
    IPAddress wifiIp;
    IPAddress ethIp;
    String macAddress;
    bool apActive;
    String apSsid;
    IPAddress apIp;
    int8_t apClientCount;
};

class INetworkManager {
public:
    virtual ~INetworkManager() = default;

    virtual bool init() = 0;
    virtual void loop() = 0;

    virtual bool connectWiFi(const String& ssid, const String& password) = 0;
    virtual void disconnectWiFi() = 0;
    virtual bool wifiReconnect() = 0;
    virtual ConnectionState getWiFiState() const = 0;

    virtual bool startAccessPoint(const String& ssid, const String& password = "") = 0;
    virtual void stopAccessPoint() = 0;
    virtual bool isApActive() const = 0;

    virtual bool initEthernet() = 0;
    virtual ConnectionState getEthernetState() const = 0;

    virtual bool hasConnectivity() const = 0;
    virtual IPAddress getPreferredIP() const = 0;
    virtual NetworkMedium getActiveMedium() const = 0;
    virtual NetworkStatus getStatus() const = 0;

    virtual bool addSavedNetwork(const String& ssid, const String& password) = 0;
    virtual bool removeSavedNetwork(const String& ssid) = 0;
    virtual JsonArray getSavedNetworks() const = 0;
    virtual String findBestAvailableNetwork() const = 0;
};
