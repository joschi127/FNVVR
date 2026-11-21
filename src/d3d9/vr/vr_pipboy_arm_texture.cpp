#include <algorithm>
#include <cstring>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_images.h"
#include "vr_game_nodes.h"
#include "vr_pipboy_arm_texture.h"

namespace dxvk {

  namespace {

    using GetPropertyFn = uint8_t* (__fastcall*)(void* object, void* unused, int32_t type);

    // The gamepad variant of the arm, with the LT and RT tabs
    constexpr char ArmTexture[] = "pipboy3000\\xbox\\pipboyarm01.dds";

    constexpr uint32_t ArmSize  = 1024;
    constexpr uint32_t MaxDepth = 32;

    // On the 1024 x 1024 texture: the tabs' letters, and where the tab's
    // metal is read from (left of the letters, inside the tab)
    struct Tab {
      uint32_t left, top, right, bottom;
      uint32_t metalX, metalY;
      const char* name;
    };

    constexpr Tab Tabs[2] = {
      { 703, 154, 737, 168, 700, 160, "LG" },
      { 928, 154, 956, 168, 925, 160, "RG" },
    };

    // 5 x 7 letters, as the grip images of tools/make_grip_icons.py
    const char* letter(char c) {
      switch (c) {
        case 'L': return "11000" "11000" "11000" "11000" "11000" "11000" "11111";
        case 'R': return "11110" "11011" "11011" "11110" "11011" "11011" "11011";
        case 'G': return "01111" "11000" "11000" "11011" "11001" "11001" "01111";
        default:  return nullptr;
      }
    }

    constexpr uint32_t Ink = 0xFF262826;

    uint32_t average(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
      uint32_t result = 0;

      for (uint32_t shift = 0; shift < 32; shift += 8) {
        uint32_t sum = ((a >> shift) & 0xFF) + ((b >> shift) & 0xFF)
                     + ((c >> shift) & 0xFF) + ((d >> shift) & 0xFF);
        result |= ((sum + 2) / 4) << shift;
      }

      return result;
    }

    VrImage halve(const VrImage& image) {
      VrImage half;
      half.width  = std::max(image.width  / 2, 1u);
      half.height = std::max(image.height / 2, 1u);
      half.pixels.resize(size_t(half.width) * half.height);

      for (uint32_t y = 0; y < half.height; y++) {
        for (uint32_t x = 0; x < half.width; x++) {
          uint32_t x0 = std::min(2 * x, image.width - 1), x1 = std::min(2 * x + 1, image.width - 1);
          uint32_t y0 = std::min(2 * y, image.height - 1), y1 = std::min(2 * y + 1, image.height - 1);
          half.pixels[size_t(y) * half.width + x] = average(
            image.at(x0, y0), image.at(x1, y0), image.at(x0, y1), image.at(x1, y1));
        }
      }

      return half;
    }

  }


  void vrPaintPipBoyArmTabs(VrImage& arm) {
    if (!arm.width || !arm.height)
      return;

    float sx = float(arm.width)  / float(ArmSize);
    float sy = float(arm.height) / float(ArmSize);

    for (const Tab& tab : Tabs) {
      uint32_t left   = uint32_t(float(tab.left)   * sx);
      uint32_t top    = uint32_t(float(tab.top)    * sy);
      uint32_t right  = std::min(uint32_t(float(tab.right)  * sx), arm.width);
      uint32_t bottom = std::min(uint32_t(float(tab.bottom) * sy), arm.height);
      uint32_t metal  = arm.at(std::min(uint32_t(float(tab.metalX) * sx), arm.width - 1),
                               std::min(uint32_t(float(tab.metalY) * sy), arm.height - 1));

      for (uint32_t y = top; y < bottom; y++)
        for (uint32_t x = left; x < right; x++)
          arm.pixels[size_t(y) * arm.width + x] = metal;

      // Two letters of 5 x 7 cells and a cell between them, as large as
      // the tab allows
      uint32_t cell = std::max(1u, std::min((right - left) / 11, (bottom - top) / 7));
      uint32_t x0   = left + (right - left - 11 * cell) / 2;
      uint32_t y0   = top  + (bottom - top - 7 * cell) / 2;

      for (uint32_t i = 0; i < 2; i++) {
        const char* bits = letter(tab.name[i]);

        for (uint32_t row = 0; bits && row < 7; row++) {
          for (uint32_t column = 0; column < 5; column++) {
            if (bits[row * 5 + column] != '1')
              continue;

            for (uint32_t dy = 0; dy < cell; dy++) {
              for (uint32_t dx = 0; dx < cell; dx++) {
                uint32_t x = x0 + (i * 6 + column) * cell + dx;
                uint32_t y = y0 + row * cell + dy;

                if (x < arm.width && y < arm.height)
                  arm.pixels[size_t(y) * arm.width + x] = Ink;
              }
            }
          }
        }
      }
    }
  }


