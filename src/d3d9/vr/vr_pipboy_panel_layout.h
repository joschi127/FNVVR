#pragma once

#include <cstdint>
#include <vector>

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief A rectangle as shares of an image, from 0 to 1
   */
  struct VrShareRect {
    float left   = 0.0f;
    float top    = 0.0f;
    float right  = 1.0f;
    float bottom = 1.0f;
  };

  /**
   * \brief Where the Pip-Boy's screen goes on the menu panel
   *
   * \c inside is the screen's place on the panel, inside the drawn casing;
   * \c crop the part of the Pip-Boy's screen texture shown there, with the
   * same aspect as \c inside so that nothing is stretched.
   */
  struct VrPipBoyPanelLayout {
    VrShareRect inside;
    VrShareRect crop;
  };

  /// The part of the screen texture with the Pip-Boy's menus and a dark rim around them, as on the wrist
  constexpr VrShareRect VrPipBoyPanelDefaultCrop = { 0.018f, 0.0f, 0.758f, 0.712f };

  /// Width of the drawn casing on each side, as a share of the panel's width
  constexpr float VrPipBoyPanelDefaultFrame = 0.022f;

  /// Radius of the panel's outer corners, as a share of its width: the screen's corners' and the casing's
  constexpr float VrPipBoyPanelDefaultCornerRadius = 0.04f;

  /**
   * \brief Places the Pip-Boy's screen on the panel
   *
   * The casing is as thick on every side. The crop is widened or heightened
   * about its centre, and kept inside the texture, until it has the aspect
   * of the place it is shown in.
   *
   * \param [in] panelWidth Width of the panel image, square pixels
   * \param [in] panelHeight Height of the panel image
   * \param [in] textureWidth Width of the Pip-Boy's screen texture
   * \param [in] textureHeight Height of the Pip-Boy's screen texture
   * \param [in] crop Wanted part of the texture
   * \param [in] frame Casing width, as a share of the panel's width
   */
  VrPipBoyPanelLayout vrComputePipBoyPanelLayout(uint32_t panelWidth, uint32_t panelHeight,
    uint32_t textureWidth, uint32_t textureHeight, const VrShareRect& crop, float frame);

  /**
   * \brief Paints the casing around the Pip-Boy's screen, RGBA8, top row first
   *
   * Greenish grey plastic, lighter towards its top edge and darker towards
   * its bottom, with a dark lip at the screen. The screen's place is clear
   * (alpha 0), with rounded corners of \c cornerRadius pixels and a soft
   * edge, so that the casing is blended over the copy of the screen. The
   * image's own corners are rounded with \c outerRadius pixels and clear
   * outside, so that the panel has the shape of a device.
   */
  std::vector<uint8_t> vrPaintPipBoyCasing(uint32_t width, uint32_t height,
    uint32_t insideLeft, uint32_t insideTop, uint32_t insideRight, uint32_t insideBottom, uint32_t cornerRadius,
    uint32_t outerRadius = 0);

}
