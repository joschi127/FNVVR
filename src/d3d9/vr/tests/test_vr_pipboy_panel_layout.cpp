#include "test.h"

#include "../vr_pipboy_panel_layout.h"

using namespace dxvk;


TEST_CASE(pipboy_layout_frame_is_even_on_all_sides) {
  // A 4:3 panel image, a casing 2.5 % of its width on each side
  VrPipBoyPanelLayout layout = vrComputePipBoyPanelLayout(2000, 1500, 1280, 959, VrPipBoyPanelDefaultCrop, 0.025f);
  CHECK_NEAR(layout.inside.left * 2000.0f, 50.0f, 0.01f);
  CHECK_NEAR((1.0f - layout.inside.right) * 2000.0f, 50.0f, 0.01f);
  CHECK_NEAR(layout.inside.top * 1500.0f, 50.0f, 0.01f);
  CHECK_NEAR((1.0f - layout.inside.bottom) * 1500.0f, 50.0f, 0.01f);
}


TEST_CASE(pipboy_layout_crop_has_the_insides_aspect) {
  VrPipBoyPanelLayout layout = vrComputePipBoyPanelLayout(2000, 1500, 1280, 959, VrPipBoyPanelDefaultCrop, 0.025f);
  float insideAspect = (1900.0f) / (1400.0f);
  float cropAspect = (layout.crop.right - layout.crop.left) * 1280.0f / ((layout.crop.bottom - layout.crop.top) * 959.0f);
  CHECK_NEAR(cropAspect, insideAspect, 1e-3f);

  // Kept inside the texture, and still around the wanted part
  CHECK(layout.crop.left >= 0.0f && layout.crop.top >= 0.0f);
  CHECK(layout.crop.right <= 1.0f && layout.crop.bottom <= 1.0f);
  CHECK(layout.crop.left <= VrPipBoyPanelDefaultCrop.left + 1e-5f);
  CHECK(layout.crop.right >= VrPipBoyPanelDefaultCrop.right - 1e-5f);
}


TEST_CASE(pipboy_layout_grows_within_the_texture) {
  // A crop at the texture's top edge grows downwards only
  VrShareRect crop = { 0.2f, 0.0f, 0.8f, 0.2f };
  VrPipBoyPanelLayout layout = vrComputePipBoyPanelLayout(1000, 750, 1000, 1000, crop, 0.0f);
  CHECK_NEAR(layout.crop.top, 0.0f, 1e-6f);
  CHECK_NEAR(layout.crop.bottom - layout.crop.top, 0.6f / (4.0f / 3.0f), 1e-4f);
}


TEST_CASE(pipboy_casing_is_clear_inside_and_plastic_outside) {
  std::vector<uint8_t> rgba = vrPaintPipBoyCasing(100, 80, 10, 10, 90, 70, 4);
  CHECK(rgba.size() == 100u * 80u * 4u);

  const uint8_t* centre = &rgba[(40 * 100 + 50) * 4];
  CHECK(centre[3] == 0);

  const uint8_t* border = &rgba[(5 * 100 + 50) * 4];
  CHECK(border[0] > 40 && border[1] > 40 && border[2] > 30 && border[3] == 255);

  // The screen's corner is rounded: just inside the square corner is casing
  const uint8_t* corner = &rgba[(10 * 100 + 10) * 4];
  CHECK(corner[0] > 0 && corner[3] > 200);  // in the soft edge, nearly opaque
}


TEST_CASE(pipboy_casing_has_clear_rounded_outer_corners) {
  std::vector<uint8_t> rgba = vrPaintPipBoyCasing(100, 80, 10, 10, 90, 70, 4, 12);

  // The very corner is clear, the edge's middle and a point inside the rounding are not
  CHECK(rgba[(0 * 100 + 0) * 4 + 3] == 0);
  CHECK(rgba[(79 * 100 + 99) * 4 + 3] == 0);
  CHECK(rgba[(0 * 100 + 50) * 4 + 3] > 0);
  CHECK(rgba[(5 * 100 + 5) * 4 + 3] == 255);

  // Without an outer radius the corner is casing
  std::vector<uint8_t> square = vrPaintPipBoyCasing(100, 80, 10, 10, 90, 70, 4, 0);
  CHECK(square[3] == 255);
}
