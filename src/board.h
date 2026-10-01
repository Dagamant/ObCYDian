#pragma once
// Pin map and display/touch driver config for the LCDWiki 3.5" ESP32-32E board (E32R35T).

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

namespace pins {
// LCD + touch share the HSPI bus
constexpr int SPI_SCK = 14;
constexpr int SPI_MOSI = 13;
constexpr int SPI_MISO = 12;
constexpr int LCD_CS = 15;
constexpr int LCD_DC = 2;
constexpr int LCD_BL = 27;
constexpr int TOUCH_CS = 33;
constexpr int TOUCH_IRQ = 36;
// microSD on VSPI
constexpr int SD_CS = 5;
constexpr int SD_SCK = 18;
constexpr int SD_MOSI = 23;
constexpr int SD_MISO = 19;
// RGB LED (active low)
constexpr int LED_R = 22;
constexpr int LED_G = 16;
constexpr int LED_B = 17;
// Misc
constexpr int BOOT_BTN = 0;
constexpr int AUDIO_EN = 4;
constexpr int AUDIO_DAC = 26;
constexpr int BAT_ADC = 34;
}  // namespace pins

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM light_;
  lgfx::Touch_XPT2046 touch_;

 public:
  LGFX() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = HSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = pins::SPI_SCK;
      cfg.pin_mosi = pins::SPI_MOSI;
      cfg.pin_miso = pins::SPI_MISO;
      cfg.pin_dc = pins::LCD_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = pins::LCD_CS;
      cfg.pin_rst = -1;  // tied to EN
      cfg.pin_busy = -1;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = true;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;
      panel_.config(cfg);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = pins::LCD_BL;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      light_.config(cfg);
      panel_.setLight(&light_);
    }
    {
      auto cfg = touch_.config();
      cfg.x_min = 0;
      cfg.x_max = 319;
      cfg.y_min = 0;
      cfg.y_max = 479;
      cfg.pin_int = pins::TOUCH_IRQ;
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;
      cfg.spi_host = HSPI_HOST;
      cfg.freq = 1000000;
      cfg.pin_sclk = pins::SPI_SCK;
      cfg.pin_mosi = pins::SPI_MOSI;
      cfg.pin_miso = pins::SPI_MISO;
      cfg.pin_cs = pins::TOUCH_CS;
      touch_.config(cfg);
      panel_.setTouch(&touch_);
    }
    setPanel(&panel_);
  }
};
