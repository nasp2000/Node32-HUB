// can_page_renderer.h
// Streams the PROGMEM CAN bus page to a Print object.

#pragma once
#include <Arduino.h>

size_t streamCanPageToPrint(Print& out);
