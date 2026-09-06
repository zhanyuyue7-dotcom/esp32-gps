#pragma once

#include <array>
#include <cstdint>

#include "ActivityTracker.h"

namespace gpstracker {

enum class OledScreen : std::uint8_t {
  Searching,
  Ready,
  Recording,
  Paused,
  Summary,
  GpsLost,
  StorageError,
};

struct OledInput {
  OledScreen screen{OledScreen::Searching};
  bool gpsValid{false};
  std::uint8_t satellites{0};
  ActivitySnapshot activity{};
};

struct OledLayout {
  std::array<char, 32> title{};
  std::array<char, 16> primary{};
  std::array<char, 8> unit{};
  std::array<char, 32> footer{};
  bool primaryNumeric{false};
  int primaryX{0};
  int primaryWidth{0};
};

int oledPrimaryTextWidth(const char* text, bool numeric);
OledLayout makeOledLayout(const OledInput& input);

}  // namespace gpstracker
