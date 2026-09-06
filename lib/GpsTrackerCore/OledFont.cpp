#include "OledFont.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace gpstracker {
namespace {

#include "OledFontData.inc"

struct FontData {
  const std::uint8_t* bitmap;
  std::size_t bitmapSize;
  const OledGlyph* glyphs;
  std::size_t glyphCount;
};

const FontData& fontData(const OledFont font) {
  static constexpr FontData small{kSmallBitmap.data(), kSmallBitmap.size(),
                                  kSmallGlyphs.data(), kSmallGlyphs.size()};
  static constexpr FontData medium{kMediumBitmap.data(), kMediumBitmap.size(),
                                   kMediumGlyphs.data(), kMediumGlyphs.size()};
  static constexpr FontData large{kLargeBitmap.data(), kLargeBitmap.size(),
                                  kLargeGlyphs.data(), kLargeGlyphs.size()};
  switch (font) {
    case OledFont::Medium:
      return medium;
    case OledFont::Large:
      return large;
    case OledFont::Small:
    default:
      return small;
  }
}

const OledGlyph* exactGlyph(const FontData& data,
                            const std::uint32_t codepoint) {
  for (std::size_t index = 0; index < data.glyphCount; ++index) {
    if (data.glyphs[index].codepoint == codepoint) {
      return &data.glyphs[index];
    }
  }
  return nullptr;
}

const OledGlyph* resolvedGlyph(const OledFont font,
                               const std::uint32_t codepoint) {
  const auto& data = fontData(font);
  const auto* glyph = exactGlyph(data, codepoint);
  return glyph != nullptr ? glyph
                          : exactGlyph(data, static_cast<std::uint32_t>('?'));
}

bool continuation(const std::uint8_t value) {
  return (value & 0xC0U) == 0x80U;
}

}  // namespace

const OledGlyph* oledGlyph(const OledFont font,
                           const std::uint32_t codepoint) {
  return exactGlyph(fontData(font), codepoint);
}

bool oledGlyphPixel(const OledFont font, const OledGlyph& glyph, const int x,
                    const int y) {
  if (x < 0 || y < 0 || x >= glyph.width || y >= glyph.height) {
    return false;
  }
  const auto& data = fontData(font);
  const auto bitIndex = static_cast<std::size_t>(y * glyph.width + x);
  const auto byteIndex =
      static_cast<std::size_t>(glyph.bitmapOffset) + bitIndex / 8U;
  if (byteIndex >= data.bitmapSize) {
    return false;
  }
  return (data.bitmap[byteIndex] & (0x80U >> (bitIndex % 8U))) != 0U;
}

std::uint32_t nextOledCodePoint(const char*& text) {
  if (text == nullptr || *text == '\0') {
    return 0U;
  }

  const auto first = static_cast<std::uint8_t>(*text);
  if (first < 0x80U) {
    ++text;
    return first;
  }

  const auto* bytes = reinterpret_cast<const std::uint8_t*>(text);
  std::uint32_t codepoint = 0U;
  int length = 0;
  std::uint32_t minimum = 0U;
  if ((first & 0xE0U) == 0xC0U) {
    codepoint = first & 0x1FU;
    length = 2;
    minimum = 0x80U;
  } else if ((first & 0xF0U) == 0xE0U) {
    codepoint = first & 0x0FU;
    length = 3;
    minimum = 0x800U;
  } else if ((first & 0xF8U) == 0xF0U) {
    codepoint = first & 0x07U;
    length = 4;
    minimum = 0x10000U;
  } else {
    ++text;
    return static_cast<std::uint32_t>('?');
  }

  for (int index = 1; index < length; ++index) {
    if (bytes[index] == 0U || !continuation(bytes[index])) {
      ++text;
      return static_cast<std::uint32_t>('?');
    }
    codepoint = (codepoint << 6U) | (bytes[index] & 0x3FU);
  }
  text += length;
  if (codepoint < minimum || codepoint > 0x10FFFFU ||
      (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
    return static_cast<std::uint32_t>('?');
  }
  return codepoint;
}

int oledTextWidth(const OledFont font, const char* text) {
  int width = 0;
  const char* cursor = text;
  while (true) {
    const auto codepoint = nextOledCodePoint(cursor);
    if (codepoint == 0U) {
      break;
    }
    const auto* glyph = resolvedGlyph(font, codepoint);
    if (glyph != nullptr) {
      width += glyph->advance;
    }
  }
  return width;
}

int oledTextTop(const OledFont font, const char* text) {
  int top = std::numeric_limits<int>::max();
  const char* cursor = text;
  while (true) {
    const auto codepoint = nextOledCodePoint(cursor);
    if (codepoint == 0U) {
      break;
    }
    const auto* glyph = resolvedGlyph(font, codepoint);
    if (glyph != nullptr && glyph->height > 0U && glyph->yOffset < top) {
      top = glyph->yOffset;
    }
  }
  return top == std::numeric_limits<int>::max() ? 0 : top;
}

void renderOledTextPixels(const OledFont font, int x, const int y,
                          const char* text, void* context,
                          const OledPixelSink sink) {
  if (text == nullptr || sink == nullptr) {
    return;
  }

  const int visibleTop = oledTextTop(font, text);
  const char* cursor = text;
  while (true) {
    const auto codepoint = nextOledCodePoint(cursor);
    if (codepoint == 0U) {
      break;
    }
    const auto* glyph = resolvedGlyph(font, codepoint);
    if (glyph == nullptr) {
      continue;
    }
    for (int glyphY = 0; glyphY < glyph->height; ++glyphY) {
      for (int glyphX = 0; glyphX < glyph->width; ++glyphX) {
        if (oledGlyphPixel(font, *glyph, glyphX, glyphY)) {
          sink(context, x + glyph->xOffset + glyphX,
               y + glyph->yOffset - visibleTop + glyphY);
        }
      }
    }
    x += glyph->advance;
  }
}

}  // namespace gpstracker
