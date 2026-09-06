#pragma once

#include <cstdint>
#include <string>

#include "ActivityTracker.h"
#include "NmeaParser.h"

namespace gpstracker {

struct DeviceStatus {
  GpsFix gps{};
  ActivitySnapshot activity{};
  bool storageReady{false};
  std::uint32_t activityCount{0};
  std::uint64_t freeBytes{0};
  std::string wifiSsid{};
};

struct StoredActivitySummary {
  std::uint32_t id{0};
  ActivityMode mode{ActivityMode::Cycling};
  std::string startedAt{};
  std::uint64_t elapsedMs{0};
  double distanceM{0.0};
  double averageSpeedKmh{0.0};
  double maxSpeedKmh{0.0};
  std::uint32_t pointCount{0};
};

std::string jsonString(const std::string& value);
std::string formatIsoUtc(const GpsFix& fix);
std::string makeStatusJson(const DeviceStatus& status);
std::string makeActivitySummaryJson(const StoredActivitySummary& summary);

}  // namespace gpstracker
