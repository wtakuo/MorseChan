// config.h - default parameters
// Keep this Arduino-independent (it is also included by the host tests).
//
// The first part ("MorseComm compatibility") is copied from MorseComm's
// config.h. protocol.h / jitter.h / peers.h / keyer.h / morse_decoder.h are
// copied from MorseComm unchanged and use these constants. Do not change them
// unless MorseComm changes them too, or StackChan will no longer talk to
// MorseComm.
#pragma once
#include <stdint.h>

namespace cfg {

// =====================================================================
// MorseComm compatibility (same values as MorseComm 0.1.0, config.h)
// =====================================================================

// ---- Key input (used only by keyer.h's Debouncer, unused here) ----
constexpr uint32_t DEBOUNCE_MS = 4;

// ---- Keyer ----
constexpr int WPM_DEFAULT = 20;  // MorseComm's default speed
constexpr int WPM_MIN = 5;
constexpr int WPM_MAX = 50;

// ---- Transmit schedule (MorseComm CLAUDE.md sec. 4.2) ----
constexpr uint32_t TX_INTERVAL_ACTIVE_MS = 20;
constexpr uint32_t TX_ACTIVE_HOLD_MS = 500;
constexpr uint32_t TX_INTERVAL_IDLE_MS = 500;  // idle heartbeat
constexpr int HIST_N = 8;     // transition history entries per packet
constexpr int HIST_MAX = 16;  // upper limit allowed by the packet format

// ---- Receive (MorseComm CLAUDE.md sec. 4.5) ----
constexpr uint32_t JITTER_DELAY_MS = 150;
constexpr uint32_t WATCHDOG_MS = 100;
constexpr uint32_t OFFSET_BUCKET_MS = 1000;
constexpr int OFFSET_BUCKETS = 4;
constexpr uint32_t LINK_TIMEOUT_MS = 3000;
constexpr int32_t SENDER_RESTART_BACKSTEP_MS = 2000;

// ---- ESP-NOW ----
// Wi-Fi channel (fixed, common to all MorseComm stations)
constexpr uint8_t ESPNOW_CHANNEL = 1;

// ---- Communication channel (the "channel" byte in the packet; only stations
// with the same value talk to each other) ----
constexpr uint8_t CH_MIN = 1;
constexpr uint8_t CH_MAX = 8;
constexpr uint8_t CH_DEFAULT = 1;
constexpr int MAX_PEERS = 6;  // stations tracked at the same time

// ---- Decoder ----
constexpr uint32_t DEC_DIT_INIT_MS = 60;  // 20 WPM
constexpr uint32_t DEC_DIT_MIN_MS = 20;   // 60 WPM
constexpr uint32_t DEC_DIT_MAX_MS = 240;  // 5 WPM

// =====================================================================
// MorseChan
// =====================================================================

// ---- Tone (StackChan speaker) ----
// The StackChan plays its own transmission at TX_HZ (MorseComm's sidetone
// default) and, if enabled, received keying at RX_HZ (MorseComm's receive
// tone default)
constexpr int TX_HZ_DEFAULT = 700;
constexpr int RX_HZ_DEFAULT = 600;
constexpr int TONE_HZ_MIN = 300;
constexpr int TONE_HZ_MAX = 1500;
constexpr int VOLUME_DEFAULT = 2;  // 0..5 (ToneOut::VOLUME_MAX)

// ---- Message assembly (MorseComm has no end-of-message marker; a message
// ends when the sender has been silent for this long) ----
constexpr uint32_t MSG_END_GAP_DEFAULT_MS = 2500;
constexpr uint32_t MSG_END_GAP_MIN_MS = 800;
constexpr uint32_t MSG_END_GAP_MAX_MS = 10000;
constexpr int MSG_MAX_LEN = 48;  // a longer message is finalized at once

// ---- Recognizer ----
// Approximate matching (edit distance 1) is used only for phrases at least
// this long (spaces removed). Short words such as YES / NO / OK must match
// exactly
constexpr int FUZZY_MIN_LEN = 5;

// ---- Dialog timing ----
// Delay between the reaction (face / motion) and the start of the reply
constexpr uint32_t REPLY_DELAY_MS = 700;
// Delay between the end of the reply and a follow-up question
constexpr uint32_t QUESTION_DELAY_MS = 1200;
// How long to wait for an answer to a question
constexpr uint32_t ANSWER_TIMEOUT_MS = 30000;
// Receiving state without any keying for this long returns to the previous
// state (safety net; normally the message assembler ends it earlier)
constexpr uint32_t RECEIVE_TIMEOUT_MS = 15000;
// Safety net in case the end of a transmission is never reported
constexpr uint32_t TX_GUARD_MS = 30000;
// Idle call-out ("HI"). 0 disables it. Only CALLOUT_MAX call-outs are made
// until someone sends something
constexpr uint32_t CALLOUT_DEFAULT_S = 180;
constexpr int CALLOUT_MAX = 2;
// A greeting received within this time after our call-out is treated as the
// answer to it (StackChan goes straight to its question)
constexpr uint32_t GREET_WINDOW_MS = 60000;
// A reaction face is kept this long before returning to neutral
constexpr uint32_t EMOTION_HOLD_MS = 4000;
constexpr int REPLY_MAX = 24;  // maximum reply / question length

// ---- Servo (unit: 0.1 degree, as in the M5StackChan library) ----
// Library limits: yaw -1280..1280, pitch 0..900 (0..90 degrees). MorseChan
// stays well inside them
constexpr int YAW_LIMIT = 450;   // +-45 degrees
constexpr int PITCH_MIN = 0;     // library lower limit
constexpr int PITCH_MAX = 450;   // 45 degrees up
constexpr int REST_PITCH = 150;  // resting pose: 15 degrees up (room to bow)
constexpr int REST_SPEED = 300;
// Upper limit of the servo speed (library 0..1000) for all motions. Fast moves
// draw current peaks from the servos (see CLAUDE.md: the USB connection to the
// PC dropped during fast gestures)
constexpr int SERVO_SPEED_MAX = 500;
constexpr uint32_t GESTURE_GAP_MS = 1500;  // minimum time between gestures
constexpr uint8_t MOTION_DEFAULT = 2;      // 0 = off, 1 = small, 2 = normal
// Idle look-around interval (random between MIN and MAX)
constexpr uint32_t IDLE_LOOK_MIN_MS = 20000;
constexpr uint32_t IDLE_LOOK_MAX_MS = 45000;

// ---- Dozing (no MorseComm station heard on our channel) ----
// Doze after this long with no station (0 = never). A station appearing on
// the channel wakes StackChan up
constexpr uint16_t DOZE_DEFAULT_S = 30;
constexpr int DOZE_PITCH_OFFSET = -100;      // the head droops 10 degrees
constexpr int DOZE_SPEED = 80;               // slowly
constexpr uint32_t WAKE_SURPRISE_MS = 1500;  // "!" before smiling

// ---- Settings screen (swipe up from the bottom of the screen to open it;
// gesture thresholds in touch_gesture.h) ----
// Touches right after opening are ignored (the finger is still lifting)
constexpr uint32_t MENU_GUARD_MS = 500;
constexpr uint32_t MENU_TIMEOUT_MS = 30000;  // closes (and saves) when idle
constexpr uint32_t PREVIEW_MS = 150;  // beep when the volume / tone changes

// ---- RGB LED (12 LEDs) ----
constexpr uint8_t LED_LEVEL_DEFAULT = 2;  // brightness 0..3 (0 = off)
constexpr uint32_t LED_REACTION_MS = 3000;
constexpr uint32_t LED_ALERT_BLINK_MS = 200;

}  // namespace cfg
