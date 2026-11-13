#pragma once
// Host stand-in for the AVR/ESP PROGMEM helpers: strings live in ordinary RAM,
// so the _P variants are plain calls (same as stubs/Arduino.h does).
#include <string.h>

#ifndef PROGMEM
#define PROGMEM
#endif
#define strlen_P strlen
#define strncpy_P strncpy
#define memcpy_P memcpy
#define strcmp_P strcmp
#define pgm_read_byte(p) (*(const unsigned char*)(p))
#define pgm_read_word(p) (*(const unsigned short*)(p))
#define pgm_read_dword(p) (*(const unsigned long*)(p))
