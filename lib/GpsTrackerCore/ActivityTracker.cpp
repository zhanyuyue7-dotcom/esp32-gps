#include "ActivityTracker.h"

#include <algorithm>
#include <cmath>

namespace gpstracker {
namespace {

constexpr double kEarthRadiusM = 6371000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kStationarySpeedKmh = 1.5;

double radians(const double degrees) { return degrees * kPi / 180.0; }

}  // namespace

double distanceMeters(const double latitudeA, const double longitudeA,
                      const double latitudeB, const double longitudeB) {
  const double latitudeDelta = radians(latitudeB - latitudeA);
  const double longitudeDelta = radians(longitudeB - longitudeA);
  const double a = std::sin(latitudeDelta / 2.0) *
                       std::sin(latitudeDelta / 2.0) +
                   std::cos(radians(latitudeA)) *
                       std::cos(radians(latitudeB)) *
                       std::sin(longitudeDelta / 2.0) *
                       std::sin(longitudeDelta / 2.0);
  return kEarthRadiusM * 2.0 *
         std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
}

const char* activityModeName(const ActivityMode mode) {
  return mode == ActivityMode::Running ? "running" : "cycling";
}

const char* recordingStateName(const RecordingState state) {
  switch (state) {
    case RecordingState::Recording:
      return "recording";
    case RecordingState::Paused:
      return "paused";
    case RecordingState::Finished:
      return "finished";
    case RecordingState::Ready:
    default:
      return "ready";
  }
}

bool ActivityTracker::toggleMode() {
  if (state_ != RecordingState::Ready &&
      state_ != RecordingState::Finished) {
    return false;
  }
  mode_ = mode_ == ActivityMode::Cycling ? ActivityMode::Running
                                         : ActivityMode::Cycling;
  if (state_ == RecordingState::Finished) {
    state_ = RecordingState::Ready;
  }
  return true;
}

bool ActivityTracker::start(const GpsFix& fix, const std::uint64_t nowMs) {
  if ((state_ != RecordingState::Ready &&
       state_ != RecordingState::Finished) ||
      !fix.usable()) {
    return false;
  }
  state_ = RecordingState::Recording;
  activeStartedMs_ = nowMs;
  accumulatedMs_ = 0;
  lastAcceptedMs_ = nowMs;
  lastTimestampKey_ = fix.timestampKey();
  hasPrevious_ = true;
  previousLatitude_ = fix.latitude;
  previousLongitude_ = fix.longitude;
  distanceM_ = 0.0;
  maxSpeedKmh_ = fix.speedKmh < kStationarySpeedKmh ? 0.0 : fix.speedKmh;
  speedSamples_.fill(0.0);
  speedSampleCount_ = 0;
  speedSampleIndex_ = 0;
  pointCount_ = 1;
  rejectedPoints_ = 0;
  addSpeedSample(fix.speedKmh);
  return true;
}

bool ActivityTracker::pause(const std::uint64_t nowMs) {
  if (state_ != RecordingState::Recording) {
    return false;
  }
  accumulatedMs_ += nowMs >= activeStartedMs_ ? nowMs - activeStartedMs_ : 0;
  state_ = RecordingState::Paused;
  return true;
}

bool ActivityTracker::resume(const std::uint64_t nowMs) {
  if (state_ != RecordingState::Paused) {
    return false;
  }
  state_ = RecordingState::Recording;
  activeStartedMs_ = nowMs;
  hasPrevious_ = false;
  lastTimestampKey_ = 0;
  return true;
}

bool ActivityTracker::finish(const std::uint64_t nowMs) {
  if (state_ != RecordingState::Recording &&
      state_ != RecordingState::Paused) {
    return false;
  }
  if (state_ == RecordingState::Recording) {
    accumulatedMs_ += nowMs >= activeStartedMs_ ? nowMs - activeStartedMs_ : 0;
  }
  state_ = RecordingState::Finished;
  return true;
}

ObserveResult ActivityTracker::observe(const GpsFix& fix,
                                       const std::uint64_t nowMs) {
  if (state_ != RecordingState::Recording) {
    return ObserveResult::NotRecording;
  }
  if (!fix.usable()) {
    ++rejectedPoints_;
    return ObserveResult::WeakFix;
  }
  const auto timestamp = fix.timestampKey();
  if (timestamp == 0 || timestamp == lastTimestampKey_) {
    return ObserveResult::Duplicate;
  }
  if (!hasPrevious_) {
    hasPrevious_ = true;
    previousLatitude_ = fix.latitude;
    previousLongitude_ = fix.longitude;
    lastAcceptedMs_ = nowMs;
    lastTimestampKey_ = timestamp;
    ++pointCount_;
    addSpeedSample(fix.speedKmh);
    maxSpeedKmh_ = std::max(
        maxSpeedKmh_, fix.speedKmh < kStationarySpeedKmh ? 0.0 : fix.speedKmh);
    return ObserveResult::Accepted;
  }

  const double segmentM = distanceMeters(previousLatitude_, previousLongitude_,
                                         fix.latitude, fix.longitude);
  const double deltaSeconds =
      nowMs > lastAcceptedMs_ ? (nowMs - lastAcceptedMs_) / 1000.0 : 0.0;
  const double impliedSpeedKmh =
      deltaSeconds > 0.0 ? segmentM / deltaSeconds * 3.6 : 1e9;
  if (impliedSpeedKmh > maxPlausibleSpeedKmh() ||
      fix.speedKmh > maxPlausibleSpeedKmh()) {
    ++rejectedPoints_;
    return ObserveResult::Jump;
  }

  if (fix.speedKmh < kStationarySpeedKmh && segmentM < 15.0) {
    previousLatitude_ = fix.latitude;
    previousLongitude_ = fix.longitude;
    lastAcceptedMs_ = nowMs;
    lastTimestampKey_ = timestamp;
    addSpeedSample(0.0);
    return ObserveResult::Stationary;
  }

  distanceM_ += segmentM;
  previousLatitude_ = fix.latitude;
  previousLongitude_ = fix.longitude;
  lastAcceptedMs_ = nowMs;
  lastTimestampKey_ = timestamp;
  ++pointCount_;
  addSpeedSample(fix.speedKmh);
  maxSpeedKmh_ = std::max(
      maxSpeedKmh_, fix.speedKmh < kStationarySpeedKmh ? 0.0 : fix.speedKmh);
  return ObserveResult::Accepted;
}

ActivitySnapshot ActivityTracker::snapshot(const std::uint64_t nowMs) const {
  ActivitySnapshot value;
  value.mode = mode_;
  value.state = state_;
  value.elapsedMs = elapsedAt(nowMs);
  value.distanceM = distanceM_;
  value.currentSpeedKmh = smoothedSpeed();
  value.maxSpeedKmh = maxSpeedKmh_;
  value.pointCount = pointCount_;
  value.rejectedPoints = rejectedPoints_;
  if (value.elapsedMs > 0 && distanceM_ > 0.0) {
    value.averageSpeedKmh = distanceM_ * 3600.0 / value.elapsedMs;
    value.averagePaceSecondsPerKm =
        (value.elapsedMs / 1000.0) / (distanceM_ / 1000.0);
  }
  if (value.currentSpeedKmh >= 0.5) {
    value.currentPaceSecondsPerKm = 3600.0 / value.currentSpeedKmh;
  }
  return value;
}

void ActivityTracker::addSpeedSample(const double speedKmh) {
  speedSamples_[speedSampleIndex_] =
      speedKmh < kStationarySpeedKmh ? 0.0 : speedKmh;
  speedSampleIndex_ = (speedSampleIndex_ + 1) % speedSamples_.size();
  speedSampleCount_ = std::min(speedSampleCount_ + 1, speedSamples_.size());
}

double ActivityTracker::smoothedSpeed() const {
  if (speedSampleCount_ == 0) {
    return 0.0;
  }
  double total = 0.0;
  for (std::size_t index = 0; index < speedSampleCount_; ++index) {
    total += speedSamples_[index];
  }
  return total / static_cast<double>(speedSampleCount_);
}

double ActivityTracker::maxPlausibleSpeedKmh() const {
  return mode_ == ActivityMode::Running ? 45.0 : 120.0;
}

std::uint64_t ActivityTracker::elapsedAt(const std::uint64_t nowMs) const {
  if (state_ == RecordingState::Recording) {
    return accumulatedMs_ +
           (nowMs >= activeStartedMs_ ? nowMs - activeStartedMs_ : 0);
  }
  return accumulatedMs_;
}

}  // namespace gpstracker
