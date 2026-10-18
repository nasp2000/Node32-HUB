#pragma once

#include <Arduino.h>

enum class OtaPageTransportMode {
    WifiAsync,
    EthernetBootstrap,
};

size_t streamOtaPageToPrint(Print& out, OtaPageTransportMode mode);
