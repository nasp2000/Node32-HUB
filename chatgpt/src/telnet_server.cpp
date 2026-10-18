// telnet_server.cpp
// Telnet server implementation for ESP32 (WiFi + Ethernet transports).
// Extracted from chatgpt.ino to reduce monolithic file size.
// Global variable definitions and all Telnet server function implementations.

#include "telnet_server.h"
#include "commands.h"          // LogLevel enum
#include "runtime_environment.h" // ethernetHasIp()
#include "eth_w5500.h"           // ethernetLocalIP()

extern ConfigManager configManager;
// sendVirtualMessage declared in commands.h (included above)
extern void processLine(const String& line, unsigned long currentMillis);
extern void sendWifiStatusAndRssi(LogLevel level, unsigned long currentMillis);
extern bool autoSendEmail;
extern int currentMaxEmailLines;

#include "feature_flags.h"
#if ENABLE_TELNET==1

// Global variable definitions (moved from chatgpt.ino)
String telnetUser = "root";                       // Telnet username.
String telnetPass = "root";                       // Telnet password.
const int TELNET_PORT = 23;                       // Telnet port.
WiFiServer* telnetWifiServer = nullptr;           // Telnet server for WiFi/AP (lazy allocated).
#if ENABLE_ETHERNET==1
EthernetServer* telnetEthernetServer = nullptr;   // Telnet server for Ethernet (lazy allocated).
#endif
UnifiedTelnetClient telnetClient;                 // Active Telnet client on current transport.
bool telnetAuthenticated = false;                 // Whether the Telnet client is authenticated.
String telnetInputBuffer = "";                    // Telnet input buffer.
unsigned long telnetAuthTimeout = 0;              // Telnet authentication timeout timestamp.
const unsigned long TELNET_AUTH_TIMEOUT_MS = 60000; // 60 seconds to authenticate.
int telnetAuthState = 0;                          // Telnet authentication state machine.
bool wasTelnetClientConnected = false;            // Whether a client was previously connected.
unsigned long lastTelnetActivityMillis = 0;       // Timestamp of last Telnet I/O activity.
bool telnetCompactMode = true;                    // Compact mode: short prefixes, less noise.

// Function implementations (moved from chatgpt.ino)
static bool hasTelnetWiFiTransportAvailable() {
#if ENABLE_WIFI!=1
    return false;
#else
    wifi_mode_t currentMode = WiFi.getMode();
    if ((currentMode == WIFI_STA || currentMode == WIFI_AP_STA) &&
        WiFi.status() == WL_CONNECTED &&
        WiFi.localIP() != IPAddress(0,0,0,0)) {
        return true;
    }
    if ((currentMode == WIFI_AP || currentMode == WIFI_AP_STA) &&
        WiFi.softAPIP() != IPAddress(0,0,0,0)) {
        return true;
    }
    return false;
#endif
}

static bool hasTelnetEthernetTransportAvailable() {
#if ENABLE_ETHERNET==1
    return ethernetHasIp();
#else
    return false;
#endif
}

void stopTelnetServer() {
    if (telnetClient && telnetClient.connected()) {
        telnetClient.stop();
    }
    telnetAuthenticated = false;
    telnetAuthState = 0;
    telnetInputBuffer = "";
    wasTelnetClientConnected = false;

    if (telnetWifiServer) {
        delete telnetWifiServer;
        telnetWifiServer = nullptr;
    }
#if ENABLE_ETHERNET==1
    if (telnetEthernetServer) {
        delete telnetEthernetServer;
        telnetEthernetServer = nullptr;
    }
#endif
}

bool startTelnetServer() {
    // Start Telnet on any currently usable transport (WiFi STA/AP and/or Ethernet).
    bool canStartWiFiTelnet = hasTelnetWiFiTransportAvailable();
    bool canStartEthernetTelnet = hasTelnetEthernetTransportAvailable();
    bool startedAnyServer = false;
    String serverIP = "";

    if (canStartWiFiTelnet) {
        if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0,0,0,0)) {
            serverIP = WiFi.localIP().toString();
        }
        if (WiFi.softAPIP() != IPAddress(0,0,0,0)) {
            if (serverIP.isEmpty()) {
                serverIP = WiFi.softAPIP().toString();
            } else {
                serverIP += " / AP: " + WiFi.softAPIP().toString();
            }
        }
    }

