// do_renderer.cpp
// Streams the /do page from PROGMEM.

#include "do_renderer.h"

#if defined(BOARD_HAS_TCA9554_DO) && BOARD_HAS_TCA9554_DO == 1

#include "../assets/page_do_asset.h"

size_t streamDoPageToPrint(Print& out) {
    out.print(FPSTR(DO_PAGE));
    return strlen_P(DO_PAGE);
}

#endif // BOARD_HAS_TCA9554_DO
