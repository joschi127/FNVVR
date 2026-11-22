#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dxvk {

  /**
   * \brief Encodes RGBA8 pixels (top row first) as a PNG file in memory
   *
   * The image data is stored without compression (deflate "stored" blocks),
   * which keeps the encoder small; the files are for test captures only.
   */
  std::vector<uint8_t> vrEncodePng(const uint8_t* rgba, uint32_t width, uint32_t height);

  /**
   * \brief Writes RGBA8 pixels to a PNG file
   *
   * \returns \c false if the file could not be written
   */
  bool vrWritePng(const std::string& path, const uint8_t* rgba, uint32_t width, uint32_t height);

}