  VrPipBoyArmTexture::VrPipBoyArmTexture(IDirect3DDevice9* device)
  : m_device(device) { }


  VrPipBoyArmTexture::~VrPipBoyArmTexture() {
    endDraw();
  }


  void VrPipBoyArmTexture::beginDraw(uint8_t* device, VrGameImages& images) {
    endDraw();

    if (!device || m_failed || (m_texture == nullptr && !create(images)))
      return;

    // The base map of each geometry of the model whose Direct3D texture is
    // the arm's: 1024 x 1024 and compressed, which the glove and the
    // screen are not
    vrVisitGameObjects(reinterpret_cast<uintptr_t>(device), MaxDepth, [this] (uintptr_t object, uint32_t) {
      uint8_t* property = reinterpret_cast<GetPropertyFn>(VrGame::ObjectGetProperty)(
        reinterpret_cast<void*>(object), nullptr, VrGame::ShadePropertyType);
      auto     textures = property ? *reinterpret_cast<uint8_t***>(property + VrGame::ShaderPropertyTextures) : nullptr;
      uint8_t* base     = textures ? textures[0] : nullptr;
      uint8_t* data     = base ? *reinterpret_cast<uint8_t**>(base + VrGame::TextureRendererData) : nullptr;

      if (!data)
        return;

      void** slot = reinterpret_cast<void**>(data + VrGame::TextureDataD3DTexture);

      if (*slot == m_texture.ptr() || std::any_of(m_swapped.begin(), m_swapped.end(),
          [slot] (const std::pair<void**, void*>& entry) { return entry.first == slot; }))
        return;

      auto texture = static_cast<IDirect3DBaseTexture9*>(*slot);
      Com<IDirect3DTexture9> texture2d;
      D3DSURFACE_DESC desc = { };

      if (!texture || texture->GetType() != D3DRTYPE_TEXTURE
       || FAILED(texture->QueryInterface(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&texture2d)))
       || FAILED(texture2d->GetLevelDesc(0, &desc))
       || desc.Width != ArmSize || desc.Height != ArmSize
       || (desc.Format != D3DFMT_DXT1 && desc.Format != D3DFMT_DXT3 && desc.Format != D3DFMT_DXT5))
        return;

      m_swapped.emplace_back(slot, *slot);
      *slot = static_cast<IDirect3DBaseTexture9*>(m_texture.ptr());
    });

    if (!m_logged && !m_swapped.empty()) {
      Logger::info(str::format("VR: The Pip-Boy's arm shows the grips on its tabs (", m_swapped.size(), " geometry)"));
      m_logged = true;
    }
  }


  void VrPipBoyArmTexture::endDraw() {
    // In reverse, should one slot have been swapped twice
    for (auto entry = m_swapped.rbegin(); entry != m_swapped.rend(); ++entry)
      *entry->first = entry->second;

    m_swapped.clear();
  }


  bool VrPipBoyArmTexture::create(VrGameImages& images) {
    const VrImage* source = images.picture(ArmTexture);

    // The game's archives may not be read yet: tried again on a later frame
    if (!source)
      return false;

    if (source->width != ArmSize || source->height != ArmSize) {
      Logger::warn(str::format("VR: The Pip-Boy arm's texture is ", source->width, "x", source->height,
        ", not ", ArmSize, "x", ArmSize, ": its tabs keep LT and RT"));
      m_failed = true;
      return false;
    }

    VrImage level = *source;
    vrPaintPipBoyArmTabs(level);

    if (FAILED(m_device->CreateTexture(ArmSize, ArmSize, 0, 0, D3DFMT_A8R8G8B8,
        D3DPOOL_MANAGED, &m_texture, nullptr))) {
      Logger::err("VR: Failed to create the Pip-Boy arm's texture, its tabs keep LT and RT");
      m_failed = true;
      return false;
    }

    DWORD levels = m_texture->GetLevelCount();

    for (DWORD i = 0; i < levels; i++) {
      D3DLOCKED_RECT locked = { };

      if (FAILED(m_texture->LockRect(i, &locked, nullptr, 0))) {
        Logger::err("VR: Failed to fill the Pip-Boy arm's texture, its tabs keep LT and RT");
        m_texture = nullptr;
        m_failed  = true;
        return false;
      }

      // 0xAARRGGBB is D3DFMT_A8R8G8B8's layout
      for (uint32_t y = 0; y < level.height; y++)
        std::memcpy(static_cast<uint8_t*>(locked.pBits) + size_t(y) * locked.Pitch,
          &level.pixels[size_t(y) * level.width], size_t(level.width) * sizeof(uint32_t));

      m_texture->UnlockRect(i);

      if (i + 1 < levels)
        level = halve(level);
    }

    return true;
  }

}
