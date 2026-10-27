#include "STi5512_Display.h"
#include "STi5512_RegisterBlocks.h"
#include "STi5512_VideoDecoder.h"
#include "../../core/cpu/omr.h"
#include "../../core/cpu/st20.h"
#include "../../core/memory/memory.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <vector>

/*
 * STi5512 datasheet (7145560 B) section 21.5 and Register Manual (7145562 B) VID/OSD
 * registers. VSYNC top/bottom (VID_ITS 0x63 bits VST and VSB) alternate every PAL field.
 * The OSD walks the linked list of 64-bit big-endian block headers from OSD_TOP (top field,
 * even rows) and OSD_BOT (bottom field, odd rows), each with an optional palette and a
 * contiguous MSB-first bitmap. The MPEG video and still-picture planes are not decoded, so
 * the OSD is mixed over the background colour (BCK_Y/U/V). Regions with the S bit set are
 * left out, as on the composite (TV) output. Screen coordinates use the active-area origin
 * the firmware's STOSD driver uses for PAL: X 118, field line 23.
 */
namespace {
constexpr uint64_t fieldCycles = CPU_CLOCK / 50;
constexpr uint32_t vsyncTop = 0x40;
constexpr uint32_t vsyncBottom = 0x20;
constexpr uint32_t sdramBase = 0xc0000000U;
constexpr int screenWidth = 720;
constexpr int screenHeight = 576;
constexpr int originX = 118;
constexpr int originLine = 23;
constexpr int maxBlocks = 256;

uint64_t nextField = fieldCycles;
bool bottomField = false;

struct Rgb {
  double r, g, b;
};

struct Colour {
  uint8_t y, cb, cr;
  double alpha;
};

uint8_t readByte(uint32_t address) {
  uint32_t value = 0;
  return readBytes(address, 1, &value) ? 0 : static_cast<uint8_t>(value);
}

uint64_t readBigEndian(uint32_t address, int bytes) {
  uint64_t value = 0;
  for (int i = 0; i < bytes; ++i) value = value << 8 | readByte(address + static_cast<uint32_t>(i));
  return value;
}

Rgb toRgb(uint8_t y, uint8_t cb, uint8_t cr) {
  double luma = 1.164 * (y - 16);
  return {luma + 1.596 * (cr - 128), luma - 0.813 * (cr - 128) - 0.392 * (cb - 128), luma + 2.017 * (cb - 128)};
}

Colour paletteColour(uint32_t entry, bool trueColour, bool antiAlias, uint32_t mixWeight, bool first) {
  Colour colour{};
  bool blend;
  if (trueColour) {
    colour = {static_cast<uint8_t>(entry >> 16), static_cast<uint8_t>(entry), static_cast<uint8_t>(entry >> 8), 1.0};
    if (antiAlias) {
      colour.alpha = ((entry >> 24) & 0x3f) / 63.0;
      blend = false;
    } else {
      blend = (entry >> 24) & 1;
    }
  } else {
    uint8_t cb = (entry >> 4) & 0xf;
    uint8_t cr = entry & 0xf;
    colour = {static_cast<uint8_t>(((entry >> 10) & 0x3f) << 2), static_cast<uint8_t>(cb << 4 | cb),
              static_cast<uint8_t>(cr << 4 | cr), 1.0};
    blend = (entry >> 8) & 1;
  }
  if (!antiAlias && blend && mixWeight < 15) colour.alpha = mixWeight / 16.0;
  if (first && colour.y == 0 && colour.cb == 0 && colour.cr == 0) colour.alpha = 0;
  return colour;
}

/* draws one field's OSD list into the frame; returns the number of regions drawn */
int drawField(std::vector<Rgb> &frame, int field, bool largeGranularity) {
  uint32_t block = sdramBase + (sti5512OsdPointer(field) << 8);
  std::array<Colour, 256> palette{};
  int regions = 0;
  for (int count = 0; count < maxBlocks; ++count) {
    uint64_t header = readBigEndian(block, 8);
    uint32_t top = (header >> 48) & 0x1ff;
    uint32_t bottom = (header >> 32) & 0x1ff;
    if (top < 3 || top > 0x140 || bottom < top) break;
    uint32_t mode = (header >> 61) & 7; // M Q E
    bool trueColour = (header >> 60) & 1;
    bool samePalette = (header >> 59) & 1;
    bool antiAlias = (header >> 58) & 1;
    bool hidden = (header >> 57) & 1;
    uint32_t mixWeight = (header >> 44) & 0xf;
    uint32_t next = static_cast<uint32_t>(((header >> 41) & 7) << 4 | ((header >> 26) & 0x3f) << 7 |
                                          ((header >> 10) & 0x3f) << 13);
    int left = static_cast<int>((header >> 16) & 0x3ff);
    int right = static_cast<int>(header & 0x3ff);
    int bpp = 2;
    bool half = false;
    switch (mode) {
      case 0: bpp = 2; break;
      case 6: bpp = 2; half = true; break;
      case 4: bpp = 4; break;
      case 2: bpp = 4; half = true; break;
      case 1: bpp = 8; break;
      case 7: bpp = 8; half = true; break;
      default: return regions;
    }
    uint32_t entries = 1U << bpp;
    uint32_t entryBytes = trueColour ? 4 : 2;
    uint32_t data = block + 8;
    if (!samePalette) {
      for (uint32_t i = 0; i < entries; ++i)
        palette[i] = paletteColour(static_cast<uint32_t>(readBigEndian(data + i * entryBytes, static_cast<int>(entryBytes))),
                                   trueColour, antiAlias, mixWeight, i == 0);
      data += entries * entryBytes;
    }
    int pixels = right - left + 1;
    int stored = half ? pixels / 2 : pixels;
    uint64_t bit = 0;
    for (uint32_t line = top; line <= bottom; ++line) {
      int row = 2 * (static_cast<int>(line) - originLine) + field;
      for (int i = 0; i < stored; ++i, bit += static_cast<uint64_t>(bpp)) {
        uint8_t byte = readByte(data + static_cast<uint32_t>(bit >> 3));
        uint32_t shift = static_cast<uint32_t>(8 - bpp) - static_cast<uint32_t>(bit & 7);
        uint32_t index = (static_cast<uint32_t>(byte) >> shift) & (entries - 1);
        const Colour &colour = palette[index];
        if (hidden || colour.alpha <= 0 || row < 0 || row >= screenHeight) continue;
        Rgb rgb = toRgb(colour.y, colour.cb, colour.cr);
        for (int repeat = 0; repeat < (half ? 2 : 1); ++repeat) {
          int x = left - originX + (half ? 2 * i + repeat : i);
          if (x < 0 || x >= screenWidth) continue;
          Rgb &pixel = frame[static_cast<std::size_t>(row * screenWidth + x)];
          pixel = {pixel.r + (rgb.r - pixel.r) * colour.alpha, pixel.g + (rgb.g - pixel.g) * colour.alpha,
                   pixel.b + (rgb.b - pixel.b) * colour.alpha};
        }
      }
    }
    if (!hidden) ++regions;
    uint32_t end = data + static_cast<uint32_t>((bit + 7) >> 3);
    block = next ? sdramBase + next * (largeGranularity ? 16U : 8U) : (end + 7) & ~7U;
  }
  return regions;
}

void put16(std::vector<uint8_t> &out, uint32_t value) {
  out.push_back(static_cast<uint8_t>(value));
  out.push_back(static_cast<uint8_t>(value >> 8));
}

void put32(std::vector<uint8_t> &out, uint32_t value) {
  put16(out, value & 0xffff);
  put16(out, value >> 16);
}

uint8_t clamp(double value) {
  return static_cast<uint8_t>(std::clamp(value + 0.5, 0.0, 255.0));
}
}

