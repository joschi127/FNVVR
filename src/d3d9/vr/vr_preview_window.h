#pragma once

#include <array>

#include <memory>

#include "../d3d9_include.h"

#include "../../util/com/com_pointer.h"

#include "vr_types.h"

namespace dxvk {

  /**
   * \brief Window that shows both eyes side by side
   *
   * Works with any backend: it only needs the D3D9 eye textures. It has its
   * own swapchain on the game's device, so it does not touch the game's
   * window. The window belongs to the thread that creates it, which has to
   * be the thread that pumps the game's messages.
   */
  class VrPreviewWindow {

  public:

    ~VrPreviewWindow();

    /**
     * \brief Creates the window and its swapchain
     *
     * \param [in] device D3D9 device that owns the eye textures
     * \param [in] eyeExtent Size of one eye texture, sets the aspect ratio
     * \returns \c nullptr if the window or swapchain could not be created
     */
    static std::unique_ptr<VrPreviewWindow> create(
            IDirect3DDevice9*     device,
            const VrExtent&       eyeExtent);

    /**
     * \brief Copies the eye textures into the window and presents it
     *
     * With d3d9.vrPreviewPanels, the panels and lasers are drawn over the
     * eyes where a headset's compositor would show them.
     *
     * \param [in] eyes Eye textures, indexed by VrEye
     * \param [in] views Eye poses and fields of view the eyes were rendered with
     */
    void present(IDirect3DTexture9* eyes[VrEyeCount], const std::array<VrEyeView, VrEyeCount>& views);

  private:

    VrPreviewWindow(IDirect3DDevice9* device, HWND window);

    void drawOverlays(IDirect3DSurface9* target, const std::array<VrEyeView, VrEyeCount>& views);

    IDirect3DDevice9*       m_device;
    HWND                    m_window;
    Com<IDirect3DSwapChain9> m_swapChain;
    Com<IDirect3DStateBlock9> m_overlayState;

    uint32_t                m_width  = 0;
    uint32_t                m_height = 0;

  };

}
