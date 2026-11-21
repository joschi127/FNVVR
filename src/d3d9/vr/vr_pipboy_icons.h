#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace dxvk {

  /**
   * \brief The image a gamepad button image of the open Pip-Boy shows in VR
   *
   * In VR the grips page through the Pip-Boy's main views and the triggers
   * press LB and RB (vrSwapShouldersAndTriggers). The LT and RT images show
   * the grips' images (fnvvr_button_lgrip.dds and rgrip, loose files next to
   * the game's), and the LB and RB images the game's LT and RT images, the
   * triggers. The glow variants follow.
   *
   * \param [in] path The image's file, relative to textures\, any case
   * \param [out] replaced The file to show instead, in the same folder
   * \param [out] loose Whether that file is loose, not in the texture atlas
   * \returns \c false if the image is not one that changes
   */
  bool vrPipBoyIconReplacement(
    const std::string&          path,
          std::string&          replaced,
          bool&                 loose);

  /**
   * \brief Shows the VR controllers' buttons on the open Pip-Boy
   *
   * While the Pip-Boy is open in VR, the button images of the interface's
   * tiles are given the files of vrPipBoyIconReplacement; once it closes,
   * they are given their own back, so that the game without VR and the
   * other menus keep theirs. A loose file has no place in the texture
   * atlas, so its image's atlas is cleared while it is shown.
   *
   * Only used on the game's main thread, outside the interface passes.
   */
  class VrPipBoyIcons {

  public:

    bool initialize();

    /**
     * \brief Changes or restores the images
     * \param [in] pipBoyOpen Whether the Pip-Boy is open
     */
    void update(bool pipBoyOpen);

  private:

    struct Original {
      std::string filename;
      std::string atlas;
      bool        loose = false;
    };

    bool m_available = false;
    bool m_logged    = false;

    std::unordered_map<uintptr_t, Original> m_changed;

    void visit(uintptr_t tile, uint32_t depth, bool open);

    void change(uintptr_t tile);

    void restore(uintptr_t tile, const Original& original);

  };

}
