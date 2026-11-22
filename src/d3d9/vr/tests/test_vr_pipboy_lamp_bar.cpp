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


TEST_CASE(pager_presses_the_next_step_only_once_the_last_is_shown) {
  VrPipBoyPager pager;
  pager.start(VrPipBoyView::Data, 0.0f);

  // STATS to DATA: a step forward, then nothing while the game still shows STATS
  CHECK(pager.update(VrPipBoyView::Stats, false, 0.00f) == 1);
  CHECK(pager.update(VrPipBoyView::Stats, true,  0.05f) == 0);
  CHECK(pager.update(VrPipBoyView::Stats, false, 0.10f) == 0);
  CHECK(pager.update(VrPipBoyView::None,  false, 0.15f) == 0);

  // ITEMS is shown: the second step
  CHECK(pager.update(VrPipBoyView::Items, false, 0.20f) == 1);
  CHECK(pager.update(VrPipBoyView::Items, false, 0.30f) == 0);

  // DATA reached: done
  CHECK(pager.update(VrPipBoyView::Data, false, 0.40f) == 0);
  CHECK(!pager.active());
}


TEST_CASE(pager_presses_again_when_a_step_was_swallowed) {
  VrPipBoyPager pager;
  pager.start(VrPipBoyView::Stats, 0.0f);
  CHECK(pager.update(VrPipBoyView::Data, false, 0.0f) == -1);
  CHECK(pager.update(VrPipBoyView::Data, false, VrPipBoyPager::RetryAfter - 0.01f) == 0);
  CHECK(pager.update(VrPipBoyView::Data, false, VrPipBoyPager::RetryAfter + 0.01f) == -1);
}


TEST_CASE(pager_gives_up_and_can_be_cancelled) {
  VrPipBoyPager pager;
  pager.start(VrPipBoyView::Data, 0.0f);
  CHECK(pager.update(VrPipBoyView::Stats, false, 0.0f) == 1);
  CHECK(pager.update(VrPipBoyView::Stats, false, VrPipBoyPager::GiveUpAfter + 0.1f) == 0);
  CHECK(!pager.active());

  pager.start(VrPipBoyView::Items, 10.0f);
  pager.cancel();
  CHECK(pager.update(VrPipBoyView::Stats, false, 10.0f) == 0);
}
