#pragma once
//
// Ui — what the player sees on the board itself.
//
// The laptop GUI is the real scoreboard; the board only needs to show the
// prompt, how much of the window is left, and whatever verdict and score the
// server sent back. It draws straight to the panel and repaints only the
// countdown bar between frames — no LVGL, no full-frame buffer.
//
// The AMOLED-1.8 ships in two revisions with the same pinout but different
// controllers (SH8601 + FT3168 on rev 1, CO5300 + CST816 on rev 2). begin()
// takes the touch address that InputManager found and picks the matching
// display driver, so one binary runs on either revision.
//

#include <Arduino_GFX_Library.h>
#include <stdint.h>

#include "../game/actions.h"

class Ui {
 public:
  bool begin(uint8_t touchAddress);

  // Full repaint: a headline and a subtitle, used for boot and link status.
  void showStatus(const char* title, const char* subtitle);

  // Connected, between rounds.
  void showWaiting(int32_t score);

  // Full repaint at the start of an action window.
  void showPrompt(Action expected, uint32_t windowMs, int32_t score, const char* roundId);

  // Repaints only the countdown bar. Cheap enough to call at 30 fps.
  void updateCountdown(uint32_t windowMs, uint32_t elapsedMs);

  // Replaces the countdown bar with a short note while the board waits for the
  // server's verdict. Leaves the prompt in place.
  void showPending(const char* note);

  // Full repaint of the verdict the server sent for the round just finished.
  void showResult(RoundOutcome outcome, int32_t score);

  bool isReady() const { return gfx_ != nullptr; }

 private:
  void drawCentered(const char* text, int16_t y, uint8_t size, uint16_t color);
  void drawScore(int32_t score);
  void drawRoundId(const char* roundId);
  void drawBar(uint32_t windowMs, uint32_t elapsedMs);

  Arduino_DataBus* bus_ = nullptr;
  Arduino_OLED* gfx_ = nullptr;
  int16_t lastBarWidth_ = -1;
};
