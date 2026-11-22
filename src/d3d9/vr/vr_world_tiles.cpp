#include <windows.h>

#include <algorithm>
#include <cmath>

#include "../../util/log/log.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_math.h"
#include "vr_world_tiles.h"

namespace dxvk {

  namespace {

    using TileGetValueFn    = uintptr_t (__thiscall*)(uintptr_t tile, uint32_t trait);
    using TileTextToTraitFn = uint32_t (__cdecl*)(const char* name);

    // Image of one tile: room for two lines, such as a name and "DOWNED 12s"
    constexpr uint32_t ImageWidth  = 512;
    constexpr uint32_t ImageHeight = 128;
    // Height of the capitals, in pixels of the image
    constexpr float    Capitals    = 30.0f;

    // A shown tile is given again once it moved by this share of its
    // distance (a fifth of a degree) or turned by about half a degree
    constexpr float    MoveToShow = 0.0035f;
    constexpr float    TurnToShow = 0.99999f;

    // Frames a slot stays unused before it lets go of its texture (about 10 s)
    constexpr uint32_t ReleaseAfterFrames = 900;

    // Bounds of the walk through the HUD's top-level tiles, far above what it holds
    constexpr uint32_t MaxHudChildren = 1024;

    template<typename T>
    T readField(uintptr_t base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

    uintptr_t hudTile() {
      uintptr_t tiles = readField<uintptr_t>(VrGame::MenuTiles, 0);
      return tiles ? readField<uintptr_t>(tiles, (VrGame::HudMenuId - VrGame::FirstMenuId) * sizeof(uintptr_t)) : 0;
    }

    uintptr_t value(uintptr_t tile, uint32_t trait) {
      return trait != VrGame::TileTraitUnknown
        ? reinterpret_cast<TileGetValueFn>(VrGame::TileGetValue)(tile, trait) : 0;
    }

    float number(uintptr_t tile, uint32_t trait, float fallback) {
      uintptr_t v = value(tile, trait);
      return v ? readField<float>(v, 8) : fallback;
    }

    uint32_t colourKey(const VrColour& c) {
      return vrPremultiplied(c);
    }

  }


  VrWorldTiles::VrWorldTiles(VrPainter& painter, IVRBackend& backend, VrGameImages& images)
  : m_painter(painter), m_backend(backend), m_images(images), m_list(ImageWidth, ImageHeight) { }


  VrWorldTiles::~VrWorldTiles() {
    hide();
  }


  bool VrWorldTiles::initialize() {
    m_available = VrGameMemory::matches(VrGame::TileGetValue,
        VrGame::TileGetValuePrologue, sizeof(VrGame::TileGetValuePrologue))
      && VrGameMemory::matches(VrGame::TileTextToTrait,
        VrGame::TileTextToTraitPrologue, sizeof(VrGame::TileTextToTraitPrologue))
      && VrGameMemory::readable(VrGame::MenuTiles, sizeof(uintptr_t))
      && VrGameMemory::readable(VrGame::TileUpdateLock, sizeof(CRITICAL_SECTION));

    if (!m_available) {
      Logger::warn("VR: The HUD's tiles cannot be read, tiles anchored in the world are shown flat");
      return false;
    }

    auto toTrait = reinterpret_cast<TileTextToTraitFn>(VrGame::TileTextToTrait);
    m_traitFlag  = toTrait(VrGame::WorldTileFlagTrait);
    m_traitX     = toTrait(VrGame::WorldTileXTrait);
    m_traitY     = toTrait(VrGame::WorldTileYTrait);
    m_traitZ     = toTrait(VrGame::WorldTileZTrait);
    m_traitKind  = toTrait(VrGame::WorldTileKindTrait);
    m_traitSystemColour = toTrait(VrGame::TileSystemColourTrait);
    m_traitRed   = toTrait(VrGame::TileRedTrait);
    m_traitGreen = toTrait(VrGame::TileGreenTrait);
    m_traitBlue  = toTrait(VrGame::TileBlueTrait);

    if (m_traitFlag == VrGame::TileTraitUnknown || m_traitX == VrGame::TileTraitUnknown
     || m_traitY == VrGame::TileTraitUnknown || m_traitZ == VrGame::TileTraitUnknown) {
      Logger::warn("VR: The game does not know the traits user2 to user5, tiles anchored in the world are shown flat");
      m_available = false;
    }

    return m_available;
  }


