// engine.h - Morse engine task: every 1ms runs transmit keying (StreamKeyer),
// packet transmission, reception and playback (jitter buffer), decoding and
// the tone. Adapted from MorseComm's engine.cpp (no key / paddle input).
// It runs in its own FreeRTOS task, so the face animation, servos and LEDs in
// loop() cannot delay it.
#pragma once
#include "shared.h"

namespace engine {

// Call after setting g_settings
bool begin();
// Fetch decoded characters and key events (called from loop())
bool popOut(OutItem& item);
// Queue text to send. Returns false if it does not fit
bool send(const char* text);
// Discard text not yet sent (the character being sent is finished)
void cancelSend();
// True while text is waiting or being sent
bool txBusy();

}  // namespace engine
