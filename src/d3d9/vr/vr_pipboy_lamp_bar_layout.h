#pragma once

#include <cstdint>

namespace dxvk {

  /**
   * \brief The Pip-Boy's main views, as its tab knobs name them
   *
   * The values are the game's (InterfaceManager's Pip-Boy view to set).
   */
  enum class VrPipBoyView : uint32_t {
    None  = 0,
    Stats = 1,
    Items = 2,
    Data  = 3,
  };

  /**
   * \brief Layout of the lamp bar, from the Pip-Boy arm's gamepad texture
   *
   * The bar is the strip of the arm (textures\pipboy3000\xbox\pipboyarm01.dds,
   * 1024 x 1024) with STATS, ITEMS and DATA, their three knobs, and the
   * LT and RT tabs. Places on it are shares of the strip, (0, 0) top left.
   */
  struct VrPipBoyLampBarLayout {
    static constexpr uint32_t ImageWidth  = 690;
    static constexpr uint32_t ImageHeight = 200;
    /// The strip on the arm's texture, as shares of the texture
    static constexpr float    StripLeft   = 692.0f / 1024.0f;
    static constexpr float    StripTop    = 125.0f / 1024.0f;
    static constexpr float    StripRight  = 968.0f / 1024.0f;
    static constexpr float    StripBottom = 205.0f / 1024.0f;
    /// The knobs' centres (Stats, Items, Data), and their radius as a share of the width
    static constexpr float    KnobX0      = 0.257f;
    // From two headset tests (0.500/0.746 a little right, 0.491/0.728 a
    // little left) and the rings' centres measured on the texture
    static constexpr float    KnobX1      = 0.4955f;
    static constexpr float    KnobX2      = 0.737f;
    static constexpr float    KnobY       = 0.65f;
    static constexpr float    KnobRadius  = 0.075f;
    /// The tabs' letters, covered with the tab's colour for the grips' names
    static constexpr float    TabTop      = 0.36f;
    static constexpr float    TabBottom   = 0.55f;
    static constexpr float    LeftTabLeft   = 0.035f;
    static constexpr float    LeftTabRight  = 0.165f;
    static constexpr float    RightTabLeft  = 0.850f;
    static constexpr float    RightTabRight = 0.955f;
    /// Width of the bar, as a share of the panel's width
    static constexpr float    WidthShare  = 0.375f;
    /// Space between the panel's bottom edge and the bar, in metres
    static constexpr float    Gap         = 0.01f;
  };

  /**
   * \brief The knob of the lamp bar at a point of it, None if there is none
   *
   * \param [in] x Point on the bar, share of its width from the left
   * \param [in] y Share of its height from the top
   */
  VrPipBoyView vrPipBoyLampBarKnobAt(float x, float y);

  /**
   * \brief Pages the Pip-Boy to a wanted view, one step at a time
   *
   * The game takes a press of LT or RT while it switches views as no press,
   * so the next step is pressed only once the game shows the view the last
   * one went to, or after \c RetryAfter seconds without a change. A wanted
   * view that is not reached within \c GiveUpAfter seconds is dropped.
   */
  class VrPipBoyPager {

  public:

    static constexpr float RetryAfter  = 0.5f;
    static constexpr float GiveUpAfter = 3.0f;

    /// Pages to \c view from now on
    void start(VrPipBoyView view, float now);

    /// Drops the wanted view, as when the Pip-Boy closes
    void cancel() { m_target = VrPipBoyView::None; }

    bool active() const { return m_target != VrPipBoyView::None; }

    /**
     * \brief The step to press now
     *
     * \param [in] current The view the game shows, None between views
     * \param [in] pressing Whether the last step's press is still going on
     * \param [in] now Seconds on a steady clock
     * \returns 1 for a step forward (RT), -1 back (LT), 0 for none
     */
    int32_t update(VrPipBoyView current, bool pressing, float now);

  private:

    VrPipBoyView m_target    = VrPipBoyView::None;
    VrPipBoyView m_pressedAt = VrPipBoyView::None;
    float        m_started   = 0.0f;
    float        m_pressTime = 0.0f;

  };

}
