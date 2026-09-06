#pragma once

#include <cstdint>

namespace gpstracker {

enum class OledFont : std::uint8_t {
  Small,
  Medium,
  Large,
};

struct OledGlyph {
  std::uint32_t codepoint{0};
  std::uint16_t bitmapOffset{0};
  std::uint8_t width{0};
  std::uint8_t height{0};
  std::uint8_t advance{0};
  std::int8_t xOffset{0};
  std::int8_t yOffset{0};
};

using OledPixelSink = void (*)(void* context, int x, int y);

const OledGlyph* oledGlyph(OledFont font, std::uint32_t codepoint);
bool oledGlyphPixel(OledFont font, const OledGlyph& glyph, int x, int y);
std::uint32_t nextOledCodePoint(const char*& text);
int oledTextWidth(OledFont font, const char* text);
int oledTextTop(OledFont font, const char* text);
void renderOledTextPixels(OledFont font, int x, int y, const char* text,
                          void* context, OledPixelSink sink);

}  // namespace gpstracker
