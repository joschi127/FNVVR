#include <algorithm>

#include <windows.h>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_math.h"
#include "vr_preview_panels.h"
#include "vr_preview_projection.h"
#include "vr_preview_window.h"

namespace dxvk {

  namespace {

    constexpr const wchar_t* WindowClassName = L"DxvkVrPreviewWindow";
    constexpr const wchar_t* WindowTitle     = L"VR preview (left eye | right eye)";

    // Width of the window's client area, the height follows the eye aspect
    // ratio: d3d9.vrPreviewWidth, at most the screen's width
    uint32_t g_previewWidth = VrPreviewWindow::DefaultWidth;

    LRESULT CALLBACK previewWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
      // The swapchain needs the window, so closing it only hides it
      if (message == WM_CLOSE) {
        ShowWindow(window, SW_HIDE);
        return 0;
      }

      return DefWindowProcW(window, message, wParam, lParam);
    }

    bool registerWindowClass() {
      WNDCLASSW windowClass = { };
      windowClass.lpfnWndProc   = previewWindowProc;
      windowClass.hInstance     = GetModuleHandleW(nullptr);
      windowClass.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
      windowClass.lpszClassName = WindowClassName;

      return RegisterClassW(&windowClass) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

  }


  VrPreviewWindow::VrPreviewWindow(IDirect3DDevice9* device, HWND window)
  : m_device(device), m_window(window) { }


  VrPreviewWindow::~VrPreviewWindow() {
    m_swapChain = nullptr;
    DestroyWindow(m_window);
  }


  void VrPreviewWindow::setWidth(uint32_t width) {
    g_previewWidth = width;
  }


  std::unique_ptr<VrPreviewWindow> VrPreviewWindow::create(
          IDirect3DDevice9*     device,
    const VrExtent&             eyeExtent) {
    if (!eyeExtent.width || !eyeExtent.height || !registerWindowClass())
      return nullptr;

    uint32_t width  = std::max(VrPreviewWindow::MinWidth,
      std::min(g_previewWidth, uint32_t(std::max(GetSystemMetrics(SM_CXSCREEN), 1))));
    uint32_t height = std::max(1u, uint32_t(uint64_t(width) * eyeExtent.height / (2u * eyeExtent.width)));

    RECT rect = { 0, 0, LONG(width), LONG(height) };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    LONG windowWidth  = rect.right - rect.left;
    LONG windowHeight = rect.bottom - rect.top;

    // Right edge of the screen, so that the game window keeps its menus visible
    LONG left = std::max(0l, LONG(GetSystemMetrics(SM_CXSCREEN)) - windowWidth);

    // The game pauses when it loses the focus, so the preview must never take it
    HWND window = CreateWindowExW(WS_EX_NOACTIVATE, WindowClassName, WindowTitle,
      WS_OVERLAPPEDWINDOW, left, 0, windowWidth, windowHeight,
      nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!window)
      return nullptr;

    ShowWindow(window, SW_SHOWNA);

    std::unique_ptr<VrPreviewWindow> preview(new VrPreviewWindow(device, window));
    preview->m_width  = width;
    preview->m_height = height;

    D3DPRESENT_PARAMETERS params = { };
    params.BackBufferWidth        = width;
    params.BackBufferHeight       = height;
    params.BackBufferFormat       = D3DFMT_X8R8G8B8;
    params.BackBufferCount        = 1;
    params.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    params.hDeviceWindow          = window;
    params.Windowed               = TRUE;
    params.PresentationInterval   = D3DPRESENT_INTERVAL_IMMEDIATE;

    if (FAILED(device->CreateAdditionalSwapChain(&params, &preview->m_swapChain))) {
      Logger::err("VR: Failed to create the preview swapchain");
      return nullptr;
    }

    Logger::info(str::format("VR: Preview window ", width, "x", height));
    return preview;
  }


  void VrPreviewWindow::present(IDirect3DTexture9* eyes[VrEyeCount], const std::array<VrEyeView, VrEyeCount>& views) {
    Com<IDirect3DSurface9> backBuffer;

    if (FAILED(m_swapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &backBuffer)))
      return;

    const LONG half = LONG(m_width / 2);

    for (uint32_t i = 0; i < VrEyeCount; i++) {
      Com<IDirect3DSurface9> source;

      if (FAILED(eyes[i]->GetSurfaceLevel(0, &source)))
        return;

      RECT target = { LONG(i) * half, 0, LONG(i + 1) * half, LONG(m_height) };
      m_device->StretchRect(source.ptr(), nullptr, backBuffer.ptr(), &target, D3DTEXF_LINEAR);
    }

    if (VrPreviewPanels::enabled())
      drawOverlays(backBuffer.ptr(), views);

