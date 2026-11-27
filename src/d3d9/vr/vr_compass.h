#pragma once

#include <string>
#include <vector>

#include "vr_compass_layout.h"
#include "vr_game_images.h"
#include "vr_paint_panel.h"
#include "vr_world_tiles.h"

namespace dxvk {

  /**
   * \brief The compass along the top of the view
   *
   * A panel fixed to the head, above the middle of the view, drawn in the
   * look of the game's own compass (see vrPaintCompass). Its heading is the
   * head's, taken when the frame is drawn, so it turns with the head at
   * once, in V.A.T.S. and dialogues too. It shows the active quest
   * objective's targets (through the door that leads there when they are
   * elsewhere), the marker the player placed on the map, and friends from
   * VrWorldTiles' tiles marked as a person's. In an interior the cell's own
   * north is used, as on the game's compass.
   *
   * Runs on the game's main thread.
   */
  class VrCompass {

  public:

    VrCompass(VrPainter& painter, IVRBackend& backend, VrGameImages& images);

    /**
     * \brief Shows the panel through the renderer's frame submitter
     */
    void setSubmitter(VrFrameSubmitter* submitter) {
      m_panel.setSubmitter(submitter);
    }

    ~VrCompass();

    /**
     * \brief Checks that the player can be read
     */
    bool initialize();

    void setConfig(const VrCompassConfig& config);

    const VrCompassConfig& config() const {
      return m_config;
    }

    /**
     * \brief Shows the compass, or hides it
     *
     * \param [in] shown Whether the HUD is shown in the world now
     * \param [in] heading Heading the head faces in the game world, radians, 0 north, clockwise
     * \param [in] tiles Tiles anchored in the world, for friends' markers
     * \param [in] fov The field of view of both eyes together, for the
     *    strip's size and place (all zero when not known)
     */
    void show(bool shown, float heading, const std::vector<VrWorldTileInfo>& tiles, const VrFov& fov);

    void hide();

  private:

    struct Target {
      VrCompassMarkerKind kind = VrCompassMarkerKind::Quest;
      VrVector3           position = { };
    };

    VrPaintPanel      m_panel;
    VrPaintList       m_list;
    VrGameImages&     m_images;
    VrCompassConfig   m_config;

    bool              m_available = false;
    bool              m_failed    = false;
    uint32_t          m_frame     = 0;

    // Read every few frames: the game moves its targets rarely
    std::vector<Target> m_targets;
    float             m_north = 0.0f;
    std::string       m_loggedTargets;
    // The player's health, read with the targets
    VrHealthState     m_health;

    // What the image shows, to draw it only when it changes
    std::vector<VrCompassMarker> m_shownMarkers;
    VrHealthState     m_shownHealth;
    VrAmmoState       m_ammo;
    VrAmmoState       m_shownAmmo;
    float             m_shownHeading = -100.0f;
    bool              m_drawn = false;
    bool              m_placed = false;
    VrCompassPlacement m_place;

    void readTargets();

    void readHealth(uintptr_t player);

  };

}
