#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "../../include/app_config.h"
#include "../input/touch_driver.h"

namespace {

constexpr int16_t kCharW = 6;  // built-in font cell width, before scaling

constexpr int16_t kBarX = 24;
constexpr int16_t kBarW = LCD_WIDTH - (2 * kBarX);
constexpr int16_t kBarH = 26;
constexpr int16_t kBarY = 330;

constexpr int16_t kPromptY = 190;
constexpr int16_t kFooterY = 400;

// Every post-boot screen only draws inside these three regions. Clearing them
// instead of all 164,864 panel pixels keeps state transitions from monopolising
// the loop that also polls input and pumps BLE.
constexpr int16_t kMainClearY = 176;
constexpr int16_t kMainClearH = 96;
constexpr int16_t kBarClearX = kBarX - 4;
constexpr int16_t kBarClearY = kBarY - 4;
constexpr int16_t kBarClearW = kBarW + 8;
constexpr int16_t kBarClearH = kBarH + 8;
constexpr int16_t kFooterClearY = kFooterY - 4;
constexpr int16_t kFooterClearH = 24;

constexpr uint16_t kColorBg = RGB565(0, 0, 0);
constexpr uint16_t kColorText = RGB565(248, 252, 248);
constexpr uint16_t kColorDim = RGB565(90, 90, 110);
constexpr uint16_t kColorAccent = RGB565(90, 200, 255);
constexpr uint16_t kColorGood = RGB565(40, 210, 110);
constexpr uint16_t kColorBad = RGB565(230, 60, 70);
constexpr uint16_t kColorWarn = RGB565(250, 190, 40);

}  // namespace

bool Ui::begin(uint8_t touchAddress) {
  bus_ = new Arduino_ESP32QSPI(LCD_QSPI_CS, LCD_QSPI_SCK, LCD_QSPI_D0, LCD_QSPI_D1, LCD_QSPI_D2,
                               LCD_QSPI_D3);

  // Rev 2 boards (CST816 touch) use a CO5300 with a 16-column offset; rev 1
  // boards (FT3168 touch) use an SH8601 with no offset.
  if (touchAddress == TouchDriver::kAddrCst816) {
    gfx_ = new Arduino_CO5300(bus_, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT, 16, 0, 0, 0);
  } else {
    gfx_ = new Arduino_SH8601(bus_, GFX_NOT_DEFINED, 0, LCD_WIDTH, LCD_HEIGHT);
  }

  if (!gfx_->begin()) {
    log_e("display init failed");
    delete gfx_;
    gfx_ = nullptr;
    return false;
  }

  gfx_->setBrightness(LCD_BRIGHTNESS);
  gfx_->fillScreen(kColorBg);
  gfx_->setTextWrap(false);
  return true;
}

void Ui::drawCentered(const char* text, int16_t y, uint8_t size, uint16_t color) {
  const int16_t width = static_cast<int16_t>(strlen(text)) * kCharW * size;
  gfx_->setTextSize(size);
  gfx_->setTextColor(color);
  gfx_->setCursor((LCD_WIDTH - width) / 2, y);
  gfx_->print(text);
}

void Ui::drawRound(uint8_t round) {
  char buf[16];
  snprintf(buf, sizeof(buf), "round %u", round);
  drawCentered(buf, kFooterY, 2, kColorDim);
}

void Ui::clearDynamicRegions() {
  gfx_->fillRect(0, kMainClearY, LCD_WIDTH, kMainClearH, kColorBg);
  gfx_->fillRect(kBarClearX, kBarClearY, kBarClearW, kBarClearH, kColorBg);
  gfx_->fillRect(0, kFooterClearY, LCD_WIDTH, kFooterClearH, kColorBg);
  lastBarWidth_ = -1;
  hasBarColor_ = false;
}

void Ui::drawBar(uint32_t windowMs, uint32_t elapsedMs) {
  if (windowMs == 0) {
    return;
  }
  const uint32_t remaining = elapsedMs >= windowMs ? 0 : windowMs - elapsedMs;
  const float fraction = static_cast<float>(remaining) / static_cast<float>(windowMs);
  const int16_t width = static_cast<int16_t>(fraction * kBarW);
  const uint16_t color = fraction > 0.5f ? kColorGood : (fraction > 0.25f ? kColorWarn : kColorBad);

  if (width == lastBarWidth_ && hasBarColor_ && color == lastBarColor_) return;

  if (lastBarWidth_ > width) {
    gfx_->fillRect(kBarX + width, kBarY, lastBarWidth_ - width, kBarH, kColorBg);
  }
  if (!hasBarColor_ || color != lastBarColor_) {
    // A band transition recolors the remaining bar once. Ordinary countdown
    // ticks only clear the disappeared sliver above.
    gfx_->fillRect(kBarX, kBarY, width, kBarH, color);
  } else if (width > lastBarWidth_) {
    // Defensive support for a reset/increased window without a full repaint.
    gfx_->fillRect(kBarX + lastBarWidth_, kBarY, width - lastBarWidth_, kBarH, color);
  }
  lastBarWidth_ = width;
  lastBarColor_ = color;
  hasBarColor_ = true;
}

void Ui::showStatus(const char* title, const char* subtitle) {
  if (!isReady()) return;
  clearDynamicRegions();
  drawCentered(title, kPromptY, 4, kColorAccent);
  drawCentered(subtitle, kPromptY + 60, 2, kColorDim);
}

void Ui::showWaiting() {
  if (!isReady()) return;
  clearDynamicRegions();
  drawCentered("GET READY", kPromptY, 4, kColorAccent);
}

void Ui::showPrompt(Action expected, uint32_t windowMs, uint8_t round) {
  if (!isReady()) return;
  clearDynamicRegions();
  drawCentered(actionToPrompt(expected), kPromptY, 4, kColorText);
  gfx_->drawRect(kBarX - 2, kBarY - 2, kBarW + 4, kBarH + 4, kColorDim);
  drawBar(windowMs, 0);
  drawRound(round);
}

void Ui::updateCountdown(uint32_t windowMs, uint32_t elapsedMs) {
  if (!isReady()) {
    return;
  }
  drawBar(windowMs, elapsedMs);
}

void Ui::showResult(LocalVerdict verdict) {
  if (!isReady()) {
    return;
  }

  const char* headline = "?";
  uint16_t color = kColorDim;
  switch (verdict) {
    case LocalVerdict::Success:
      headline = "NICE";
      color = kColorGood;
      break;
    case LocalVerdict::WrongAction:
      headline = "WRONG";
      color = kColorWarn;
      break;
    case LocalVerdict::Timeout:
      headline = "TOO SLOW";
      color = kColorBad;
      break;
    default:
      break;
  }

  clearDynamicRegions();
  drawCentered(headline, kPromptY - 10, 5, color);
}
