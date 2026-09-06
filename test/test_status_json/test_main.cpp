#include <cassert>
#include <iostream>
#include <string>

#include "StatusJson.h"

namespace {

void test_serializes_device_status_contract() {
  gpstracker::DeviceStatus status;
  status.gps.locationValid = true;
  status.gps.timeValid = true;
  status.gps.dateValid = true;
  status.gps.latitude = 23.1234567;
  status.gps.longitude = 113.7654321;
  status.gps.altitudeM = 40.5;
  status.gps.speedKmh = 18.4;
  status.gps.satellites = 7;
  status.gps.hdop = 1.2;
  status.gps.fixQuality = 1;
  status.activity.mode = gpstracker::ActivityMode::Cycling;
  status.activity.state = gpstracker::RecordingState::Recording;
  status.activity.elapsedMs = 90000;
  status.activity.distanceM = 1234.5;
  status.activity.currentSpeedKmh = 18.2;
  status.activity.averageSpeedKmh = 16.4;
  status.activity.maxSpeedKmh = 28.8;
  status.activity.currentPaceSecondsPerKm = 197.8;
  status.activity.averagePaceSecondsPerKm = 219.5;
  status.activity.pointCount = 90;
  status.activity.rejectedPoints = 2;
  status.storageReady = true;
  status.activityCount = 3;
  status.freeBytes = 123456;
  status.wifiSsid = "TrackBox-A1B2";

  const std::string json = gpstracker::makeStatusJson(status);
  assert(json ==
         "{\"gpsValid\":true,\"latitude\":23.1234567,"
         "\"longitude\":113.7654321,\"altitudeM\":40.5,"
         "\"gpsSpeedKmh\":18.4,\"satellites\":7,\"hdop\":1.2,"
         "\"mode\":\"cycling\",\"state\":\"recording\","
         "\"elapsedSeconds\":90,\"distanceM\":1234.5,"
         "\"currentSpeedKmh\":18.2,\"averageSpeedKmh\":16.4,"
         "\"maxSpeedKmh\":28.8,\"currentPaceSecondsPerKm\":197.8,"
         "\"averagePaceSecondsPerKm\":219.5,\"pointCount\":90,"
         "\"rejectedPoints\":2,\"storageReady\":true,"
         "\"activityCount\":3,\"freeBytes\":123456,"
         "\"wifiSsid\":\"TrackBox-A1B2\"}");
}

void test_serializes_stored_activity_summary() {
  gpstracker::StoredActivitySummary summary;
  summary.id = 42;
  summary.mode = gpstracker::ActivityMode::Running;
  summary.startedAt = "2026-09-02T08:00:01Z";
  summary.elapsedMs = 1832000;
  summary.distanceM = 5234.6;
  summary.averageSpeedKmh = 10.3;
  summary.maxSpeedKmh = 18.7;
  summary.pointCount = 1750;

  assert(gpstracker::makeActivitySummaryJson(summary) ==
         "{\"id\":42,\"mode\":\"running\","
         "\"startedAt\":\"2026-09-02T08:00:01Z\","
         "\"elapsedSeconds\":1832,\"distanceM\":5234.6,"
         "\"averageSpeedKmh\":10.3,\"maxSpeedKmh\":18.7,"
         "\"pointCount\":1750}");
}

void test_formats_iso_utc_and_escapes_json_text() {
  gpstracker::GpsFix fix;
  fix.dateValid = true;
  fix.timeValid = true;
  fix.year = 2026;
  fix.month = 9;
  fix.day = 2;
  fix.hour = 8;
  fix.minute = 3;
  fix.second = 4;
  assert(gpstracker::formatIsoUtc(fix) == "2026-09-02T08:03:04Z");
  assert(gpstracker::jsonString("A\"B\\C\n") == "\"A\\\"B\\\\C\\n\"");
}

}  // namespace

int main() {
  test_serializes_device_status_contract();
  test_serializes_stored_activity_summary();
  test_formats_iso_utc_and_escapes_json_text();
  std::cout << "3 status JSON tests passed\n";
  return 0;
}
