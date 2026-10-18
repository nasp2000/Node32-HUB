#pragma once

#include <Arduino.h>
#include "feature_flags.h"

void safeEnviarMensagemVirtual(const char *fmt, int level, ...);

#if ENABLE_BUZZER == 1
// Trigger a "config saved" beep if the buzzer on_save flag is set.
void buzzerOnSave();
#endif
void logManualNetworkSwitchReconnect();
void logHybridStaReconnectEvent(const char* reason, bool forceLog);
void prepareRs232ForAccessPointStart();
void performFactoryResetInternalCleanup();
void resetRecipientThrottleSlot(int slotIdx);
void logRs232TestPayloadToSd();