#if ENABLE_ETHERNET==1
    IPAddress ethIp = ethernetLocalIP();
    if (canStartEthernetTelnet && ethIp != IPAddress(0,0,0,0)) {
        if (serverIP.isEmpty()) {
            serverIP = ethIp.toString() + " (ETH)";
        } else {
            serverIP += " / ETH: " + ethIp.toString();
        }
    }
#endif

    if (!canStartWiFiTelnet && !canStartEthernetTelnet) {
        sendVirtualMessage(String(F("Telnet indisponivel; nenhum transporte de rede ativo.")), WARN);
        return false;
    }

    // Check memory before starting Telnet server
    if (ESP.getFreeHeap() < 15000) { // Need at least 15KB for Telnet
    sendVirtualMessage(String(F("Insufficient memory for Telnet server")), WARN);
        return false;
    }
    
    if (canStartWiFiTelnet) {
        if (!telnetWifiServer) {
            telnetWifiServer = new WiFiServer(TELNET_PORT);
            if (!telnetWifiServer) {
                sendVirtualMessage(String(F("Failed to allocate WiFi Telnet server")), ERROR);
                return false;
            }
        }
        telnetWifiServer->begin();
        telnetWifiServer->setNoDelay(true);
        startedAnyServer = true;
    }

#if ENABLE_ETHERNET==1
    if (canStartEthernetTelnet) {
        if (!telnetEthernetServer) {
            telnetEthernetServer = new EthernetServer(TELNET_PORT);
            if (!telnetEthernetServer) {
                sendVirtualMessage(String(F("Failed to allocate Ethernet Telnet server")), ERROR);
                return startedAnyServer;
            }
        }
        telnetEthernetServer->begin();
        startedAnyServer = true;
    }
#endif

    if (startedAnyServer) {
        String telnetStartMsg = String(F("Telnet Server started on port "));
        telnetStartMsg += String(TELNET_PORT);
        telnetStartMsg += String(F(" no IP: "));
        telnetStartMsg += serverIP;
        
        // Always use sendVirtualMessage for consistency
        sendVirtualMessage(telnetStartMsg, TELNET_EVENT);
        return true;
    } else {
    sendVirtualMessage(String(F("Could not start Telnet server.")), ERROR);
        return false;
    }
}

void sendTelnet(const String &msg) {
    if (telnetClient && telnetClient.connected() && telnetAuthenticated) {
        // Regista actividade de sa?da Telnet
        lastTelnetActivityMillis = millis();
    }
}

