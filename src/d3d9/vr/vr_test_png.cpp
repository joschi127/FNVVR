#include "vr_test_png.h"

#include <algorithm>
#include <cstdio>

namespace dxvk {

  namespace {

    uint32_t crc32(const uint8_t* data, size_t size, uint32_t crc = 0) {
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

      crc = ~crc;

      for (size_t i = 0; i < size; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);

      return ~crc;
    }

    void putU32(std::vector<uint8_t>& out, uint32_t v) {
      out.push_back(uint8_t(v >> 24));
      out.push_back(uint8_t(v >> 16));
      out.push_back(uint8_t(v >> 8));
      out.push_back(uint8_t(v));
    }

    void putChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data) {
      putU32(out, uint32_t(data.size()));
      size_t start = out.size();
      out.insert(out.end(), type, type + 4);
      out.insert(out.end(), data.begin(), data.end());
      putU32(out, crc32(&out[start], out.size() - start));
    }

  }


  std::vector<uint8_t> vrEncodePng(const uint8_t* rgba, uint32_t width, uint32_t height) {
    std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

    std::vector<uint8_t> header;
    putU32(header, width);
    putU32(header, height);
    header.insert(header.end(), { 8, 6, 0, 0, 0 });  // 8 bits, RGBA, deflate, no filter, no interlace
    putChunk(png, "IHDR", header);

    // Raw scanlines: a filter byte (0, none) before each row
    size_t rowBytes = size_t(width) * 4;
    std::vector<uint8_t> raw;
    raw.reserve((rowBytes + 1) * height);

    for (uint32_t y = 0; y < height; y++) {
      raw.push_back(0);
      raw.insert(raw.end(), rgba + y * rowBytes, rgba + (y + 1) * rowBytes);
    }

    // zlib stream of stored deflate blocks (at most 65535 bytes each)
    std::vector<uint8_t> z = { 0x78, 0x01 };
    size_t pos = 0;

    do {
      size_t len = std::min<size_t>(65535, raw.size() - pos);
      bool last = pos + len == raw.size();
      z.push_back(last ? 1 : 0);
      z.push_back(uint8_t(len));
      z.push_back(uint8_t(len >> 8));
      z.push_back(uint8_t(~len));
      z.push_back(uint8_t(~len >> 8));
      z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
      pos += len;
    } while (pos < raw.size());

    uint32_t a = 1, b = 0;

    for (uint8_t byte : raw) {
      a = (a + byte) % 65521;
      b = (b + a) % 65521;
    }

    putU32(z, (b << 16) | a);
    putChunk(png, "IDAT", z);
    putChunk(png, "IEND", { });
    return png;
  }


  bool vrWritePng(const std::string& path, const uint8_t* rgba, uint32_t width, uint32_t height) {
    std::vector<uint8_t> png = vrEncodePng(rgba, width, height);
    FILE* file = std::fopen(path.c_str(), "wb");

    if (!file)
      return false;

    bool ok = std::fwrite(png.data(), 1, png.size(), file) == png.size();
    ok = std::fclose(file) == 0 && ok;
    return ok;
  }

}
