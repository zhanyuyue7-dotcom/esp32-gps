#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "NmeaParser.h"

namespace gpstracker {

enum class ActivityMode : std::uint8_t {
  Cycling,
  Running,
};

enum class RecordingState : std::uint8_t {
  Ready,
  Recording,
  Paused,
  Finished,
};

enum class ObserveResult : std::uint8_t {
  Accepted,
  Duplicate,
  WeakFix,
  Stationary,
  Jump,
  NotRecording,
};

struct ActivitySnapshot {
  ActivityMode mode{ActivityMode::Cycling};
  RecordingState state{RecordingState::Ready};
  std::uint64_t elapsedMs{0};
  double distanceM{0.0};
  double currentSpeedKmh{0.0};
  double averageSpeedKmh{0.0};
  double maxSpeedKmh{0.0};
  double currentPaceSecondsPerKm{0.0};
  double averagePaceSecondsPerKm{0.0};
  std::uint32_t pointCount{0};
  std::uint32_t rejectedPoints{0};
};

class ActivityTracker {
 public:
  explicit ActivityTracker(ActivityMode mode = ActivityMode::Cycling)
      : mode_(mode) {}

  ActivityMode mode() const { return mode_; }
  RecordingState state() const { return state_; }
  bool toggleMode();
  bool start(const GpsFix& fix, std::uint64_t nowMs);
  bool pause(std::uint64_t nowMs);
  bool resume(std::uint64_t nowMs);
  bool finish(std::uint64_t nowMs);
  ObserveResult observe(const GpsFix& fix, std::uint64_t nowMs);
  ActivitySnapshot snapshot(std::uint64_t nowMs) const;

 private:
  void addSpeedSample(double speedKmh);
  double smoothedSpeed() const;
  double maxPlausibleSpeedKmh() const;
  std::uint64_t elapsedAt(std::uint64_t nowMs) const;

  ActivityMode mode_{ActivityMode::Cycling};
  RecordingState state_{RecordingState::Ready};
  std::uint64_t activeStartedMs_{0};
  std::uint64_t accumulatedMs_{0};
  std::uint64_t lastAcceptedMs_{0};
  std::uint64_t lastTimestampKey_{0};
  bool hasPrevious_{false};
  double previousLatitude_{0.0};
  double previousLongitude_{0.0};
  double distanceM_{0.0};
  double maxSpeedKmh_{0.0};
  std::array<double, 5> speedSamples_{};
  std::size_t speedSampleCount_{0};
  std::size_t speedSampleIndex_{0};
  std::uint32_t pointCount_{0};
  std::uint32_t rejectedPoints_{0};
};

double distanceMeters(double latitudeA, double longitudeA,
                      double latitudeB, double longitudeB);
const char* activityModeName(ActivityMode mode);
const char* recordingStateName(RecordingState state);

}  // namespace gpstracker