void syncTelnetNetworkBindings() {
    static bool lastWifiTransportAvailable = false;
    static bool lastEthernetTransportAvailable = false;

    bool wifiTransportAvailable = hasTelnetWiFiTransportAvailable();
    bool ethernetTransportAvailable = hasTelnetEthernetTransportAvailable();
    bool transportChanged = (wifiTransportAvailable != lastWifiTransportAvailable) ||
                            (ethernetTransportAvailable != lastEthernetTransportAvailable);

    bool hasEthernetServerAllocated = false;
#if ENABLE_ETHERNET==1
    hasEthernetServerAllocated = (telnetEthernetServer != nullptr);
#endif

    if (!configManager.getTelnetEnabled()) {
        if (telnetWifiServer || hasEthernetServerAllocated || (telnetClient && telnetClient.connected())) {
            stopTelnetServer();
        }
        lastWifiTransportAvailable = wifiTransportAvailable;
        lastEthernetTransportAvailable = ethernetTransportAvailable;
        return;
    }

    bool clientOnWiFi = (telnetClient.transport() == UnifiedTelnetClient::Transport::WiFi);
    bool clientOnEthernet = (telnetClient.transport() == UnifiedTelnetClient::Transport::Ethernet);

    if (!wifiTransportAvailable && telnetWifiServer) {
        if (clientOnWiFi && telnetClient.connected()) {
            telnetClient.stop();
            telnetAuthenticated = false;
            telnetAuthState = 0;
            telnetInputBuffer = "";
            wasTelnetClientConnected = false;
        }
        delete telnetWifiServer;
        telnetWifiServer = nullptr;
    }

#if ENABLE_ETHERNET==1
    if (!ethernetTransportAvailable && telnetEthernetServer) {
        if (clientOnEthernet && telnetClient.connected()) {
            telnetClient.stop();
            telnetAuthenticated = false;
            telnetAuthState = 0;
            telnetInputBuffer = "";
            wasTelnetClientConnected = false;
        }
        delete telnetEthernetServer;
        telnetEthernetServer = nullptr;
    }
#endif

    bool missingActiveListener = (wifiTransportAvailable && !telnetWifiServer)
#if ENABLE_ETHERNET==1
                              || (ethernetTransportAvailable && !telnetEthernetServer)
#endif
                              ;

    if ((transportChanged || missingActiveListener) && (wifiTransportAvailable || ethernetTransportAvailable)) {
        startTelnetServer();
    }

    lastWifiTransportAvailable = wifiTransportAvailable;
    lastEthernetTransportAvailable = ethernetTransportAvailable;
}

