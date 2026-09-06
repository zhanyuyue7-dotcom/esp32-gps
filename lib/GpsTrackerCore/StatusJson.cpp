#include "StatusJson.h"

#include <iomanip>
#include <locale>
#include <sstream>

namespace gpstracker {
namespace {

void appendFixed(std::ostringstream& output, const double value,
                 const int precision) {
  output << std::fixed << std::setprecision(precision) << value;
}

}  // namespace

std::string jsonString(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back('"');
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (character < 0x20U) {
          escaped += '?';
        } else {
          escaped.push_back(static_cast<char>(character));
        }
        break;
    }
  }
  escaped.push_back('"');
  return escaped;
}

std::string formatIsoUtc(const GpsFix& fix) {
  if (!fix.dateValid || !fix.timeValid) {
    return {};
  }
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::setfill('0') << std::setw(4) << fix.year << '-'
         << std::setw(2) << static_cast<unsigned>(fix.month) << '-'
         << std::setw(2) << static_cast<unsigned>(fix.day) << 'T'
         << std::setw(2) << static_cast<unsigned>(fix.hour) << ':'
         << std::setw(2) << static_cast<unsigned>(fix.minute) << ':'
         << std::setw(2) << static_cast<unsigned>(fix.second) << 'Z';
  return output.str();
}

std::string makeStatusJson(const DeviceStatus& status) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << "{\"gpsValid\":" << (status.gps.usable() ? "true" : "false")
         << ",\"latitude\":";
  appendFixed(output, status.gps.latitude, 7);
  output << ",\"longitude\":";
  appendFixed(output, status.gps.longitude, 7);
  output << ",\"altitudeM\":";
  appendFixed(output, status.gps.altitudeM, 1);
  output << ",\"gpsSpeedKmh\":";
  appendFixed(output, status.gps.speedKmh, 1);
  output << ",\"satellites\":" << static_cast<unsigned>(status.gps.satellites)
         << ",\"hdop\":";
  appendFixed(output, status.gps.hdop, 1);
  output << ",\"mode\":" << jsonString(activityModeName(status.activity.mode))
         << ",\"state\":"
         << jsonString(recordingStateName(status.activity.state))
         << ",\"elapsedSeconds\":" << status.activity.elapsedMs / 1000U
         << ",\"distanceM\":";
  appendFixed(output, status.activity.distanceM, 1);
  output << ",\"currentSpeedKmh\":";
  appendFixed(output, status.activity.currentSpeedKmh, 1);
  output << ",\"averageSpeedKmh\":";
  appendFixed(output, status.activity.averageSpeedKmh, 1);
  output << ",\"maxSpeedKmh\":";
  appendFixed(output, status.activity.maxSpeedKmh, 1);
  output << ",\"currentPaceSecondsPerKm\":";
  appendFixed(output, status.activity.currentPaceSecondsPerKm, 1);
  output << ",\"averagePaceSecondsPerKm\":";
  appendFixed(output, status.activity.averagePaceSecondsPerKm, 1);
  output << ",\"pointCount\":" << status.activity.pointCount
         << ",\"rejectedPoints\":" << status.activity.rejectedPoints
         << ",\"storageReady\":"
         << (status.storageReady ? "true" : "false")
         << ",\"activityCount\":" << status.activityCount
         << ",\"freeBytes\":" << status.freeBytes << ",\"wifiSsid\":"
         << jsonString(status.wifiSsid) << '}';
  return output.str();
}

std::string makeActivitySummaryJson(const StoredActivitySummary& summary) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << "{\"id\":" << summary.id << ",\"mode\":"
         << jsonString(activityModeName(summary.mode)) << ",\"startedAt\":"
         << jsonString(summary.startedAt) << ",\"elapsedSeconds\":"
         << summary.elapsedMs / 1000U << ",\"distanceM\":";
  appendFixed(output, summary.distanceM, 1);
  output << ",\"averageSpeedKmh\":";
  appendFixed(output, summary.averageSpeedKmh, 1);
  output << ",\"maxSpeedKmh\":";
  appendFixed(output, summary.maxSpeedKmh, 1);
  output << ",\"pointCount\":" << summary.pointCount << '}';
  return output.str();
}

}  // namespace gpstracker
