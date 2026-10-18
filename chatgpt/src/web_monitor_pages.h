#pragma once

#include <Arduino.h>
#include "feature_flags.h"

void streamSharedMonitorPage(Print& out);
void streamSharedBufferViewerPage(Print& out);
void streamSharedSerialMonitorPage(Print& out);