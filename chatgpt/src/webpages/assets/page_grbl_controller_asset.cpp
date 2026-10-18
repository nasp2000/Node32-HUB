#include "page_grbl_controller_asset.h"

const char PAGE_GRBL_CONTROLLER[] PROGMEM =
R"GRBL(
<div class='page grbl-controller' id='grbl_page_root' data-grbl-theme='dark'>
  <div class='wrap'>
)GRBL"

#include "grbl/grbl_css.h"

#include "grbl/grbl_html_shell.h"
R"GRBL(
    (function() {
      'use strict';
)GRBL"
#include "grbl/grbl_state.h"
#include "grbl/grbl_ui.h"
#include "grbl/grbl_parser_3d.h"
#include "grbl/grbl_parser_worker.h"
#include "grbl/grbl_2d_render.h"
#include "grbl/grbl_2d_controls.h"
#include "grbl/grbl_contour.h"
#include "grbl/grbl_light.h"
#include "grbl/grbl_status.h"
#include "grbl/grbl_actions.h"
#include "grbl/grbl_sd.h"
#include "grbl/grbl_init.h"
R"GRBL(    
    </div>
  </div>
</div>
)GRBL"
;