void handleTelnetClient(unsigned long currentMillis) {
    bool pendingClient = false;
    bool pendingOnWiFi = false;
#if ENABLE_ETHERNET==1
    EthernetClient pendingEthernetClient;
#endif

    if (telnetWifiServer && telnetWifiServer->hasClient()) {
        pendingClient = true;
        pendingOnWiFi = true;
    }
#if ENABLE_ETHERNET==1
    else if (telnetEthernetServer) {
        pendingEthernetClient = telnetEthernetServer->accept();
        if (pendingEthernetClient) {
        pendingClient = true;
        pendingOnWiFi = false;
        }
    }
#endif

    if (!pendingClient && !(telnetClient && telnetClient.connected()) && !wasTelnetClientConnected) {
        return;
    }

    if (pendingClient) {
        if (telnetClient && telnetClient.connected()) {
            if (pendingOnWiFi) {
                WiFiClient newClient = telnetWifiServer->available();
                newClient.println(String(F("Servidor Telnet ocupado. Tente novamente mais tarde.")));
                newClient.stop();
            }
#if ENABLE_ETHERNET==1
            else {
                if (pendingEthernetClient) {
                    pendingEthernetClient.println(String(F("Servidor Telnet ocupado. Tente novamente mais tarde.")));
                    pendingEthernetClient.stop();
                }
            }
#endif
            sendVirtualMessage(String(F("New Telnet connection attempt rejected: client already connected.")), TELNET_EVENT);
        } else {
            if (pendingOnWiFi) {
                telnetClient = telnetWifiServer->available();
            }
#if ENABLE_ETHERNET==1
            else {
                telnetClient = pendingEthernetClient;
            }
#endif
            telnetAuthenticated = false;
            telnetAuthState = 0;
            telnetInputBuffer = "";
            telnetAuthTimeout = currentMillis + TELNET_AUTH_TIMEOUT_MS;
#if ENABLE_TELNET_AUTH==1
            String banner = "Bem-vindo ao " + configManager.getSystemName() + " (ESP32) via Telnet\r\n";
            telnetClient.print(banner);
            telnetClient.print(String(F("Username: ")));
#else
            // No authentication: mark as authenticated and show banner
            telnetAuthenticated = true;
            String banner = "Bem-vindo ao " + configManager.getSystemName() + " (ESP32) via Telnet (AUTH OFF)\r\n> ";
            telnetClient.print(banner);
#endif
            sendVirtualMessage(String(F("New Telnet client connected. Waiting for authentication.")), TELNET_EVENT);
            wasTelnetClientConnected = true;
            // marca actividade imediata
            lastTelnetActivityMillis = currentMillis;
        }
    }

    if (telnetClient && telnetClient.connected()) {
        if (!telnetAuthenticated) {
#if ENABLE_TELNET_AUTH==1
            if (currentMillis > telnetAuthTimeout) {
                telnetClient.println(String(F("\r\nAuthentication timeout. Connection closed.")));
                telnetClient.stop();
                sendVirtualMessage(String(F("Telnet client disconnected due to authentication timeout.")), TELNET_EVENT);
                wasTelnetClientConnected = false;
                return;
            }

            while (telnetClient.available()) {
                char c = telnetClient.read();
                if (c == '\r' || c == '\n') {
                    if (telnetInputBuffer.length() > 0) {
                        if (telnetAuthState == 0) {
                            if (telnetInputBuffer == telnetUser) {
                                telnetClient.print(String(F("Password: ")));
                                telnetAuthState = 1;
                                telnetInputBuffer = "";
                            } else {
                                telnetClient.println(String(F("\r\nUsername inv?lido.")));
                                telnetClient.print(String(F("Username: ")));
                                telnetInputBuffer = "";
                            }
                        } else if (telnetAuthState == 1) {
                            if (telnetInputBuffer == telnetPass) {
                                telnetAuthenticated = true;
                                telnetClient.println(String(F("\r\nAuthentication successful. Welcome!\r\n")));
                                sendVirtualMessage(String(F("Telnet client authenticated successfully.")), TELNET_EVENT);
                                sendWifiStatusAndRssi(TELNET_EVENT, currentMillis);
                                String autoEmailStatusTelnet = String(F("AUTO_EMAIL_STATUS:"));
                                autoEmailStatusTelnet += String(autoSendEmail ? "ATIVO" : "DESATIVADO");
                                sendVirtualMessage(autoEmailStatusTelnet, TELNET_EVENT);
                                String emailLineLimitTelnet = String(F("EMAIL_LINE_LIMIT:"));
                                emailLineLimitTelnet += String(currentMaxEmailLines);
                                sendVirtualMessage(emailLineLimitTelnet, TELNET_EVENT);
                            } else {
                                telnetClient.println(String(F("\r\nPassword inv?lida.")));
                                telnetClient.print(String(F("Username: ")));
                                telnetAuthState = 0;
                                telnetInputBuffer = "";
                            }
                        }
                    }
                } else if (c == 8 || c == 127) {
                    if (telnetInputBuffer.length() > 0) {
                        telnetInputBuffer.remove(telnetInputBuffer.length() - 1);
                        telnetClient.print(String(F("\b \b")));
                    }
                } else if (isPrintable(c)) {
                    telnetInputBuffer += c;
                    telnetClient.print(c);
                    // I/O: update activity timestamp
                    lastTelnetActivityMillis = currentMillis;
                }
            }
#else
            // No authentication: nothing to handle at this stage
#endif
        } else {
            while (telnetClient.available()) {
                char c = telnetClient.read();
                if (c == '\r' || c == '\n') {
                    if (telnetInputBuffer.length() > 0) {
                        processLine(telnetInputBuffer, currentMillis);
                        telnetInputBuffer = "";
                    }
                    telnetClient.print(String(F("> ")));
                } else if (c == 8 || c == 127) {
                    if (telnetInputBuffer.length() > 0) {
                        telnetInputBuffer.remove(telnetInputBuffer.length() - 1);
                        telnetClient.print(String(F("\b \b")));
                    }
                } else if (isPrintable(c)) {
                    telnetInputBuffer += c;
                    telnetClient.print(c);
                    // I/O: update activity timestamp
                    lastTelnetActivityMillis = currentMillis;
                }
            }
        }
    } else if (wasTelnetClientConnected) {
        telnetClient.stop();
        telnetAuthenticated = false;
        telnetAuthState = 0;
        sendVirtualMessage(String(F("Telnet client disconnected.")), TELNET_EVENT);
        wasTelnetClientConnected = false;
    }
}

#else // ENABLE_TELNET==0
// Stub variable definitions needed for extern resolution when Telnet is disabled.
UnifiedTelnetClient telnetClient;
bool telnetAuthenticated = false;
#endif // ENABLE_TELNET