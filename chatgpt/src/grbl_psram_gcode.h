#pragma once
#include <stdint.h>
#include <stddef.h>

bool grblPsramGcodeStore(const char* name, const uint8_t* data, size_t size);
void grblPsramGcodeClear();
const char* grblPsramGcodeGetName();
size_t grblPsramGcodeGetSize();
const char* grblPsramGcodeGetData();
char* grblPsramGcodeRelease();
