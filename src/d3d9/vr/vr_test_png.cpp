#include "vr_test_png.h"

#include <algorithm>

namespace dxvk {

  namespace {

    const uint32_t* crcTable() {
      static uint32_t table[256];
      static bool ready = false;

      if (!ready) {
        for (uint32_t n = 0; n < 256; n++) {
          uint32_t c = n;

          for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;

          table[n] = c;
        }

        ready = true;
      }

      return table;
    }

    /// Updates a running CRC-32 (start with 0xFFFFFFFF, finish with ~)
    uint32_t crcUpdate(uint32_t crc, const uint8_t* data, size_t size) {
      const uint32_t* table = crcTable();

      for (size_t i = 0; i < size; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);

      return crc;
    }

    void writeChunk(const VrPngWriter::Sink& sink, bool& ok, const char type[4], const uint8_t* data, uint32_t size) {
      uint8_t head[8] = { uint8_t(size >> 24), uint8_t(size >> 16), uint8_t(size >> 8), uint8_t(size),
                          uint8_t(type[0]), uint8_t(type[1]), uint8_t(type[2]), uint8_t(type[3]) };
      uint32_t crc = ~crcUpdate(crcUpdate(0xFFFFFFFFu, head + 4, 4), data, size);
      uint8_t tail[4] = { uint8_t(crc >> 24), uint8_t(crc >> 16), uint8_t(crc >> 8), uint8_t(crc) };
      ok = ok && sink(head, 8) && (!size || sink(data, size)) && sink(tail, 4);
    }

  }


  VrPngWriter::VrPngWriter(Sink sink, uint32_t width, uint32_t height)
  : m_sink(std::move(sink)), m_width(width), m_height(height) {
    static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    m_ok = m_sink(signature, sizeof(signature));

    uint8_t header[13] = { uint8_t(width >> 24), uint8_t(width >> 16), uint8_t(width >> 8), uint8_t(width),
      uint8_t(height >> 24), uint8_t(height >> 16), uint8_t(height >> 8), uint8_t(height),
      8, 6, 0, 0, 0 };  // 8 bits, RGBA, deflate, no filter, no interlace
    writeChunk(m_sink, m_ok, "IHDR", header, sizeof(header));

    // One IDAT chunk: zlib header, stored blocks of at most 65535 bytes, Adler-32
    m_rawLeft = (uint64_t(width) * 4 + 1) * height;
    uint64_t blocks = std::max<uint64_t>(1, (m_rawLeft + 65534) / 65535);
    uint64_t length = 2 + blocks * 5 + m_rawLeft + 4;

    if (length > 0x7FFFFFFFu)
      m_ok = false;

    m_crc = 0xFFFFFFFFu;
    uint8_t head[8] = { uint8_t(length >> 24), uint8_t(length >> 16), uint8_t(length >> 8), uint8_t(length),
                        'I', 'D', 'A', 'T' };
    m_ok = m_ok && m_sink(head, 4);
    put(head + 4, 4, true);

    static const uint8_t zlib[2] = { 0x78, 0x01 };
    put(zlib, 2, true);

    if (!m_rawLeft) {
      static const uint8_t empty[5] = { 1, 0, 0, 0xFF, 0xFF };
      put(empty, 5, true);
    }
  }


  void VrPngWriter::put(const uint8_t* data, size_t size, bool inIdat) {
    if (inIdat)
      m_crc = crcUpdate(m_crc, data, size);

    m_ok = m_ok && m_sink(data, size);
  }


  void VrPngWriter::putU32(uint32_t value, bool inIdat) {
    uint8_t bytes[4] = { uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value) };
    put(bytes, 4, inIdat);
  }


  void VrPngWriter::putRaw(const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
      m_adlerA = (m_adlerA + data[i]) % 65521;
      m_adlerB = (m_adlerB + m_adlerA) % 65521;
    }

    while (size) {
      if (!m_blockLeft) {
        uint32_t len = uint32_t(std::min<uint64_t>(65535, m_rawLeft));
        uint8_t head[5] = { uint8_t(len == m_rawLeft ? 1 : 0), uint8_t(len), uint8_t(len >> 8),
                            uint8_t(~len), uint8_t((~len) >> 8) };
        put(head, 5, true);
        m_blockLeft = len;
      }

      size_t n = std::min<size_t>(size, m_blockLeft);
      put(data, n, true);
      data += n;
      size -= n;
      m_blockLeft -= uint32_t(n);
      m_rawLeft -= n;
    }
  }


  bool VrPngWriter::row(const uint8_t* rgba) {
    if (m_rows >= m_height)
      return m_ok = false;

    static const uint8_t filter = 0;
    putRaw(&filter, 1);
    putRaw(rgba, size_t(m_width) * 4);
    m_rows++;
    return m_ok;
  }


  bool VrPngWriter::finish() {
    if (m_rows != m_height)
      return m_ok = false;

    putU32((m_adlerB << 16) | m_adlerA, true);
    uint32_t crc = ~m_crc;
    putU32(crc, false);
    writeChunk(m_sink, m_ok, "IEND", nullptr, 0);
    return m_ok;
  }


  std::vector<uint8_t> vrEncodePng(const uint8_t* rgba, uint32_t width, uint32_t height) {
    std::vector<uint8_t> png;
    VrPngWriter writer([&png] (const uint8_t* data, size_t size) {
      png.insert(png.end(), data, data + size);
      return true;
    }, width, height);

    for (uint32_t y = 0; y < height; y++)
      writer.row(rgba + size_t(y) * width * 4);

    writer.finish();
    return png;
  }

}
