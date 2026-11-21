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

}
