#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gpstracker {

struct GpsFix {
  bool locationValid{false};
  bool timeValid{false};
  bool dateValid{false};
  double latitude{0.0};
  double longitude{0.0};
  double altitudeM{0.0};
  double speedKmh{0.0};
  double courseDeg{0.0};
  double hdop{99.9};
  std::uint8_t satellites{0};
  std::uint8_t fixQuality{0};
  std::uint16_t year{0};
  std::uint8_t month{0};
  std::uint8_t day{0};
  std::uint8_t hour{0};
  std::uint8_t minute{0};
  std::uint8_t second{0};

  std::uint64_t timestampKey() const;
  bool usable() const;
};

enum class ParseResult : std::uint8_t {
  None,
  Updated,
  Ignored,
  ChecksumError,
  Malformed,
};

class NmeaParser {
 public:
  ParseResult push(std::uint8_t byte);
  const GpsFix& fix() const { return fix_; }
  std::uint32_t validSentences() const { return validSentences_; }
  std::uint32_t checksumErrors() const { return checksumErrors_; }
  std::uint32_t malformedSentences() const { return malformedSentences_; }

 private:
  ParseResult parseLine();
  ParseResult parseRmc(char** fields, std::size_t count, GpsFix& next) const;
  ParseResult parseGga(char** fields, std::size_t count, GpsFix& next) const;

  std::array<char, 128> line_{};
  std::size_t length_{0};
  bool collecting_{false};
  bool overflowed_{false};
  GpsFix fix_{};
  std::uint32_t validSentences_{0};
  std::uint32_t checksumErrors_{0};
  std::uint32_t malformedSentences_{0};
};

}  // namespace gpstracker
