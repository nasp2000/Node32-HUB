#pragma once

extern volatile bool autoEmailMonitorOverrideActive;
extern volatile int monitorOpenClients;

void monitorOpenIncrement();
void monitorOpenDecrement();