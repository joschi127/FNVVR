#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "../d3d9_include.h"
#include "../../util/com/com_pointer.h"

#include "vr_dds.h"

namespace dxvk {

  class VrGameImages;

  /**
   * \brief Paints the grips' names over the LT and RT tabs of the Pip-Boy
   *    arm's gamepad texture
   *
   * The tabs sit left and right of the STATS, ITEMS and DATA knobs (see
   * VrPipBoyLampBarLayout). Their letters are covered with the tab's metal
   * and LG and RG are written in its place, in a small pixel font.
   *
   * \param [in,out] arm The arm's texture, 1024 x 1024, or a scaled copy
   */
  void vrPaintPipBoyArmTabs(VrImage& arm);

  /**
   * \brief The Pip-Boy's arm with the VR controllers' grips on its tabs
   *
   * The game paints LT and RT, the gamepad's triggers that page through the
   * Pip-Boy's views, on the arm's texture next to the knobs; in VR the grips
   * do it (vrSwapShouldersAndTriggers). While the eyes are drawn, the base
   * map of the arm's geometry is swapped for a copy of the gamepad texture
   * with the grips' names, and the game's own is put back right after, as
   * VrTerminalKeyboardModel does with the terminal's keyboard. The game,
   * the other menus and the game without VR keep the original.
   *
   * Only used on the game's main thread.
   */
  class VrPipBoyArmTexture {

  public:

    explicit VrPipBoyArmTexture(IDirect3DDevice9* device);

    ~VrPipBoyArmTexture();

    /**
     * \brief Swaps the arm's base map for the copy
     *
     * \param [in] device Root of the Pip-Boy model, or \c nullptr
     * \param [in] images Where the game's texture is read from
     */
    void beginDraw(uint8_t* device, VrGameImages& images);

    /**
     * \brief Puts the game's base map back
     */
    void endDraw();

  private:

    IDirect3DDevice9*          m_device;
    Com<IDirect3DTexture9>     m_texture;
    bool                       m_failed = false;
    bool                       m_logged = false;

    std::vector<std::pair<void**, void*>> m_swapped;

    bool create(VrGameImages& images);

  };

}
