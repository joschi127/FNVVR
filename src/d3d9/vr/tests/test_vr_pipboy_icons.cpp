#include "test.h"

#include "../vr_pipboy_icons.h"

using namespace dxvk;


TEST_CASE(pipboy_icons_show_the_grips_for_lt_and_rt) {
  std::string replaced;
  bool loose = false;

  CHECK(vrPipBoyIconReplacement("interface\\shared\\button\\general_button_lt.dds", replaced, loose));
  CHECK(replaced == "interface\\shared\\button\\fnvvr_button_lgrip.dds");
  CHECK(loose);

  CHECK(vrPipBoyIconReplacement("Interface\\Shared\\Button\\Glow_General_Button_RT.dds", replaced, loose));
  CHECK(replaced == "interface\\shared\\button\\glow_fnvvr_button_rgrip.dds");
  CHECK(loose);
}


TEST_CASE(pipboy_icons_give_loose_images_their_path) {
  std::string replaced;
  bool loose = false;

  // The menus name the atlas images by their file alone
  CHECK(vrPipBoyIconReplacement("general_button_lt.dds", replaced, loose));
  CHECK(replaced == "interface\\shared\\button\\fnvvr_button_lgrip.dds");
  CHECK(loose);

  CHECK(vrPipBoyIconReplacement("general_button_lb.dds", replaced, loose));
  CHECK(replaced == "general_button_lt.dds");
  CHECK(!loose);
}


TEST_CASE(pipboy_icons_show_the_triggers_for_lb_and_rb) {
  std::string replaced;
  bool loose = true;

  CHECK(vrPipBoyIconReplacement("interface/shared/button/general_button_rb.dds", replaced, loose));
  CHECK(replaced == "interface/shared/button/general_button_rt.dds");
  CHECK(!loose);
}


TEST_CASE(pipboy_icons_leave_other_images) {
  std::string replaced;
  bool loose = false;

  CHECK(!vrPipBoyIconReplacement("interface\\shared\\button\\general_button_a.dds", replaced, loose));
  CHECK(!vrPipBoyIconReplacement("interface\\pipboy\\general_button_lt.dds.bak", replaced, loose));
  CHECK(!vrPipBoyIconReplacement("", replaced, loose));
}
