// settings_menu.h - the on-screen settings menu: items, value stepping and
// the touch layout. Arduino-independent (tested on the host); robot.cpp draws
// it and MorseChan.ino handles the touches.
//
// Layout (320 x 240): a title line, one row per item (tap to select), and
// three large buttons at the bottom: [-] [+] [DONE]
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "config.h"
#include "shared.h"

namespace menu {

enum class Item : uint8_t {
    Channel,
    Volume,
    Speed,
    Motion,
    Led,
    Callout,
    Doze,
    RxTone,
    COUNT
};
constexpr int ITEM_COUNT = (int)Item::COUNT;

// ---- Layout ----
constexpr int SCREEN_W = 320;
constexpr int TITLE_H = 22;
constexpr int ROW_Y0 = 24;
constexpr int ROW_H = 21;
constexpr int BTN_Y = ROW_Y0 + ROW_H * ITEM_COUNT + 6;  // 198
constexpr int BTN_H = 240 - BTN_Y - 2;                  // 40
constexpr int BTN_W = 100;
constexpr int BTN_MINUS_X = 6;
constexpr int BTN_PLUS_X = 110;
constexpr int BTN_DONE_X = 214;

enum class Hit : uint8_t { None, Row, Minus, Plus, Done };

inline Hit hitTest(int x, int y, int& row) {
    if (y >= ROW_Y0 && y < ROW_Y0 + ROW_H * ITEM_COUNT) {
        row = (y - ROW_Y0) / ROW_H;
        return Hit::Row;
    }
    if (y >= BTN_Y - 4) {  // a little slack above the buttons
        if (x >= BTN_MINUS_X && x < BTN_MINUS_X + BTN_W) return Hit::Minus;
        if (x >= BTN_PLUS_X && x < BTN_PLUS_X + BTN_W) return Hit::Plus;
        if (x >= BTN_DONE_X && x < BTN_DONE_X + BTN_W) return Hit::Done;
    }
    return Hit::None;
}

// ---- Items ----
constexpr uint16_t CALLOUT_STEPS[] = {0, 60, 120, 180, 300, 600};
constexpr uint16_t DOZE_STEPS[] = {0, 15, 30, 60, 120, 300};
constexpr int SPEED_MAX = 30;  // the menu offers 5..30 WPM

inline const char* label(Item i) {
    switch (i) {
        case Item::Channel:
            return "CHANNEL";
        case Item::Volume:
            return "VOLUME";
        case Item::Speed:
            return "SPEED (WPM)";
        case Item::Motion:
            return "MOTION";
        case Item::Led:
            return "LED";
        case Item::Callout:
            return "CALL-OUT";
        case Item::Doze:
            return "DOZE";
        case Item::RxTone:
            return "RX TONE";
        default:
            return "?";
    }
}

inline void valueText(const Settings& s, Item i, char* out, int cap) {
    static const char* const MOTION[] = {"OFF", "SMALL", "NORMAL"};
    switch (i) {
        case Item::Channel:
            snprintf(out, cap, "CH%u", s.channel);
            break;
        case Item::Volume:
            snprintf(out, cap, "%u", s.volume);
            break;
        case Item::Speed:
            snprintf(out, cap, "%u", s.wpm);
            break;
        case Item::Motion:
            snprintf(out, cap, "%s", MOTION[s.motion > 2 ? 2 : s.motion]);
            break;
        case Item::Led:
            snprintf(out, cap, "%u", s.ledLevel);
            break;
        case Item::Callout:
            if (s.calloutS)
                snprintf(out, cap, "%us", s.calloutS);
            else
                snprintf(out, cap, "OFF");
            break;
        case Item::Doze:
            if (s.dozeS)
                snprintf(out, cap, "%us", s.dozeS);
            else
                snprintf(out, cap, "OFF");
            break;
        case Item::RxTone:
            snprintf(out, cap, "%s", s.rxTone ? "ON" : "OFF");
            break;
        default:
            out[0] = 0;
    }
}

template <typename T>
inline T stepRange(T v, int dir, int lo, int hi) {
    int n = (int)v + dir;
    return (T)(n < lo ? lo : n > hi ? hi : n);
}

// Step through an ascending list of values: the next larger (dir > 0) or
// smaller (dir < 0) entry; stays at the end of the list. A value set by a
// serial command that is not in the list moves to its neighbor
template <int N>
inline uint16_t stepList(uint16_t v, int dir, const uint16_t (&list)[N]) {
    if (dir > 0) {
        for (int i = 0; i < N; i++)
            if (list[i] > v) return list[i];
        return list[N - 1];
    }
    for (int i = N - 1; i >= 0; i--)
        if (list[i] < v) return list[i];
    return list[0];
}

// Change one item by dir (+1 / -1). Returns true if the value changed
inline bool step(Settings& s, Item i, int dir) {
    Settings old = s;
    switch (i) {
        case Item::Channel:
            s.channel = stepRange(s.channel, dir, cfg::CH_MIN, cfg::CH_MAX);
            break;
        case Item::Volume:
            s.volume = stepRange(s.volume, dir, 0, 5);
            break;
        case Item::Speed:
            s.wpm = stepRange(s.wpm, dir, cfg::WPM_MIN, SPEED_MAX);
            break;
        case Item::Motion:
            s.motion = stepRange(s.motion, dir, 0, 2);
            break;
        case Item::Led:
            s.ledLevel = stepRange(s.ledLevel, dir, 0, 3);
            break;
        case Item::Callout:
            s.calloutS = stepList(s.calloutS, dir, CALLOUT_STEPS);
            break;
        case Item::Doze:
            s.dozeS = stepList(s.dozeS, dir, DOZE_STEPS);
            break;
        case Item::RxTone:
            s.rxTone = dir > 0 ? 1 : 0;
            break;
        default:
            break;
    }
    return s.channel != old.channel || s.volume != old.volume ||
           s.wpm != old.wpm || s.motion != old.motion ||
           s.ledLevel != old.ledLevel || s.calloutS != old.calloutS ||
           s.dozeS != old.dozeS || s.rxTone != old.rxTone;
}

}  // namespace menu
