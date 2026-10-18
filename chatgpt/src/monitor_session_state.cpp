#include "monitor_session_state.h"

volatile bool autoEmailMonitorOverrideActive = false;
volatile int monitorOpenClients = 0;

void monitorOpenIncrement() {
    monitorOpenClients++;
    if (monitorOpenClients < 0) {
        monitorOpenClients = 0;
    }
    autoEmailMonitorOverrideActive = false;
}

void monitorOpenDecrement() {
    monitorOpenClients--;
    if (monitorOpenClients < 0) {
        monitorOpenClients = 0;
    }
    autoEmailMonitorOverrideActive = false;
}