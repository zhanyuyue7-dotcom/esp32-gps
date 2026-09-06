#include "OledLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "OledFont.h"

namespace gpstracker {
namespace {

void formatElapsed(const std::uint64_t elapsedMs, char* output,
                   const std::size_t size) {
  const auto totalSeconds = elapsedMs / 1000U;
  const auto hours = std::min<std::uint64_t>(99, totalSeconds / 3600U);
  const auto minutes = (totalSeconds % 3600U) / 60U;
  const auto seconds = totalSeconds % 60U;
  std::snprintf(output, size, "%02llu:%02llu:%02llu",
                static_cast<unsigned long long>(hours),
                static_cast<unsigned long long>(minutes),
                static_cast<unsigned long long>(seconds));
}

void setPrimary(OledLayout& layout, const char* value, const char* unit,
                const bool numeric) {
  std::snprintf(layout.primary.data(), layout.primary.size(), "%s", value);
  std::snprintf(layout.unit.data(), layout.unit.size(), "%s", unit);
  layout.primaryNumeric = numeric;
  layout.primaryWidth = oledPrimaryTextWidth(layout.primary.data(), numeric);
  layout.primaryX = std::max(0, (128 - layout.primaryWidth) / 2);
}

const char* modeLabel(const ActivityMode mode) {
  return mode == ActivityMode::Running ? "RUN" : "BIKE";
}

}  // namespace

int oledPrimaryTextWidth(const char* text, const bool numeric) {
  if (text == nullptr || *text == '\0') {
    return 0;
  }
  if (!numeric) {
    return static_cast<int>(std::strlen(text)) * 12 - 2;
  }
  int width = 0;
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    if (*cursor == '.') {
      width += 5;
    } else if (*cursor == ':') {
      width += 6;
    } else {
      const char glyph[2]{*cursor, '\0'};
      width += oledTextWidth(OledFont::Large, glyph);
    }
  }
  return width;
}

OledLayout makeOledLayout(const OledInput& input) {
  OledLayout layout;
  char elapsed[16]{};
  formatElapsed(input.activity.elapsedMs, elapsed, sizeof(elapsed));

  switch (input.screen) {
    case OledScreen::Searching: {
      std::snprintf(layout.title.data(), layout.title.size(), "GPS SEARCH");
      char satellites[16]{};
      std::snprintf(satellites, sizeof(satellites), "SAT %02u",
                    static_cast<unsigned>(input.satellites));
      setPrimary(layout, satellites, "", false);
      std::snprintf(layout.footer.data(), layout.footer.size(),
                    "GO OUTSIDE - FACE SKY");
      break;
    }
    case OledScreen::Ready:
      std::snprintf(layout.title.data(), layout.title.size(), "GPS READY");
      setPrimary(layout, modeLabel(input.activity.mode), "", false);
      std::snprintf(layout.footer.data(), layout.footer.size(),
                    "A START  B MODE");
      break;
    case OledScreen::Recording: {
      std::snprintf(layout.title.data(), layout.title.size(), "REC %-4s SAT %02u",
                    modeLabel(input.activity.mode),
                    static_cast<unsigned>(input.satellites));
      char primary[16]{};
      if (input.activity.mode == ActivityMode::Running) {
        const auto pace = static_cast<unsigned>(std::round(
            input.activity.currentPaceSecondsPerKm > 0.0
                ? input.activity.currentPaceSecondsPerKm
                : 0.0));
        if (pace == 0 || pace >= 6000) {
          std::snprintf(primary, sizeof(primary), "--:--");
        } else {
          std::snprintf(primary, sizeof(primary), "%u:%02u", pace / 60U,
                        pace % 60U);
        }
        setPrimary(layout, primary, "/KM", true);
      } else {
        std::snprintf(primary, sizeof(primary), "%.0f",
                      input.activity.currentSpeedKmh);
        setPrimary(layout, primary, "KM/H", true);
      }
      std::snprintf(layout.footer.data(), layout.footer.size(), "%.2fKM  %s",
                    input.activity.distanceM / 1000.0, elapsed);
      break;
    }
    case OledScreen::Paused: {
      std::snprintf(layout.title.data(), layout.title.size(), "PAUSED");
      char distance[16]{};
      std::snprintf(distance, sizeof(distance), "%.2f",
                    input.activity.distanceM / 1000.0);
      setPrimary(layout, distance, "KM", true);
      std::snprintf(layout.footer.data(), layout.footer.size(),
                    "A RESUME  HOLD END");
      break;
    }
    case OledScreen::Summary: {
      std::snprintf(layout.title.data(), layout.title.size(), "SAVED");
      char distance[16]{};
      std::snprintf(distance, sizeof(distance), "%.2f",
                    input.activity.distanceM / 1000.0);
      setPrimary(layout, distance, "KM", true);
      std::snprintf(layout.footer.data(), layout.footer.size(), "%s %s",
                    modeLabel(input.activity.mode), elapsed);
      break;
    }
    case OledScreen::GpsLost:
      std::snprintf(layout.title.data(), layout.title.size(), "GPS LOST");
      setPrimary(layout, "SAT 00", "", false);
      std::snprintf(layout.footer.data(), layout.footer.size(),
                    "RECORDING - WAIT FIX");
      break;
    case OledScreen::StorageError:
    default:
      std::snprintf(layout.title.data(), layout.title.size(), "STORAGE ERROR");
      setPrimary(layout, "FULL", "", false);
      std::snprintf(layout.footer.data(), layout.footer.size(),
                    "DOWNLOAD AND DELETE");
      break;
  }
  return layout;
}

}  // namespace gpstracker
