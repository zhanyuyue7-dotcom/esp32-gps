#include <cassert>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "NmeaParser.h"

namespace {

std::string sentence(const std::string& body) {
  std::uint8_t checksum = 0;
  for (const char value : body) {
    checksum ^= static_cast<std::uint8_t>(value);
  }
  std::ostringstream text;
  text << '$' << body << '*' << std::uppercase << std::hex << std::setw(2)
       << std::setfill('0') << static_cast<int>(checksum) << "\r\n";
  return text.str();
}

gpstracker::ParseResult feed(gpstracker::NmeaParser& parser,
                             const std::string& bytes) {
  gpstracker::ParseResult final = gpstracker::ParseResult::None;
  for (const auto value : bytes) {
    const auto result = parser.push(static_cast<std::uint8_t>(value));
    if (result != gpstracker::ParseResult::None) {
      final = result;
    }
  }
  return final;
}

bool near(const double actual, const double expected,
          const double tolerance = 0.00001) {
  return std::abs(actual - expected) <= tolerance;
}

void test_parses_rmc_location_time_speed_and_course() {
  gpstracker::NmeaParser parser;
  const auto result = feed(
      parser,
      sentence("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,"
               "230394,003.1,W"));

  const auto fix = parser.fix();
  assert(result == gpstracker::ParseResult::Updated);
  assert(fix.locationValid);
  assert(fix.timeValid);
  assert(fix.dateValid);
  assert(near(fix.latitude, 48.1173));
  assert(near(fix.longitude, 11.5166667));
  assert(near(fix.speedKmh, 41.4848, 0.001));
  assert(near(fix.courseDeg, 84.4));
  assert(fix.year == 1994);
  assert(fix.month == 3);
  assert(fix.day == 23);
  assert(fix.hour == 12);
  assert(fix.minute == 35);
  assert(fix.second == 19);
  assert(fix.timestampKey() == 19940323123519ULL);
}

void test_parses_gga_quality_satellites_hdop_and_altitude() {
  gpstracker::NmeaParser parser;
  const auto result = feed(
      parser,
      sentence("GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,"
               "545.4,M,46.9,M,,"));

  const auto fix = parser.fix();
  assert(result == gpstracker::ParseResult::Updated);
  assert(fix.locationValid);
  assert(fix.fixQuality == 1);
  assert(fix.satellites == 8);
  assert(near(fix.hdop, 0.9));
  assert(near(fix.altitudeM, 545.4));
}

void test_accepts_gn_talker_and_negative_hemispheres() {
  gpstracker::NmeaParser parser;
  feed(parser,
       sentence("GNRMC,225446,A,4916.45,S,12311.12,W,000.5,054.7,"
                "191194,020.3,E"));

  const auto fix = parser.fix();
  assert(near(fix.latitude, -49.2741667));
  assert(near(fix.longitude, -123.1853333));
}

void test_rejects_bad_checksum_without_changing_fix() {
  gpstracker::NmeaParser parser;
  feed(parser,
       sentence("GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,"
                "230394,003.1,W"));
  const auto before = parser.fix();
  const auto result = feed(
      parser,
      "$GPRMC,000000,A,0000.000,N,00000.000,E,0,0,010100,,,A*00\r\n");

  assert(result == gpstracker::ParseResult::ChecksumError);
  assert(parser.fix().timestampKey() == before.timestampKey());
  assert(parser.checksumErrors() == 1);
}

void test_marks_void_rmc_and_no_fix_gga_invalid() {
  gpstracker::NmeaParser parser;
  feed(parser,
       sentence("GPRMC,123519,V,4807.038,N,01131.000,E,0,0,230394,,,N"));
  assert(!parser.fix().locationValid);
  feed(parser,
       sentence("GPGGA,123520,4807.038,N,01131.000,E,0,00,99.9,"
                "0.0,M,0.0,M,,"));
  assert(!parser.fix().locationValid);
}

void test_ignores_unneeded_sentence_and_rejects_malformed_sentence() {
  gpstracker::NmeaParser parser;
  assert(feed(parser, sentence("GPVTG,054.7,T,034.4,M,005.5,N,010.2,K")) ==
         gpstracker::ParseResult::Ignored);
  assert(feed(parser, sentence("GPRMC,broken")) ==
         gpstracker::ParseResult::Malformed);
}

}  // namespace

int main() {
  test_parses_rmc_location_time_speed_and_course();
  test_parses_gga_quality_satellites_hdop_and_altitude();
  test_accepts_gn_talker_and_negative_hemispheres();
  test_rejects_bad_checksum_without_changing_fix();
  test_marks_void_rmc_and_no_fix_gga_invalid();
  test_ignores_unneeded_sentence_and_rejects_malformed_sentence();
  std::cout << "6 NMEA parser tests passed\n";
  return 0;
}
