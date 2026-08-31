#pragma once
// Board-local prompt, countdown, and verdict UI. Score lives only in browser.

#include <Arduino_GFX_Library.h>
#include <stdint.h>

#include "../game/actions.h"

class Ui {
 public:
  bool begin(uint8_t touchAddress);
  void showStatus(const char* title, const char* subtitle);
  void showWaiting();
  void showPrompt(Action expected, uint32_t windowMs, uint8_t round);
  void updateCountdown(uint32_t windowMs, uint32_t elapsedMs);
  void showResult(LocalVerdict verdict);

  bool isReady() const { return gfx_ != nullptr; }

 private:
  void drawCentered(const char* text, int16_t y, uint8_t size, uint16_t color);
  void drawRound(uint8_t round);
  void clearDynamicRegions();
  void drawBar(uint32_t windowMs, uint32_t elapsedMs);

  Arduino_DataBus* bus_ = nullptr;
  Arduino_OLED* gfx_ = nullptr;
  int16_t lastBarWidth_ = -1;
  uint16_t lastBarColor_ = 0;
  bool hasBarColor_ = false;
};