  void VrWorldTiles::setConfig(const VrWorldTileConfig& config, const std::vector<std::string>& headOnlyTiles) {
    m_config = config;
    m_headOnlyNames = headOnlyTiles;
  }


  void VrWorldTiles::update() {
    m_tiles.clear();

    std::vector<uintptr_t> world;
    std::vector<uintptr_t> headOnly;

    uintptr_t hud = m_available ? hudTile() : 0;

    if (hud) {
      auto lock = reinterpret_cast<CRITICAL_SECTION*>(VrGame::TileUpdateLock);
      EnterCriticalSection(lock);

      uint32_t visited = 0;

      for (uintptr_t entry = readField<uintptr_t>(hud, VrGame::TileChildren);
           entry && ++visited <= MaxHudChildren; entry = readField<uintptr_t>(entry, 0)) {
        uintptr_t tile = readField<uintptr_t>(entry, 8);

        if (!tile)
          continue;

        if (!m_headOnlyNames.empty()
         && vrTileNameMatches(readField<const char*>(tile, VrGame::TileName), m_headOnlyNames))
          headOnly.push_back(tile);

        if (!m_config.enabled || number(tile, m_traitFlag, 0.0f) == 0.0f)
          continue;

        // Without the game's font the tiles cannot be drawn in the world: they
        // stay flat. Asked for only now, in game, as the font is read once.
        if (!m_images.font()) {
          if (!m_loggedNoFont) {
            Logger::warn("VR: No game font for HUD tiles anchored in the world, they are shown flat");
            m_loggedNoFont = true;
          }

          continue;
        }

        // Anchored: never drawn flat, even while it has nothing to show
        world.push_back(tile);

        uintptr_t text = value(tile, VrGame::TileTraitString);
        const char* chars = text ? readField<const char*>(text, 0x0C) : nullptr;

        if (!chars || !*chars || m_tiles.size() >= VrWorldTileCount)
          continue;

        VrWorldTileInfo info;
        info.tile   = tile;
        info.text   = chars;
        info.anchor = { number(tile, m_traitX, 0.0f), number(tile, m_traitY, 0.0f), number(tile, m_traitZ, 0.0f) };
        info.person = number(tile, m_traitKind, 0.0f) == 1.0f;

        if (number(tile, m_traitSystemColour, 1.0f) != 0.0f) {
          info.colour = VrGameImages::hudColour();
        } else {
          info.colour = {
            std::clamp(number(tile, m_traitRed, 255.0f) / 255.0f, 0.0f, 1.0f),
            std::clamp(number(tile, m_traitGreen, 255.0f) / 255.0f, 0.0f, 1.0f),
            std::clamp(number(tile, m_traitBlue, 255.0f) / 255.0f, 0.0f, 1.0f), 1.0f };
        }

        if (std::isfinite(info.anchor.x) && std::isfinite(info.anchor.y) && std::isfinite(info.anchor.z)
         && !vrTileTextLines(info.text).empty())
          m_tiles.push_back(std::move(info));
      }

      LeaveCriticalSection(lock);
    }

    if (!m_loggedFound && !world.empty()) {
      Logger::info(str::format("VR: A mod anchors ", world.size(), " HUD tile(s) in the world, shown at their place"));
      m_loggedFound = true;
    }

    std::lock_guard<std::mutex> guard(m_cullMutex);
    m_cullWorld    = std::move(world);
    m_cullHeadOnly = std::move(headOnly);
  }


  void VrWorldTiles::tilesToCull(bool headPass, std::vector<uintptr_t>& tiles) const {
    std::lock_guard<std::mutex> guard(m_cullMutex);
    tiles.insert(tiles.end(), m_cullWorld.begin(), m_cullWorld.end());

    if (!headPass)
      tiles.insert(tiles.end(), m_cullHeadOnly.begin(), m_cullHeadOnly.end());
  }


