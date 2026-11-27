#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "../../util/log/log.h"

#include "vr_compass.h"
#include "vr_game_addresses.h"
#include "vr_game_memory.h"

namespace dxvk {

  namespace {

    // The game's compass pictures, see VrCompassPictures
    constexpr char StripPicture[]  = "interface\\hud\\glow_hud_comp_direction_strip.dds";
    constexpr char QuestPicture[]  = "interface\\hud\\glow_hud_compass_objective_marker.dds";
    constexpr char CustomPicture[] = "interface\\hud\\glow_hud_compass_pc_marker.dds";
    constexpr char PersonPicture[] = "interface\\hud\\glow_hud_tick_mark.dds";
    constexpr char HealthPicture[] = "interface\\main\\glow_hp_bar.dds";

    // ActorValueOwner::GetActorFloatValue and GetBaseActorFloatValue
    using GetAvFloatFn = float (__fastcall*)(uintptr_t owner, void* unused, uint32_t index);

    template<typename Fn>
    Fn virtualFunction(uintptr_t object, uintptr_t slot) {
      auto vtable = *reinterpret_cast<const uintptr_t* const*>(object);
      return reinterpret_cast<Fn>(vtable[slot / sizeof(uintptr_t)]);
    }

    // Seconds of a steady clock, for the health bar's pulse
    double steadySeconds() {
      using namespace std::chrono;
      return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

    // Frames between reads of the targets
    constexpr uint32_t TargetReadInterval = 10;
    // Most objective targets and teleport links looked at
    constexpr uint32_t MaxTargets = 8;
    constexpr uint32_t MaxLinks   = 64;
    // Most extra data entries walked on a cell
    constexpr uint32_t MaxExtraData = 256;

    template<typename T>
    T readField(uintptr_t base, uintptr_t offset) {
      return *reinterpret_cast<const T*>(base + offset);
    }

    bool readPosition(uintptr_t ref, VrVector3& position) {
      if (!ref || !VrGameMemory::readable(ref + VrGame::RefLocation, sizeof(VrVector3)))
        return false;

      position = readField<VrVector3>(ref, VrGame::RefLocation);
      return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
    }

    // Where an objective target's marker points: the first door of its way,
    // when it is somewhere else, or the target itself
    uintptr_t targetRef(uintptr_t target) {
      if (!VrGameMemory::readable(target, VrGame::TargetTeleportLinks + VrGame::SimpleArrayCount + sizeof(uint32_t)))
        return 0;

      uintptr_t links = target + VrGame::TargetTeleportLinks;
      uintptr_t items = readField<uintptr_t>(links, VrGame::SimpleArrayItems);
      uint32_t  count = readField<uint32_t>(links, VrGame::SimpleArrayCount);

      if (items && count && count <= MaxLinks && VrGameMemory::readable(items, sizeof(uintptr_t))) {
        uintptr_t door = readField<uintptr_t>(items, 0);

        if (door)
          return door;
      }

      return readField<uintptr_t>(target, VrGame::TargetRef);
    }

    // A line about an objective target for the log: its reference, its
    // teleport links and the reference shown
    void describeTarget(std::string& out, uintptr_t target, uintptr_t shown, const VrVector3& at) {
      // The target itself may be gone while its doors are shown: only read what can be read
      auto formId = [] (uintptr_t form) {
        return form && VrGameMemory::readable(form + VrGame::FormId, sizeof(uint32_t))
          ? readField<uint32_t>(form, VrGame::FormId) : 0u;
      };

      uintptr_t ref = readField<uintptr_t>(target, VrGame::TargetRef);
      uint32_t links = readField<uint32_t>(target + VrGame::TargetTeleportLinks, VrGame::SimpleArrayCount);
      char line[160];
      // Without the place, which changes while a target walks
      (void)at;
      std::snprintf(line, sizeof(line), " [target %08X, %u door(s), shown %08X]",
        formId(ref), links, formId(shown));
      out += line;
    }

    // The interior's north, from its ExtraNorthRotation; 0 outside
    float cellNorth(uintptr_t player) {
      uintptr_t cell = readField<uintptr_t>(player, VrGame::RefParentCell);

      if (!cell || !VrGameMemory::readable(cell, VrGame::CellExtraList + VrGame::ExtraListFirst + sizeof(uintptr_t))
       || !(readField<uint8_t>(cell, VrGame::CellFlags) & VrGame::CellFlagInterior))
        return 0.0f;

      uintptr_t extra = readField<uintptr_t>(cell + VrGame::CellExtraList, VrGame::ExtraListFirst);

      for (uint32_t i = 0; extra && i < MaxExtraData; i++) {
        if (!VrGameMemory::readable(extra, VrGame::ExtraNorthRotationAngle + sizeof(float)))
          return 0.0f;

        if (readField<uint8_t>(extra, VrGame::ExtraDataType) == VrGame::ExtraTypeNorthRotation) {
          float angle = readField<float>(extra, VrGame::ExtraNorthRotationAngle);
          return std::isfinite(angle) ? angle : 0.0f;
        }

        extra = readField<uintptr_t>(extra, VrGame::ExtraDataNext);
      }

      return 0.0f;
    }

  }


