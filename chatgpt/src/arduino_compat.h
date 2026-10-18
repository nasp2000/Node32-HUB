// Compatibility header to simplify builds with the Arduino IDE (ESP32-S3)
// Defines useful macros when missing (e.g., BOARD_HAS_PSRAM)
// Note: This file is safe to include in PlatformIO and the Arduino IDE

#ifndef CHATGPT_ARDUINO_COMPAT_H
#define CHATGPT_ARDUINO_COMPAT_H

// Only defines when not already defined by the build system
#ifndef BOARD_HAS_PSRAM
#define BOARD_HAS_PSRAM 1
#endif

#ifndef ARDUINO_RUNNING_CORE
#define ARDUINO_RUNNING_CORE 1
#endif

#ifndef ARDUINO_EVENT_RUNNING_CORE
#define ARDUINO_EVENT_RUNNING_CORE 1
#endif

#ifndef ARDUINO_USB_CDC_ON_BOOT
// 0 = Serial maps to UART0 (hardware bridge CH340/CP2102) — single COM port for logs + commands
// 1 = Serial maps to HWCDC (native USB) — requires separate COM port
#define ARDUINO_USB_CDC_ON_BOOT 0
#endif

#endif // CHATGPT_ARDUINO_COMPAT_H
