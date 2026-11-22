#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dxvk {

  /**
   * \brief Writes a PNG row by row, without holding the image
   *
   * The image data is stored without compression (deflate "stored" blocks),
   * which keeps the encoder small and lets each row go straight to the sink;
   * the files are for test captures only. Rows are RGBA8, top row first.
   */
  class VrPngWriter {

  public:

    /// Receives the file's bytes in order; returns false on a write error
    using Sink = std::function<bool(const uint8_t* data, size_t size)>;

    VrPngWriter(Sink sink, uint32_t width, uint32_t height);

    /// Writes the next row (width * 4 bytes)
    bool row(const uint8_t* rgba);

    /// Writes the end of the file; false if rows are missing or a write failed
    bool finish();

  private:

    void put(const uint8_t* data, size_t size, bool inIdat);
    void putU32(uint32_t value, bool inIdat);
    void putRaw(const uint8_t* data, size_t size);

    Sink     m_sink;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_rows      = 0;
    uint64_t m_rawLeft   = 0;  ///< Raw scanline bytes not written yet
    uint32_t m_blockLeft = 0;  ///< Bytes left in the current stored block
    uint32_t m_crc       = 0;
    uint32_t m_adlerA    = 1;
    uint32_t m_adlerB    = 0;
    bool     m_ok        = true;

  };

  /**
   * \brief Encodes RGBA8 pixels (top row first) as a PNG file in memory
   */
  std::vector<uint8_t> vrEncodePng(const uint8_t* rgba, uint32_t width, uint32_t height);

}
