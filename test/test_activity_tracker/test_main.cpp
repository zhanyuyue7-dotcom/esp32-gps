#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

#include "ActivityTracker.h"

namespace {

gpstracker::GpsFix fixAt(const double latitude, const double longitude,
                         const double speedKmh, const std::uint8_t second,
                         const std::uint8_t satellites = 8,
                         const double hdop = 0.9) {
  gpstracker::GpsFix fix;
  fix.locationValid = true;
  fix.timeValid = true;
  fix.dateValid = true;
  fix.latitude = latitude;
  fix.longitude = longitude;
  fix.speedKmh = speedKmh;
  fix.altitudeM = 12.3;
  fix.satellites = satellites;
  fix.hdop = hdop;
  fix.fixQuality = 1;
  fix.year = 2026;
  fix.month = 9;
  fix.day = 2;
  fix.hour = 8;
  fix.minute = 0;
  fix.second = second;
  return fix;
}

void test_requires_a_usable_fix_to_start() {
  gpstracker::ActivityTracker tracker;
  auto weak = fixAt(0.0, 0.0, 0.0, 0, 3);
  assert(!tracker.start(weak, 0));
  assert(tracker.snapshot(0).state == gpstracker::RecordingState::Ready);

  const auto good = fixAt(0.0, 0.0, 0.0, 1);
  assert(tracker.start(good, 1000));
  const auto snapshot = tracker.snapshot(1000);
  assert(snapshot.state == gpstracker::RecordingState::Recording);
  assert(snapshot.pointCount == 1);
}

void test_mode_only_changes_while_idle() {
  gpstracker::ActivityTracker tracker;
  assert(tracker.mode() == gpstracker::ActivityMode::Cycling);
  assert(tracker.toggleMode());
  assert(tracker.mode() == gpstracker::ActivityMode::Running);
  assert(tracker.start(fixAt(0.0, 0.0, 0.0, 1), 1000));
  assert(!tracker.toggleMode());
  assert(tracker.mode() == gpstracker::ActivityMode::Running);
}

void test_filters_duplicates_weak_fixes_stationary_drift_and_jumps() {
  gpstracker::ActivityTracker tracker;
  assert(tracker.start(fixAt(0.0, 0.0, 0.0, 1), 1000));

  assert(tracker.observe(fixAt(0.0, 0.0, 0.0, 1), 1100) ==
         gpstracker::ObserveResult::Duplicate);
  assert(tracker.observe(fixAt(0.0, 0.00001, 0.0, 2, 3), 2000) ==
         gpstracker::ObserveResult::WeakFix);
  assert(tracker.observe(fixAt(0.0, 0.00002, 0.2, 3), 3000) ==
         gpstracker::ObserveResult::Stationary);
  assert(tracker.observe(fixAt(1.0, 1.0, 25.0, 4), 4000) ==
         gpstracker::ObserveResult::Jump);

  const auto snapshot = tracker.snapshot(4000);
  assert(snapshot.distanceM == 0.0);
  assert(snapshot.pointCount == 1);
  assert(snapshot.rejectedPoints == 2);
}

void test_accumulates_distance_speed_and_pace() {
  gpstracker::ActivityTracker tracker;
  assert(tracker.start(fixAt(0.0, 0.0, 0.0, 1), 1000));
  const auto moved = fixAt(0.0, 0.0008993, 36.0, 11);
  assert(tracker.observe(moved, 11000) == gpstracker::ObserveResult::Accepted);

  const auto snapshot = tracker.snapshot(11000);
  assert(snapshot.distanceM > 99.0 && snapshot.distanceM < 101.5);
  assert(snapshot.pointCount == 2);
  assert(snapshot.maxSpeedKmh == 36.0);
  assert(snapshot.currentSpeedKmh > 17.0 && snapshot.currentSpeedKmh < 19.0);
  assert(snapshot.averageSpeedKmh > 35.0 && snapshot.averageSpeedKmh < 37.0);
  assert(snapshot.averagePaceSecondsPerKm > 99.0 &&
         snapshot.averagePaceSecondsPerKm < 101.5);
}

void test_pause_resume_and_finish_exclude_paused_time_and_distance() {
  gpstracker::ActivityTracker tracker;
  assert(tracker.start(fixAt(0.0, 0.0, 10.0, 1), 1000));
  assert(tracker.pause(11000));
  assert(tracker.snapshot(51000).elapsedMs == 10000);
  assert(tracker.observe(fixAt(0.0, 0.01, 10.0, 12), 12000) ==
         gpstracker::ObserveResult::NotRecording);
  assert(tracker.resume(51000));
  assert(tracker.observe(fixAt(0.0, 0.0001, 10.0, 52), 52000) ==
         gpstracker::ObserveResult::Accepted);
  assert(tracker.observe(fixAt(0.0, 0.0002, 10.0, 53), 61000) ==
         gpstracker::ObserveResult::Accepted);
  assert(tracker.finish(61000));

  const auto snapshot = tracker.snapshot(999000);
  assert(snapshot.state == gpstracker::RecordingState::Finished);
  assert(snapshot.elapsedMs == 20000);
  assert(snapshot.distanceM > 10.0 && snapshot.distanceM < 12.0);
  assert(!tracker.resume(1000000));
}

void test_running_rejects_vehicle_speed_that_cycling_accepts() {
  gpstracker::ActivityTracker running;
  running.toggleMode();
  assert(running.start(fixAt(0.0, 0.0, 0.0, 1), 1000));
  assert(running.observe(fixAt(0.0, 0.0002, 80.0, 2), 2000) ==
         gpstracker::ObserveResult::Jump);

  gpstracker::ActivityTracker cycling;
  assert(cycling.start(fixAt(0.0, 0.0, 0.0, 1), 1000));
  assert(cycling.observe(fixAt(0.0, 0.0002, 80.0, 2), 2000) ==
         gpstracker::ObserveResult::Accepted);
}

void test_suppresses_low_speed_stationary_drift() {
  gpstracker::ActivityTracker tracker;
  assert(tracker.start(fixAt(23.0, 113.0, 0.0, 1), 1000));
  const auto jitter = fixAt(23.0, 113.000002, 1.2, 2);
  assert(tracker.observe(jitter, 2000) ==
         gpstracker::ObserveResult::Stationary);
  const auto snapshot = tracker.snapshot(2000);
  assert(snapshot.distanceM == 0.0);
  assert(snapshot.currentSpeedKmh == 0.0);
}

}  // namespace

int main() {
  test_requires_a_usable_fix_to_start();
  test_mode_only_changes_while_idle();
  test_filters_duplicates_weak_fixes_stationary_drift_and_jumps();
  test_accumulates_distance_speed_and_pace();
  test_pause_resume_and_finish_exclude_paused_time_and_distance();
  test_running_rejects_vehicle_speed_that_cycling_accepts();
  test_suppresses_low_speed_stationary_drift();
  std::cout << "7 activity tracker tests passed\n";
  return 0;
}
