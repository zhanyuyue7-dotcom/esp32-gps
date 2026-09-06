#include "NmeaParser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace gpstracker {
namespace {

bool parseUnsigned(const char* text, int& value) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  char* end = nullptr;
  const long parsed = std::strtol(text, &end, 10);
  if (end == text || *end != '\0' || parsed < 0 || parsed > 65535) {
    return false;
  }
  value = static_cast<int>(parsed);
  return true;
}

bool parseDouble(const char* text, double& value) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  char* end = nullptr;
  const double parsed = std::strtod(text, &end);
  if (end == text || *end != '\0' || !std::isfinite(parsed)) {
    return false;
  }
  value = parsed;
  return true;
}

bool parseTime(const char* text, GpsFix& fix) {
  if (text == nullptr || std::strlen(text) < 6) {
    return false;
  }
  char value[3]{text[0], text[1], '\0'};
  char minute[3]{text[2], text[3], '\0'};
  char second[3]{text[4], text[5], '\0'};
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  if (!parseUnsigned(value, hours) || !parseUnsigned(minute, minutes) ||
      !parseUnsigned(second, seconds) || hours > 23 || minutes > 59 ||
      seconds > 60) {
    return false;
  }
  fix.hour = static_cast<std::uint8_t>(hours);
  fix.minute = static_cast<std::uint8_t>(minutes);
  fix.second = static_cast<std::uint8_t>(std::min(seconds, 59));
  fix.timeValid = true;
  return true;
}

bool parseDate(const char* text, GpsFix& fix) {
  if (text == nullptr || std::strlen(text) != 6) {
    return false;
  }
  char day[3]{text[0], text[1], '\0'};
  char month[3]{text[2], text[3], '\0'};
  char year[3]{text[4], text[5], '\0'};
  int parsedDay = 0;
  int parsedMonth = 0;
  int parsedYear = 0;
  if (!parseUnsigned(day, parsedDay) || !parseUnsigned(month, parsedMonth) ||
      !parseUnsigned(year, parsedYear) || parsedDay < 1 || parsedDay > 31 ||
      parsedMonth < 1 || parsedMonth > 12) {
    return false;
  }
  fix.day = static_cast<std::uint8_t>(parsedDay);
  fix.month = static_cast<std::uint8_t>(parsedMonth);
  fix.year = static_cast<std::uint16_t>(parsedYear >= 80 ? 1900 + parsedYear
                                                        : 2000 + parsedYear);
  fix.dateValid = true;
  return true;
}

bool parseCoordinate(const char* text, const char* hemisphere,
                     const bool latitude, double& result) {
  double raw = 0.0;
  if (!parseDouble(text, raw) || hemisphere == nullptr ||
      std::strlen(hemisphere) != 1) {
    return false;
  }
  const double degrees = std::floor(raw / 100.0);
  const double minutes = raw - degrees * 100.0;
  if (minutes < 0.0 || minutes >= 60.0 ||
      degrees > (latitude ? 90.0 : 180.0)) {
    return false;
  }
  result = degrees + minutes / 60.0;
  const char direction = hemisphere[0];
  if (direction == 'S' || direction == 'W') {
    result = -result;
  } else if ((latitude && direction != 'N') ||
             (!latitude && direction != 'E')) {
    return false;
  }
  return true;
}

int hexValue(const char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  return -1;
}

bool endsWith(const char* text, const char* suffix) {
  const auto textLength = std::strlen(text);
  const auto suffixLength = std::strlen(suffix);
  return textLength >= suffixLength &&
         std::strcmp(text + textLength - suffixLength, suffix) == 0;
}

}  // namespace

std::uint64_t GpsFix::timestampKey() const {
  if (!dateValid || !timeValid) {
    return 0;
  }
  std::uint64_t value = year;
  value = value * 100U + month;
  value = value * 100U + day;
  value = value * 100U + hour;
  value = value * 100U + minute;
  value = value * 100U + second;
  return value;
}

bool GpsFix::usable() const {
  return locationValid && timeValid && dateValid && satellites >= 4 &&
         hdop > 0.0 && hdop <= 5.0;
}

ParseResult NmeaParser::push(const std::uint8_t byte) {
  if (byte == '$') {
    collecting_ = true;
    overflowed_ = false;
    length_ = 0;
    line_[length_++] = '$';
    return ParseResult::None;
  }
  if (!collecting_) {
    return ParseResult::None;
  }
  if (byte == '\r') {
    return ParseResult::None;
  }
  if (byte == '\n') {
    collecting_ = false;
    if (overflowed_) {
      ++malformedSentences_;
      return ParseResult::Malformed;
    }
    line_[length_] = '\0';
    return parseLine();
  }
  if (length_ + 1 >= line_.size()) {
    overflowed_ = true;
    return ParseResult::None;
  }
  line_[length_++] = static_cast<char>(byte);
  return ParseResult::None;
}

