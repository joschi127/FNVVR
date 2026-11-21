#include <windows.h>

#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include "vr_game_addresses.h"
#include "vr_game_memory.h"
#include "vr_pipboy_icons.h"

namespace dxvk {

  namespace {

    using GetTypeFn   = uint32_t  (__fastcall*)(uintptr_t tile, void* unused);
    using GetValueFn  = uintptr_t (__fastcall*)(uintptr_t tile, void* unused, uint32_t trait);
    using SetStringFn = void      (__fastcall*)(uintptr_t tile, void* unused, uint32_t trait, const char* text, bool propagate);

    constexpr uint32_t  MaxDepth          = 32;
    // Tile::GetValue's Value: its string
    constexpr uintptr_t ValueString       = 0x0C;

    template<typename T>
    T readField(uintptr_t base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

    const char* stringValue(uintptr_t tile, uint32_t trait) {
      uintptr_t value = reinterpret_cast<GetValueFn>(VrGame::TileGetValue)(tile, nullptr, trait);
      return value ? readField<const char*>(value, ValueString) : nullptr;
    }

    void setString(uintptr_t tile, uint32_t trait, const std::string& text) {
      reinterpret_cast<SetStringFn>(VrGame::TileSetString)(tile, nullptr, trait, text.c_str(), true);
    }

  }


  bool VrPipBoyIcons::initialize() {
    m_available = VrGameMemory::matches(VrGame::TileGetValue,
        VrGame::TileGetValuePrologue, sizeof(VrGame::TileGetValuePrologue))
      && VrGameMemory::matches(VrGame::TileSetString,
        VrGame::TileSetStringPrologue, sizeof(VrGame::TileSetStringPrologue));

    if (!m_available)
      Logger::info("VR: The menu tile functions were not found, the Pip-Boy shows the gamepad's buttons");

    return m_available;
  }


  void VrPipBoyIcons::update(bool pipBoyOpen) {
    if (!m_available || (!pipBoyOpen && m_changed.empty()))
      return;

    auto lock = reinterpret_cast<LPCRITICAL_SECTION>(VrGame::TileUpdateLock);
    EnterCriticalSection(lock);

    uintptr_t tiles = readField<uintptr_t>(VrGame::MenuTiles, 0);
    uint32_t  menus = readField<uint16_t>(VrGame::MenuTileCount, 0);

    for (uint32_t i = 0; tiles && i < menus; i++) {
      uintptr_t menu = readField<uintptr_t>(tiles, i * sizeof(uintptr_t));

      if (menu)
        visit(menu, 0, pipBoyOpen);
    }

    // Tiles that were not found again are gone with their menu
    if (!pipBoyOpen)
      m_changed.clear();

    LeaveCriticalSection(lock);
  }


  void VrPipBoyIcons::visit(uintptr_t tile, uint32_t depth, bool open) {
    if (depth > MaxDepth)
      return;

    auto getType = reinterpret_cast<GetTypeFn>(readField<uintptr_t>(readField<uintptr_t>(tile, 0), VrGame::TileGetTypeSlot));

    if (getType(tile, nullptr) == VrGame::TileTypeImage) {
      auto entry = m_changed.find(tile);

      if (open && entry == m_changed.end())
        change(tile);
      else if (!open && entry != m_changed.end())
        restore(tile, entry->second);
    }

    for (uintptr_t node = readField<uintptr_t>(tile, VrGame::TileChildren); node; node = readField<uintptr_t>(node, 0)) {
      uintptr_t child = readField<uintptr_t>(node, 8);

      if (child)
        visit(child, depth + 1, open);
    }
  }


  void VrPipBoyIcons::change(uintptr_t tile) {
    const char* filename = stringValue(tile, VrGame::TraitFilename);

    std::string replaced;
    bool loose = false;

    if (!filename || !vrPipBoyIconReplacement(filename, replaced, loose))
      return;

    Original original;
    original.filename = filename;
    original.loose    = loose;

    if (loose) {
      const char* atlas = stringValue(tile, VrGame::TraitTextureAtlas);
      original.atlas = atlas ? atlas : "";

      if (atlas)
        setString(tile, VrGame::TraitTextureAtlas, "");
    }

    setString(tile, VrGame::TraitFilename, replaced);

    if (!m_logged) {
      Logger::info(str::format("VR: The Pip-Boy shows the controllers' buttons, '",
        original.filename, "' shows '", replaced, "'"));
      m_logged = true;
    }

    m_changed.emplace(tile, std::move(original));
  }


  void VrPipBoyIcons::restore(uintptr_t tile, const Original& original) {
    if (original.loose && !original.atlas.empty())
      setString(tile, VrGame::TraitTextureAtlas, original.atlas);

    setString(tile, VrGame::TraitFilename, original.filename);
  }

}
