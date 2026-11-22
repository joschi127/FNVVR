#include "vr_preview_panels.h"

#include <array>

#include "vr_emulator_backend.h"

namespace dxvk {

  namespace {

    VrEmulatorBackend* g_emulator = nullptr;
    std::array<VrPreviewPanel, VrPanelCount> g_panels;

  }


  void VrPreviewPanels::enable(VrEmulatorBackend* emulator) {
    g_emulator = emulator;
  }


  bool VrPreviewPanels::enabled() {
    return g_emulator != nullptr;
  }


  void VrPreviewPanels::record(VrPanelId id, IDirect3DTexture9* texture, const VrPose& pose, float width,
      VrPanelAnchor anchor, bool premultiplied) {
    uint32_t index = uint32_t(id);

    if (!g_emulator || index >= VrPanelCount)
      return;

    VrPreviewPanel& panel = g_panels[index];
    panel.texture = texture;
    panel.pose = pose;
    panel.width = width;
    panel.anchor = anchor;
    panel.premultiplied = premultiplied;
  }


  bool VrPreviewPanels::panel(VrPanelId id, VrPreviewPanel& result) {
    uint32_t index = uint32_t(id);

    if (!g_emulator || index >= VrPanelCount || !g_panels[index].texture.ptr() || !g_emulator->isPanelVisible(id))
      return false;

    result = g_panels[index];
    return true;
  }


  bool VrPreviewPanels::pointer(VrHand hand, VrPointerSubmission& result) {
    return g_emulator && g_emulator->pointer(hand, result);
  }

}
