#pragma once

#include <array>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "OledFont.h"

namespace gpstracker {

class Ssd1306 {
 public:
  bool beginSpi(gpio_num_t mosiPin, gpio_num_t clockPin,
                gpio_num_t resetPin, gpio_num_t dcPin);
  void clear();
  void drawText(int x, int y, const char* text,
                OledFont font = OledFont::Small);
  void drawAsciiText(int x, int y, const char* text, int scale = 1);
  void drawPrimaryText(int x, int y, const char* text);
  void fillRect(int x, int y, int width, int height);
  void drawHorizontalLine(int y);
  bool present();
  bool ready() const { return device_ != nullptr; }

 private:
  bool command(std::uint8_t value);
  bool write(const std::uint8_t* data, std::size_t size, bool isData);
  void drawPixel(int x, int y, bool on);

  spi_device_handle_t device_{nullptr};
  gpio_num_t resetPin_{GPIO_NUM_NC};
  gpio_num_t dcPin_{GPIO_NUM_NC};
  std::array<std::uint8_t, 128 * 8> buffer_{};
};

}  // namespace gpstracker
