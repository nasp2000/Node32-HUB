#pragma once

#include <Arduino.h>

struct SdBufferDiagnosticsSnapshot {
	int rs232ActiveSize = 0;
	int rs232ActiveCapacity = 0;
	int lptActiveSize = 0;
	int lptActiveCapacity = 0;
	String rs232Preview;
	String lptPreview;
};

unsigned long getSdcardPushMutexFailCountRs232();
unsigned long getSdcardPushPointerNullCountRs232();
void captureSdBufferDiagnosticsSnapshot(SdBufferDiagnosticsSnapshot& snapshot);
String getSdBuffersDiagnosticsJson();