#pragma once
#include <Arduino.h>
#include <stdint.h>

uint32_t healthGetLastAliveMs(uint8_t id);
uint32_t healthGetTimeoutMs(uint8_t id);
bool healthGetEssential(uint8_t id);
uint8_t healthGetCount();
const char* healthGetName(uint8_t id);
