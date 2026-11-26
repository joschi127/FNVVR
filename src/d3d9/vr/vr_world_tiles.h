#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "vr_game_images.h"
#include "vr_game_space.h"
#include "vr_paint_panel.h"
#include "vr_world_tile_place.h"

namespace dxvk {

  /**
   * \brief A HUD tile another mod anchors in the world
   */
  struct VrWorldTileInfo {
    uintptr_t   tile   = 0;
    std::string text;
    VrColour    colour = { };
    /// The anchor in the game world, in game units
    VrVector3   anchor = { };
    /// The mod marks the tile as a person's (trait user6 = 1), such as a
    /// friend's name: the compass shows a marker for it
    bool        person = false;
  };

  /**
   * \brief Shows HUD tiles that another mod anchors in the world at their place
   *
   * A mod puts a text tile into the HUD (HUDMainMenu) and sets traits the
   * game itself does not use: user5 = 1 marks it as anchored, user2, user3
   * and user4 hold the anchor's position in the game world. FNVVR then
   * hides the flat tile from every interface pass and draws its text, in
   * its colour, on a panel standing at the anchor (see VrWorldTileConfig).
   * Without FNVVR the traits do nothing, so the mod keeps working in the
   * flat game. The interface is described in docs/hud-world-tiles.md.
   *
   * It also keeps tiles of the HUD named in a list off the hand panels and
   * menus (d3d9.vrHudHeadTiles): screen-fixed tiles of other mods, such as a
   * chat line, belong in front of the head.
   *
   * update and show run on the game's main thread; the tiles to cull may be
   * asked for by the interface pass on another thread. The tiles are read
   * again each frame just before the eyes' interface passes; a mod that
   * destroyed one of its HUD tiles in between would leave a stale pointer
   * there (no known mod does).
   */
  class VrWorldTiles {

  public:

    VrWorldTiles(VrPainter& painter, IVRBackend& backend, VrGameImages& images);

    /**
     * \brief Shows the plates through the renderer's frame submitter
     */
    void setSubmitter(VrFrameSubmitter* submitter);

    ~VrWorldTiles();

    /**
     * \brief Checks the game's functions
     * \returns \c false if the tiles cannot be read; nothing is done then
     */
    bool initialize();

    void setConfig(const VrWorldTileConfig& config, const std::vector<std::string>& headOnlyTiles);

    /**
     * \brief Reads the HUD's tiles, once per frame before the interface is drawn
     */
    void update();

    /**
     * \brief Shows the anchored tiles, or hides them
     *
     * \param [in] shown Whether the HUD is shown in the world now
     * \param [in] reference Recenter reference, see vrMakeRecenterReference
     * \param [in] camera The game's camera this frame
     * \param [in] head Head pose in tracking space
     */
    void show(bool shown, const VrPose& reference, const VrGameCameraPose& camera, const VrPose& head);

    void hide();

    /**
     * \brief The tiles an interface pass must hide
     *
     * \param [in] headPass Whether the pass draws the HUD in front of the head
     * \param [out] tiles Tiles to cull, appended
     */
    void tilesToCull(bool headPass, std::vector<uintptr_t>& tiles) const;

    /**
     * \brief The anchored tiles read last
     */
    const std::vector<VrWorldTileInfo>& tiles() const {
      return m_tiles;
    }

  private:

    struct Slot {
      std::unique_ptr<VrPaintPanel> panel;
      uintptr_t   tile = 0;
      std::string text;
      uint32_t    colour = 0;
      bool        drawn  = false;
      /// Where the runtime shows the image; given again only when it moved
      /// or was drawn anew, as the runtime takes the image when it is given
      bool        placed = false;
      VrPose      shownPose  = { };
      float       shownWidth = 0.0f;
      uint32_t    idleFrames = 0;
    };

    VrPainter&     m_painter;
    IVRBackend&    m_backend;
    VrFrameSubmitter* m_submitter = nullptr;
    VrGameImages&  m_images;

    bool           m_available = false;
    uint32_t       m_traitFlag = 0;
    uint32_t       m_traitX    = 0;
    uint32_t       m_traitY    = 0;
    uint32_t       m_traitZ    = 0;
    uint32_t       m_traitKind = 0;
    uint32_t       m_traitSystemColour = 0;
    uint32_t       m_traitRed   = 0;
    uint32_t       m_traitGreen = 0;
    uint32_t       m_traitBlue  = 0;

    VrWorldTileConfig        m_config;
    std::vector<std::string> m_headOnlyNames;

    std::vector<VrWorldTileInfo> m_tiles;
    std::array<Slot, VrWorldTileCount> m_slots;
    VrPaintList    m_list;
    bool           m_loggedFound   = false;
    bool           m_loggedFailure = false;
    bool           m_loggedNoFont  = false;

    mutable std::mutex     m_cullMutex;
    std::vector<uintptr_t> m_cullWorld;
    std::vector<uintptr_t> m_cullHeadOnly;

    bool           m_traitsResolved = false;

    void resolveTraits();

    bool paintSlot(Slot& slot, const VrWorldTileInfo& info);

  };

}