    m_swapChain->Present(nullptr, nullptr, nullptr, nullptr, 0);
  }


  void VrPreviewWindow::drawOverlays(IDirect3DSurface9* target, const std::array<VrEyeView, VrEyeCount>& views) {
    struct TexturedVertex { float x, y, z, rhw, u, v; };
    struct ColoredVertex  { float x, y, z, rhw; D3DCOLOR color; };

    // The order a headset shows them in: the menu, the head's HUD over it, the hands' panels over that
    static const VrPanelId Order[] = {
      VrPanelId::Menu, VrPanelId::Terminal, VrPanelId::CaravanValues, VrPanelId::PipBoyLampBar,
      VrPanelId::PipBoyHint, VrPanelId::HudHead, VrPanelId::HudWrist, VrPanelId::HudPrompt,
      VrPanelId::HudCharge, VrPanelId::HudAmmo, VrPanelId::QuickWheel,
    };

    // Made once: a state block of all state is large
    if (m_overlayState == nullptr && FAILED(m_device->CreateStateBlock(D3DSBT_ALL, &m_overlayState))) {
      m_overlayState = nullptr;
      return;
    }

    m_overlayState->Capture();

    Com<IDirect3DSurface9> oldTarget;
    Com<IDirect3DSurface9> oldDepth;
    m_device->GetRenderTarget(0, &oldTarget);
    m_device->GetDepthStencilSurface(&oldDepth);
    m_device->SetRenderTarget(0, target);
    m_device->SetDepthStencilSurface(nullptr);

    D3DVIEWPORT9 viewport = { 0, 0, DWORD(m_width), DWORD(m_height), 0.0f, 1.0f };
    m_device->SetViewport(&viewport);
    m_device->SetVertexShader(nullptr);
    m_device->SetPixelShader(nullptr);
    m_device->SetRenderState(D3DRS_ZENABLE, FALSE);
    m_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    m_device->SetRenderState(D3DRS_LIGHTING, FALSE);
    m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    m_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    m_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    m_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    m_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    m_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    m_device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    m_device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    m_device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    m_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    m_device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    m_device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    m_device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    m_device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    m_device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    m_device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    m_device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    m_device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    m_device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    m_device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

    bool began = SUCCEEDED(m_device->BeginScene());
    float half = float(m_width / 2);
    VrPose head = vrHeadFromEyes(views[0], views[1]);

    for (uint32_t e = 0; e < VrEyeCount; e++) {
      float left = float(e) * half;

      // Panels
      m_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
      m_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
      m_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
      m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
      m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

      for (VrPanelId id : Order) {
        VrPreviewPanel panel;
        D3DSURFACE_DESC desc;

        if (!VrPreviewPanels::panel(id, panel) || FAILED(panel.texture->GetLevelDesc(0, &desc)) || !desc.Width)
          continue;

        VrPose pose = panel.anchor == VrPanelAnchor::Head ? vrCompose(head, panel.pose) : panel.pose;
        float w = panel.width * 0.5f;
        float h = panel.width * float(desc.Height) / float(desc.Width) * 0.5f;
        const VrVector3 corners[4] = { { -w, h, 0.0f }, { w, h, 0.0f }, { w, -h, 0.0f }, { -w, -h, 0.0f } };
        const float uv[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
        TexturedVertex vertices[4];
        bool visible = true;

        for (uint32_t c = 0; c < 4 && visible; c++) {
          VrEyePoint point;
          visible = vrProjectToEye(views[e], vrTransformPoint(pose, corners[c]), point);
          vertices[c] = { left + point.u * half, point.v * float(m_height), 0.5f, point.rhw, uv[c][0], uv[c][1] };
        }

        if (!visible)
          continue;

        m_device->SetRenderState(D3DRS_SRCBLEND, panel.premultiplied ? D3DBLEND_ONE : D3DBLEND_SRCALPHA);
        m_device->SetTexture(0, panel.texture.ptr());
        m_device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, vertices, sizeof(TexturedVertex));
      }

      // Lasers
      m_device->SetTexture(0, nullptr);
      m_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
      m_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
      m_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
      m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
      m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
      m_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);

      for (uint32_t hand = 0; hand < VrHandCount; hand++) {
        VrPointerSubmission pointer;

        if (!VrPreviewPanels::pointer(VrHand(hand), pointer) || pointer.length <= 0.0f)
          continue;

        VrVector3 start = pointer.origin.position;
        VrVector3 end   = start + vrRotate(pointer.origin.orientation, { 0.0f, 0.0f, -pointer.length });
        VrEyePoint a, b;

        if (!vrProjectToEye(views[e], start, a) || !vrProjectToEye(views[e], end, b))
          continue;

        D3DCOLOR color = pointer.active ? D3DCOLOR_ARGB(255, 255, 210, 80) : D3DCOLOR_ARGB(180, 255, 170, 40);
        ColoredVertex line[2] = {
          { left + a.u * half, a.v * float(m_height), 0.5f, a.rhw, color },
          { left + b.u * half, b.v * float(m_height), 0.5f, b.rhw, color },
        };
        m_device->DrawPrimitiveUP(D3DPT_LINELIST, 1, line, sizeof(ColoredVertex));
      }
    }

    if (began)
      m_device->EndScene();

    m_device->SetRenderTarget(0, oldTarget.ptr());
    m_device->SetDepthStencilSurface(oldDepth.ptr());
    m_overlayState->Apply();
  }

}