void resetSti5512Display() {
  nextField = elapsedCpuCycles() + fieldCycles;
  bottomField = false;
}

bool tickSti5512Display() {
  if (elapsedCpuCycles() >= nextField) {
    nextField += fieldCycles;
    sti5512VideoDecoderVsync();
    raiseSti5512VideoInterrupt(bottomField ? vsyncBottom : vsyncTop);
    bottomField = !bottomField;
  }
  tickSti5512VideoDecoder();
  return sti5512VideoInterruptLine();
}

std::string writeSti5512Screen(const std::string &path, int *regions) {
  Rgb background = toRgb(sti5512VideoRegister(0x98), sti5512VideoRegister(0x99), sti5512VideoRegister(0x9a));
  std::vector<Rgb> frame(static_cast<std::size_t>(screenWidth * screenHeight), background);
  uint8_t osdConfig = sti5512VideoRegister(0x91);
  int drawn = 0;
  if (osdConfig & 0x04) { // OSD_CFG.ENA
    bool largeGranularity = (osdConfig & 0x10) != 0; // OSD_CFG.LLG
    for (int field = 0; field < 2; ++field) drawn += drawField(frame, field, largeGranularity);
  }
  if (regions) *regions = drawn;

  constexpr uint32_t rowBytes = screenWidth * 3;
  std::vector<uint8_t> out;
  out.push_back('B');
  out.push_back('M');
  put32(out, 54 + rowBytes * screenHeight);
  put32(out, 0);
  put32(out, 54);
  put32(out, 40);
  put32(out, screenWidth);
  put32(out, screenHeight);
  put16(out, 1);
  put16(out, 24);
  put32(out, 0);
  put32(out, rowBytes * screenHeight);
  put32(out, 2835);
  put32(out, 2835);
  put32(out, 0);
  put32(out, 0);
  for (int row = screenHeight - 1; row >= 0; --row) {
    for (int x = 0; x < screenWidth; ++x) {
      const Rgb &pixel = frame[static_cast<std::size_t>(row * screenWidth + x)];
      out.push_back(clamp(pixel.b));
      out.push_back(clamp(pixel.g));
      out.push_back(clamp(pixel.r));
    }
  }
  std::ofstream file(path, std::ios::binary);
  if (!file) return "cannot open " + path;
  file.write(reinterpret_cast<const char *>(out.data()), static_cast<std::streamsize>(out.size()));
  return file ? std::string{} : "cannot write " + path;
}
