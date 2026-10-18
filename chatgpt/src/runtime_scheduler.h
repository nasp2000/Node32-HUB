#pragma once

#include <Arduino.h>

String getCurrentTriggeredEmailFrequencyLabel();

void processSchedCmdSlots();
void processSchedDtSlots();
void processDailyRebootSlots();

void resetSchedCmdSlotTimer(int slotIdx);
void resetSchedDtSlotTimer(int slotIdx);
void resetDailyRebootSlotTimer(int slotIdx);

// Slot-1 shift following: when OEE shifts are configured, slot 1 fires at the
// end of the last shift (+grace) instead of its stored HH:MM. Slot 2 is manual.
bool dailyRebootFollowsShifts();
void dailyRebootEffectiveHM(int* hourOut, int* minuteOut);