  bool VrWorldTiles::paintSlot(Slot& slot, const VrWorldTileInfo& info) {
    const VrPaintFont* font = m_images.font();

    if (!font)
      return false;

    if (!slot.panel)
      slot.panel = std::make_unique<VrPaintPanel>(m_painter, m_backend, VrPanelId(uint32_t(VrPanelId::WorldTile0) + uint32_t(&slot - m_slots.data())));

    std::vector<std::string> lines = vrTileTextLines(info.text);
    float capitals = std::max(font->metrics.glyph('A').ascent, 1.0f);
    float scale    = Capitals / capitals;
    float lineStep = std::max(font->metrics.lineHeight() * scale, 1.0f);

    // Lines too wide for the image are made smaller
    for (const std::string& line : lines) {
      float width = VrPaintList::measureText(*font, line, scale);

      if (width > float(ImageWidth) - 8.0f) {
        float shrink = (float(ImageWidth) - 8.0f) / width;
        scale *= shrink;
        lineStep *= shrink;
      }
    }

    lines.resize(std::min<size_t>(lines.size(), size_t(float(ImageHeight) / lineStep)));

    // The lines centred, the last one's baseline near the image's bottom:
    // the text stands above the anchor, as on the flat screen
    m_list.clear();

    float baseline = float(ImageHeight) - 0.35f * lineStep - lineStep * float(lines.size() - 1);

    for (const std::string& line : lines) {
      float width = VrPaintList::measureText(*font, line, scale);
      m_list.drawText(*font, line, 0.5f * (float(ImageWidth) - width), baseline, scale, info.colour);
      baseline += lineStep;
    }

    if (!slot.panel->paint(m_list)) {
      if (!m_loggedFailure) {
        Logger::err("VR: Failed to draw a HUD tile anchored in the world");
        m_loggedFailure = true;
      }

      return false;
    }

    slot.tile   = info.tile;
    slot.text   = info.text;
    slot.colour = colourKey(info.colour);
    slot.drawn  = true;
    slot.placed = false;
    return true;
  }


  void VrWorldTiles::show(bool shown, const VrPose& reference, const VrGameCameraPose& camera, const VrPose& head) {
    if (!shown || !m_config.enabled) {
      hide();
      return;
    }

    for (uint32_t i = 0; i < VrWorldTileCount; i++) {
      Slot& slot = m_slots[i];

      if (i >= m_tiles.size()) {
        if (slot.panel)
          slot.panel->hide();
        slot.placed = false;

        // A slot unused for a while lets go of its texture
        if (slot.panel && ++slot.idleFrames > ReleaseAfterFrames) {
          slot.panel->release();
          slot.drawn = false;
          slot.idleFrames = 0;
        }

        continue;
      }

      slot.idleFrames = 0;

      const VrWorldTileInfo& info = m_tiles[i];

      if (!slot.drawn || slot.tile != info.tile || slot.text != info.text || slot.colour != colourKey(info.colour)) {
        if (!paintSlot(slot, info)) {
          if (slot.panel)
            slot.panel->hide();
          slot.placed = false;
          continue;
        }
      }

      VrVector3 anchor = vrTransformPoint(reference,
        vrComputeGamePointInReference(camera, info.anchor, VrGameUnitsPerMetre));
      VrWorldTilePlace place = vrPlaceWorldTile(m_config, head, anchor);

      if (!place.visible) {
        slot.panel->hide();
        slot.placed = false;
        continue;
      }

      VrVector3 moved = {
        place.pose.position.x - slot.shownPose.position.x,
        place.pose.position.y - slot.shownPose.position.y,
        place.pose.position.z - slot.shownPose.position.z };
      const VrQuaternion& a = place.pose.orientation;
      const VrQuaternion& b = slot.shownPose.orientation;
      float turned = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);

      // A new image is given whole; a moved one only placed anew
      if (!slot.placed || !slot.panel->shown()) {
        slot.panel->show(place.pose, place.width);
        slot.placed     = true;
        slot.shownPose  = place.pose;
        slot.shownWidth = place.width;
      } else if (vrLength(moved) > MoveToShow * place.distance || turned < TurnToShow
       || std::fabs(place.width - slot.shownWidth) > 0.01f * place.width) {
        slot.panel->move(place.pose, place.width);
        slot.placed     = true;
        slot.shownPose  = place.pose;
        slot.shownWidth = place.width;
      }
    }
  }


  void VrWorldTiles::hide() {
    for (Slot& slot : m_slots) {
      if (slot.panel)
        slot.panel->hide();
      slot.placed = false;
    }
  }

}