  VrCompass::VrCompass(VrPainter& painter, IVRBackend& backend, VrGameImages& images)
  : m_panel(painter, backend, VrPanelId::Compass),
    m_list(VrCompassImage::Width, VrCompassImage::Height),
    m_images(images) { }


  VrCompass::~VrCompass() {
    hide();
  }


  bool VrCompass::initialize() {
    m_available = VrGameMemory::readable(VrGame::Player, sizeof(uintptr_t));

    if (!m_available)
      Logger::warn("VR: The player cannot be read, the compass is not shown");

    return m_available;
  }


  void VrCompass::setConfig(const VrCompassConfig& config) {
    m_config = config;
    m_health = VrHealthState();
    m_drawn  = false;
    m_placed = false;
  }


  void VrCompass::readTargets() {
    m_targets.clear();
    m_north = 0.0f;

    uintptr_t player = readField<uintptr_t>(VrGame::Player, 0);

    if (!player || !VrGameMemory::readable(player, VrGame::PlayerPlacedMarker + VrGame::TargetTeleportLinks + 0x0C))
      return;

    m_north = cellNorth(player);
    std::string described;

    // The active quest objective's targets: a list whose nodes hold a
    // target and the next node, the first node inside the player
    uintptr_t node = player + VrGame::PlayerQuestTargets;

    for (uint32_t i = 0; node && i < MaxTargets; i++) {
      if (!VrGameMemory::readable(node, 2 * sizeof(uintptr_t)))
        break;

      uintptr_t target = readField<uintptr_t>(node, 0);
      Target found;
      found.kind = VrCompassMarkerKind::Quest;

      uintptr_t shown = target ? targetRef(target) : 0;

      if (shown && readPosition(shown, found.position)) {
        m_targets.push_back(found);
        describeTarget(described, target, shown, found.position);
      }

      node = readField<uintptr_t>(node, sizeof(uintptr_t));
    }

    // The player's own marker, an objective target of the same layout
    uintptr_t placed = player + VrGame::PlayerPlacedMarker;
    Target custom;
    custom.kind = VrCompassMarkerKind::Custom;

    uintptr_t placedShown = readField<uintptr_t>(placed, VrGame::TargetRef) ? targetRef(placed) : 0;

    if (placedShown && readPosition(placedShown, custom.position)) {
      m_targets.push_back(custom);
      described += " placed marker";
      describeTarget(described, placed, placedShown, custom.position);
    }

    // Logged when what the compass points at changes, for comparing with the game's compass
    char north[48];
    std::snprintf(north, sizeof(north), ", north %.3f", m_north);
    described += north;

    if (described != m_loggedTargets) {
      Logger::info(str::format("VR: Compass:", described.empty() ? std::string(" no target") : described));
      m_loggedTargets = described;
    }
  }


  void VrCompass::readHealth(uintptr_t player) {
    m_health = VrHealthState();

    if (!player || !VrGameMemory::readable(player + VrGame::ActorValueOwnerBase, sizeof(uintptr_t)))
      return;

    uintptr_t values = player + VrGame::ActorValueOwnerBase;
    uintptr_t vtable = readField<uintptr_t>(values, 0);

    if (!vtable || !VrGameMemory::readable(vtable, VrGame::AvOwnerFloatSlot + sizeof(uintptr_t)))
      return;

    float current = virtualFunction<GetAvFloatFn>(values, VrGame::AvOwnerFloatSlot)(values, nullptr, VrGame::AvHealth);
    float full    = virtualFunction<GetAvFloatFn>(values, VrGame::AvOwnerBaseFloatSlot)(values, nullptr, VrGame::AvHealth);

    if (!std::isfinite(current) || !std::isfinite(full) || full <= 0.0f) {
      m_health.fraction = 1.0f;
      return;
    }

    m_health.known    = true;
    m_health.fraction = std::clamp(current / full, 0.0f, 1.0f);
  }


