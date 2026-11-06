// robot.h - StackChan control: face (M5Stack-Avatar), head servos, RGB LEDs
// Takes the abstract commands of actions.h. Call everything from loop() only
// (the LEDs and the touch sensor are on I2C; the servo commands go to the
// M5StackChan motion task; the face is drawn by the Avatar task).
#pragma once
#include <stdint.h>
#include "actions.h"
#include "shared.h"
#include "touch_gesture.h"

namespace robot {

// Small status shown on the face (top and bottom lines) when enabled
struct Status {
    uint8_t channel = 0;
    bool link = false;  // a MorseComm station is heard
    int stations = 0;
    bool rx = false;         // receiving keying now
    bool tx = false;         // sending now
    char state[18] = {};     // dialog state name
    char lastWord[24] = {};  // last recognized message
};

// Detects the hardware (StackChan body or not; see robot.cpp) and starts the
// face, servos and LEDs
bool begin();
// Detected hardware: name for messages, and whether the StackChan body
// (servos, body LEDs, head touch) is attached
const char* board();
// Show the start-up splash screen for ms (the robot keeps running under it)
void showSplash(uint32_t ms);
bool body();
// Face + gesture + LEDs. A reaction gesture replaces a running one
void react(const bot::Reaction& r, uint32_t nowMs);
void setEmotion(bot::Emotion e, uint32_t nowMs);
// force = true: start even during the cool-down (reactions, test command)
bool gesture(bot::Gesture g, uint32_t nowMs, bool force = true);
void led(bot::Led l, uint32_t nowMs);

// Call every loop. txKey / rxKey: key state of our transmission / of the
// received station (the LEDs follow them). idle: the dialog is idle (allows
// the occasional look-around)
void update(uint32_t nowMs, bool txKey, bool rxKey, bool idle);

void setMotionLevel(uint8_t level);  // 0 off, 1 small, 2 normal
void setLedLevel(uint8_t level);     // 0..3
void setStatus(const Status& s);
void showStatus(bool on);
bool statusShown();
bool gestureActive();
bot::Emotion emotion();

// Touch: the head touch sensor was tapped
bool headTapped();

// A finished touch on the screen (tap, swipe up, ...; touch_gesture.h)
bool pollScreen(touch::Event& ev);

// Dozing (no MorseComm station on the channel): eyes closed, "Zzz", head
// drooping, LEDs breathing. Ending it plays the wake-up (surprise, head pops
// up, then a smile). Any other emotion also ends it (without the show)
void setDozing(bool on, uint32_t nowMs);
bool dozing();

// The settings screen replaces the face while shown (drawn by the Avatar
// task). Call again whenever the values or the selection change
void showMenu(bool on, const Settings& s, int selected);

// Battery readings on the serial port (diagnostics)
void printBattery();
// Dump the overlay layer (marks / status / settings screen; 1 bit per pixel,
// 320 x 240) as hex lines on the serial port (diagnostics, screenshots)
void dumpLayer();

// Lowest free stack of the Avatar draw task so far (bytes; for diagnostics)
uint32_t drawStackFree();

}  // namespace robot
