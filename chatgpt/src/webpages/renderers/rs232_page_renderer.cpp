#include "rs232_page_renderer.h"

#include "../assets/page_rs232_asset.h"

extern const char* FIRMWARE_VERSION;

size_t streamRs232PageToPrint(Print& out) {
    size_t n = 0;
    out.print(FPSTR(PAGE_RS232_HTML));
    n += strlen_P(PAGE_RS232_HTML);
    out.print(FIRMWARE_VERSION);
    n += strlen(FIRMWARE_VERSION);
    out.print(FPSTR(PAGE_RS232_FOOTER));
    n += strlen_P(PAGE_RS232_FOOTER);
    return n;
}
