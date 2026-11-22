#include "vr_pipboy_lamp_bar_layout.h"

namespace dxvk {

  VrPipBoyView vrPipBoyLampBarKnobAt(float x, float y) {
    using Layout = VrPipBoyLampBarLayout;

    constexpr float Aspect = float(Layout::ImageWidth) / float(Layout::ImageHeight);
    const float knobs[3] = { Layout::KnobX0, Layout::KnobX1, Layout::KnobX2 };

    // Generous: a laser shakes, and the knobs are small
    float reach = 1.5f * Layout::KnobRadius;

    for (uint32_t i = 0; i < 3; i++) {
      float dx = x - knobs[i];
      float dy = (y - Layout::KnobY) / Aspect;

      if (dx * dx + dy * dy <= reach * reach)
        return VrPipBoyView(i + 1);
    }

    return VrPipBoyView::None;
  }


  void VrPipBoyPager::start(VrPipBoyView view, float now) {
    m_target    = view;
    m_pressedAt = VrPipBoyView::None;
    m_started   = now;
  }


  int32_t VrPipBoyPager::update(VrPipBoyView current, bool pressing, float now) {
    if (m_target == VrPipBoyView::None)
      return 0;

    if (current == m_target || now - m_started > GiveUpAfter) {
      m_target = VrPipBoyView::None;
      return 0;
    }

    // Between two views, or the last press not done: wait
    if (current == VrPipBoyView::None || pressing)
      return 0;

    // The last step not shown yet: wait for it a while, then press again
    if (current == m_pressedAt && now - m_pressTime < RetryAfter)
      return 0;

    m_pressedAt = current;
    m_pressTime = now;
    return uint32_t(m_target) > uint32_t(current) ? 1 : -1;
  }

}