ParseResult NmeaParser::parseLine() {
  char* star = std::strchr(line_.data(), '*');
  if (length_ < 7 || line_[0] != '$' || star == nullptr ||
      std::strlen(star) != 3) {
    ++malformedSentences_;
    return ParseResult::Malformed;
  }
  std::uint8_t checksum = 0;
  for (char* cursor = line_.data() + 1; cursor < star; ++cursor) {
    checksum ^= static_cast<std::uint8_t>(*cursor);
  }
  const int high = hexValue(star[1]);
  const int low = hexValue(star[2]);
  if (high < 0 || low < 0 || checksum != static_cast<std::uint8_t>(high * 16 + low)) {
    ++checksumErrors_;
    return ParseResult::ChecksumError;
  }

  *star = '\0';
  std::array<char*, 24> fields{};
  std::size_t count = 0;
  char* field = line_.data() + 1;
  fields[count++] = field;
  for (char* cursor = field; *cursor != '\0' && count < fields.size(); ++cursor) {
    if (*cursor == ',') {
      *cursor = '\0';
      fields[count++] = cursor + 1;
    }
  }

  GpsFix next = fix_;
  ParseResult result = ParseResult::Ignored;
  if (endsWith(fields[0], "RMC")) {
    result = parseRmc(fields.data(), count, next);
  } else if (endsWith(fields[0], "GGA")) {
    result = parseGga(fields.data(), count, next);
  }
  if (result == ParseResult::Updated) {
    fix_ = next;
    ++validSentences_;
  } else if (result == ParseResult::Malformed) {
    ++malformedSentences_;
  }
  return result;
}

ParseResult NmeaParser::parseRmc(char** fields, const std::size_t count,
                                 GpsFix& next) const {
  if (count < 10 || fields[1][0] == '\0' || fields[2][0] == '\0') {
    return ParseResult::Malformed;
  }
  if (!parseTime(fields[1], next) || !parseDate(fields[9], next)) {
    return ParseResult::Malformed;
  }
  if (fields[2][0] != 'A') {
    next.locationValid = false;
    next.speedKmh = 0.0;
    return ParseResult::Updated;
  }
  double latitude = 0.0;
  double longitude = 0.0;
  if (!parseCoordinate(fields[3], fields[4], true, latitude) ||
      !parseCoordinate(fields[5], fields[6], false, longitude)) {
    return ParseResult::Malformed;
  }
  double knots = 0.0;
  double course = 0.0;
  if (fields[7][0] != '\0' && !parseDouble(fields[7], knots)) {
    return ParseResult::Malformed;
  }
  if (fields[8][0] != '\0' && !parseDouble(fields[8], course)) {
    return ParseResult::Malformed;
  }
  next.latitude = latitude;
  next.longitude = longitude;
  next.speedKmh = std::max(0.0, knots * 1.852);
  next.courseDeg = course;
  next.locationValid = true;
  return ParseResult::Updated;
}

ParseResult NmeaParser::parseGga(char** fields, const std::size_t count,
                                 GpsFix& next) const {
  if (count < 10) {
    return ParseResult::Malformed;
  }
  int quality = 0;
  int satellites = 0;
  if (!parseTime(fields[1], next) || !parseUnsigned(fields[6], quality) ||
      !parseUnsigned(fields[7], satellites) || quality > 8 || satellites > 99) {
    return ParseResult::Malformed;
  }
  next.fixQuality = static_cast<std::uint8_t>(quality);
  next.satellites = static_cast<std::uint8_t>(satellites);
  if (quality == 0) {
    next.locationValid = false;
    next.hdop = 99.9;
    return ParseResult::Updated;
  }
  double latitude = 0.0;
  double longitude = 0.0;
  double hdop = 0.0;
  double altitude = 0.0;
  if (!parseCoordinate(fields[2], fields[3], true, latitude) ||
      !parseCoordinate(fields[4], fields[5], false, longitude) ||
      !parseDouble(fields[8], hdop) || !parseDouble(fields[9], altitude)) {
    return ParseResult::Malformed;
  }
  next.latitude = latitude;
  next.longitude = longitude;
  next.hdop = hdop;
  next.altitudeM = altitude;
  next.locationValid = true;
  return ParseResult::Updated;
}

}  // namespace gpstracker
