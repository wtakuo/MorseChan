// actions.h - abstract robot commands issued by the dialog engine
// Arduino-independent. The dialog engine only uses these names; robot.cpp maps
// them to the Avatar face, the servos and the RGB LEDs.
#pragma once
#include <stdint.h>

namespace bot {

enum class Emotion : uint8_t {
    Neutral,
    Happy,
    Excited,
    Sad,
    Surprised,
    Confused,
    Thinking,
    Sleepy,  // dozing (no MorseComm station on the channel)
};
constexpr int EMOTION_COUNT = 8;

enum class Gesture : uint8_t {
    None,
    LookLeft,
    LookRight,
    LookAround,
    Nod,
    Tilt,   // tilt the head (the face on the screen is rotated too)
    Shake,  // shake the head (no)
    Bow,
    Wiggle,  // happy wiggle
    WakeUp,  // startled awake: the head pops up
};
constexpr int GESTURE_COUNT = 10;

enum class Led : uint8_t {
    Keep,      // leave the LEDs as they are
    Idle,      // dim glow
    Happy,     // green
    Excited,   // yellow
    Sad,       // dim purple
    Alert,     // short red blinking
    Confused,  // blue
};
constexpr int LED_COUNT = 7;

// A reaction: face + motion + LEDs, all optional
struct Reaction {
    Emotion emotion = Emotion::Neutral;
    Gesture gesture = Gesture::None;
    Led led = Led::Keep;
};

inline const char* emotionName(Emotion e) {
    static const char* const n[EMOTION_COUNT] = {
        "neutral",   "happy",    "excited",  "sad",
        "surprised", "confused", "thinking", "sleepy"};
    return (int)e < EMOTION_COUNT ? n[(int)e] : "?";
}

inline const char* gestureName(Gesture g) {
    static const char* const n[GESTURE_COUNT] = {
        "none", "lookleft", "lookright", "lookaround", "nod",
        "tilt", "shake",    "bow",       "wiggle",     "wakeup"};
    return (int)g < GESTURE_COUNT ? n[(int)g] : "?";
}

inline const char* ledName(Led l) {
    static const char* const n[LED_COUNT] = {
        "keep", "idle", "happy", "excited", "sad", "alert", "confused"};
    return (int)l < LED_COUNT ? n[(int)l] : "?";
}

}  // namespace bot
