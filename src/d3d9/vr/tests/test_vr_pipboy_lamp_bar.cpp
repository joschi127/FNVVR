#include "test.h"

#include "../vr_pipboy_lamp_bar_layout.h"

using namespace dxvk;

using Layout = VrPipBoyLampBarLayout;


TEST_CASE(lamp_bar_finds_each_knob_at_its_centre) {
  CHECK(vrPipBoyLampBarKnobAt(Layout::KnobX0, Layout::KnobY) == VrPipBoyView::Stats);
  CHECK(vrPipBoyLampBarKnobAt(Layout::KnobX1, Layout::KnobY) == VrPipBoyView::Items);
  CHECK(vrPipBoyLampBarKnobAt(Layout::KnobX2, Layout::KnobY) == VrPipBoyView::Data);
}


TEST_CASE(lamp_bar_knobs_take_a_shaking_laser) {
  CHECK(vrPipBoyLampBarKnobAt(Layout::KnobX1 + Layout::KnobRadius, Layout::KnobY) == VrPipBoyView::Items);
}


TEST_CASE(lamp_bar_has_no_knob_between_or_on_the_tabs) {
  CHECK(vrPipBoyLampBarKnobAt(0.5f * (Layout::KnobX0 + Layout::KnobX1), Layout::KnobY) == VrPipBoyView::None);
  CHECK(vrPipBoyLampBarKnobAt(0.1f, 0.45f) == VrPipBoyView::None);
  CHECK(vrPipBoyLampBarKnobAt(Layout::KnobX1, 0.05f) == VrPipBoyView::None);
}
