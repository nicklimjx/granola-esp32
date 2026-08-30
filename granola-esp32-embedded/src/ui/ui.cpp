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

constexpr int16_t kScoreY = 48;
constexpr int16_t kPromptY = 190;
constexpr int16_t kFooterY = 400;

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

void Ui::drawScore(int32_t score) {
  char buf[24];
  snprintf(buf, sizeof(buf), "SCORE %ld", static_cast<long>(score));
  drawCentered(buf, kScoreY, 3, kColorAccent);
}

void Ui::drawRoundId(const char* roundId) {
  // Round IDs are opaque server strings and may be long; the board only shows
  // them as a debugging aid, so a truncated tail is fine here.
  char buf[20];
  snprintf(buf, sizeof(buf), "round %.12s", roundId);
  drawCentered(buf, kFooterY, 2, kColorDim);
}

void Ui::drawBar(uint32_t windowMs, uint32_t elapsedMs) {
  if (windowMs == 0) {
    return;
  }
  const uint32_t remaining = elapsedMs >= windowMs ? 0 : windowMs - elapsedMs;
  const float fraction = static_cast<float>(remaining) / static_cast<float>(windowMs);
  const int16_t width = static_cast<int16_t>(fraction * kBarW);

  if (width == lastBarWidth_) {
    return;
  }

  // Only clear the sliver that just disappeared, so the bar never flickers.
  if (lastBarWidth_ > width) {
    gfx_->fillRect(kBarX + width, kBarY, lastBarWidth_ - width, kBarH, kColorBg);
  }
  const uint16_t color = fraction > 0.5f ? kColorGood : (fraction > 0.25f ? kColorWarn : kColorBad);
  gfx_->fillRect(kBarX, kBarY, width, kBarH, color);
  lastBarWidth_ = width;
}

void Ui::showStatus(const char* title, const char* subtitle) {
  if (!isReady()) {
    return;
  }
  gfx_->fillScreen(kColorBg);
  lastBarWidth_ = -1;
  drawCentered(title, kPromptY, 4, kColorAccent);
  drawCentered(subtitle, kPromptY + 60, 2, kColorDim);
}

void Ui::showWaiting(int32_t score) {
  if (!isReady()) {
    return;
  }
  gfx_->fillScreen(kColorBg);
  lastBarWidth_ = -1;
  drawScore(score);
  drawCentered("GET READY", kPromptY, 4, kColorAccent);
}

void Ui::showPrompt(Action expected, uint32_t windowMs, int32_t score, const char* roundId) {
  if (!isReady()) {
    return;
  }
  gfx_->fillScreen(kColorBg);
  lastBarWidth_ = -1;
  drawScore(score);
  drawCentered(actionToPrompt(expected), kPromptY, 4, kColorText);
  gfx_->drawRect(kBarX - 2, kBarY - 2, kBarW + 4, kBarH + 4, kColorDim);
  drawBar(windowMs, 0);
  drawRoundId(roundId);
}

void Ui::updateCountdown(uint32_t windowMs, uint32_t elapsedMs) {
  if (!isReady()) {
    return;
  }
  drawBar(windowMs, elapsedMs);
}

void Ui::showPending(const char* note) {
  if (!isReady()) {
    return;
  }
  gfx_->fillRect(kBarX, kBarY, kBarW, kBarH, kColorBg);
  lastBarWidth_ = -1;
  drawCentered(note, kBarY + 5, 2, kColorDim);
}

void Ui::showResult(RoundOutcome outcome, int32_t score) {
  if (!isReady()) {
    return;
  }

  const char* headline = "?";
  uint16_t color = kColorDim;
  switch (outcome) {
    case RoundOutcome::Success:
      headline = "NICE";
      color = kColorGood;
      break;
    case RoundOutcome::WrongAction:
      headline = "WRONG";
      color = kColorWarn;
      break;
    case RoundOutcome::Timeout:
      headline = "TOO SLOW";
      color = kColorBad;
      break;
    case RoundOutcome::Unknown:
    default:
      break;
  }

  gfx_->fillScreen(kColorBg);
  lastBarWidth_ = -1;
  drawScore(score);
  drawCentered(headline, kPromptY - 10, 5, color);
}