  void VrCompass::show(bool shown, float heading, const std::vector<VrWorldTileInfo>& tiles, const VrFov& fov) {
    if (!shown || !m_available || m_failed || !(m_config.enabled || m_config.health)) {
      hide();
      return;
    }

    uintptr_t player = readField<uintptr_t>(VrGame::Player, 0);

    if (m_frame++ % TargetReadInterval == 0) {
      if (m_config.enabled)
        readTargets();

      if (m_config.health)
        readHealth(player);
    }

    VrVector3 from;

    if (!readPosition(player, from)) {
      hide();
      return;
    }

    bool healthShown = m_config.health && m_health.known;
    m_health.pulse = healthShown ? vrHealthPulse(m_config, m_health.fraction, steadySeconds()) : 1.0f;

    // Nothing to show: no layer of the runtime's few spent on an empty image
    if (!m_config.enabled && !healthShown) {
      hide();
      return;
    }

    // In an interior, headings count from the cell's north
    float compassHeading = heading - m_north;

    std::vector<VrCompassMarker> markers;

    for (const Target& target : m_targets) {
      if (!m_config.enabled)
        break;

      VrCompassMarker marker;
      marker.kind    = target.kind;
      marker.bearing = vrCompassBearing(from, target.position) - m_north;
      markers.push_back(marker);
    }

    for (const VrWorldTileInfo& tile : tiles) {
      if (!m_config.enabled)
        break;

      if (!tile.person)
        continue;

      VrCompassMarker marker;
      marker.kind    = VrCompassMarkerKind::Friend;
      marker.bearing = vrCompassBearing(from, tile.anchor) - m_north;
      marker.initial = 0;

      for (char c : tile.text) {
        if (c != ' ' && c != '\r' && c != '\n') {
          marker.initial = c;
          break;
        }
      }
      markers.push_back(marker);
    }

    // Drawn again when the heading or a marker moved by a pixel of the strip
    float redraw = std::max(m_config.span, 1.0f) * 3.14159265f / 180.0f / float(VrCompassImage::Width);
    // The health in steps of half a percent, so that regeneration does not draw every frame
    auto band = [&] (float fraction) {
      return fraction < m_config.healthCritical ? 2 : fraction < m_config.healthLow ? 1 : 0;
    };
    bool sameHealth = m_health.known == m_shownHealth.known && m_health.pulse == m_shownHealth.pulse
      && std::fabs(m_health.fraction - m_shownHealth.fraction) < 0.005f
      && band(m_health.fraction) == band(m_shownHealth.fraction);
    // Without the strip the heading does not matter
    bool sameHeading = !m_config.enabled || std::fabs(vrCompassOffset(compassHeading, m_shownHeading)) < redraw;
    bool same = m_drawn && sameHealth && sameHeading && markers.size() == m_shownMarkers.size();

    for (size_t i = 0; same && i < markers.size(); i++) {
      same = markers[i].kind == m_shownMarkers[i].kind && markers[i].initial == m_shownMarkers[i].initial
        && std::fabs(vrCompassOffset(markers[i].bearing, m_shownMarkers[i].bearing)) < redraw;
    }

    if (!same) {
      VrCompassPictures pictures;
      pictures.strip  = m_config.gameStrip ? m_images.picture(StripPicture) : nullptr;
      pictures.quest  = m_images.picture(QuestPicture);
      pictures.custom = m_images.picture(CustomPicture);
      pictures.person = m_images.picture(PersonPicture);
      pictures.healthBar = m_config.health && m_config.healthPicture ? m_images.picture(HealthPicture) : nullptr;

      vrPaintCompass(m_list, m_config, compassHeading, markers, VrGameImages::hudColour(), m_images.font(),
        pictures, m_health);

      if (!m_panel.paint(m_list)) {
        Logger::err("VR: Failed to draw the compass, it is not shown");
        m_failed = true;
        hide();
        return;
      }

      m_drawn        = true;
      m_placed       = false;
      m_shownHeading = compassHeading;
      m_shownMarkers = std::move(markers);
      m_shownHealth  = m_health;
    }

    // Fixed to the head: given again for a new image or after it was hidden,
    // as the runtime takes the image when it is given; moved when the field
    // of view changes
    VrCompassPlacement place = vrCompassPlace(m_config, fov);
    bool moved = std::fabs(place.angle - m_place.angle) > 0.05f || std::fabs(place.elevation - m_place.elevation) > 0.05f;

    if (!m_placed || !m_panel.shown()) {
      m_panel.show(vrCompassPose(m_config, place), vrCompassWidth(m_config, place), VrPanelAnchor::Head);
      m_placed = true;
      m_place  = place;
    } else if (moved) {
      m_panel.move(vrCompassPose(m_config, place), vrCompassWidth(m_config, place), VrPanelAnchor::Head);
      m_place  = place;
    }
  }


  void VrCompass::hide() {
    m_panel.hide();
    m_placed = false;
    // Shown again, e.g. after a load door: the targets are read at once
    m_frame  = 0;
  }

}
