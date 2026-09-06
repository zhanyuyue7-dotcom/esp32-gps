#include <cassert>
#include <cstring>
#include <iostream>

#include "OledLayout.h"

namespace {

gpstracker::OledInput recordingInput(const gpstracker::ActivityMode mode) {
  gpstracker::OledInput input;
  input.screen = gpstracker::OledScreen::Recording;
  input.gpsValid = true;
  input.satellites = 8;
  input.activity.mode = mode;
  input.activity.state = gpstracker::RecordingState::Recording;
  input.activity.elapsedMs = 300000;
  input.activity.distanceM = 1234.0;
  input.activity.currentSpeedKmh = mode == gpstracker::ActivityMode::Cycling
                                       ? 35.6
                                       : 10.52;
  input.activity.currentPaceSecondsPerKm = 342.0;
  return input;
}

void assertFits(const gpstracker::OledLayout& layout) {
  assert(layout.primaryX >= 0);
  assert(layout.primaryWidth > 0);
  assert(layout.primaryX + layout.primaryWidth <= 128);
  assert(std::strlen(layout.title.data()) < layout.title.size());
  assert(std::strlen(layout.footer.data()) < layout.footer.size());
}

void test_formats_cycling_recording_screen() {
  const auto layout = gpstracker::makeOledLayout(
      recordingInput(gpstracker::ActivityMode::Cycling));
  assert(std::strcmp(layout.title.data(), "REC BIKE SAT 08") == 0);
  assert(std::strcmp(layout.primary.data(), "36") == 0);
  assert(std::strcmp(layout.unit.data(), "KM/H") == 0);
  assert(std::strcmp(layout.footer.data(), "1.23KM  00:05:00") == 0);
  assert(layout.primaryNumeric);
  assertFits(layout);
}

void test_formats_running_pace_screen() {
  const auto layout = gpstracker::makeOledLayout(
      recordingInput(gpstracker::ActivityMode::Running));
  assert(std::strcmp(layout.title.data(), "REC RUN  SAT 08") == 0);
  assert(std::strcmp(layout.primary.data(), "5:42") == 0);
  assert(std::strcmp(layout.unit.data(), "/KM") == 0);
  assert(layout.primaryNumeric);
  assertFits(layout);
}

void test_formats_search_ready_pause_summary_and_error_states() {
  gpstracker::OledInput input;
  input.screen = gpstracker::OledScreen::Searching;
  input.satellites = 3;
  auto layout = gpstracker::makeOledLayout(input);
  assert(std::strcmp(layout.title.data(), "GPS SEARCH") == 0);
  assert(std::strcmp(layout.primary.data(), "SAT 03") == 0);
  assert(!layout.primaryNumeric);
  assertFits(layout);

  input.screen = gpstracker::OledScreen::Ready;
  input.gpsValid = true;
  layout = gpstracker::makeOledLayout(input);
  assert(std::strcmp(layout.title.data(), "GPS READY") == 0);
  assert(std::strcmp(layout.primary.data(), "BIKE") == 0);
  assert(std::strstr(layout.footer.data(), "A START") != nullptr);
  assertFits(layout);

  input.screen = gpstracker::OledScreen::Paused;
  input.activity.distanceM = 3210.0;
  layout = gpstracker::makeOledLayout(input);
  assert(std::strcmp(layout.title.data(), "PAUSED") == 0);
  assert(std::strcmp(layout.primary.data(), "3.21") == 0);
  assertFits(layout);

  input.screen = gpstracker::OledScreen::Summary;
  layout = gpstracker::makeOledLayout(input);
  assert(std::strcmp(layout.title.data(), "SAVED") == 0);
  assert(std::strcmp(layout.unit.data(), "KM") == 0);
  assertFits(layout);

  input.screen = gpstracker::OledScreen::StorageError;
  layout = gpstracker::makeOledLayout(input);
  assert(std::strcmp(layout.title.data(), "STORAGE ERROR") == 0);
  assert(std::strcmp(layout.primary.data(), "FULL") == 0);
  assertFits(layout);
}

void test_primary_width_handles_decimal_and_colon() {
  assert(gpstracker::oledPrimaryTextWidth("35.6", true) < 128);
  assert(gpstracker::oledPrimaryTextWidth("59:59", true) < 128);
  assert(gpstracker::oledPrimaryTextWidth("GPS LOST", false) < 128);
}

}  // namespace

int main() {
  test_formats_cycling_recording_screen();
  test_formats_running_pace_screen();
  test_formats_search_ready_pause_summary_and_error_states();
  test_primary_width_handles_decimal_and_colon();
  std::cout << "4 OLED layout tests passed\n";
  return 0;
}
