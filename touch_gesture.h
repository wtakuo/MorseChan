// touch_gesture.h - classifies screen touches into taps and swipes
// Arduino-independent (tested on the host). Fed every loop with the raw touch
// state (pressed + position) instead of relying on the library's hold / click
// events:
//   - a short loss of contact (the touch panel sometimes reports no touch for
//     a moment) does not end the touch (RELEASE_MS)
//   - SwipeUp: starts in the lower part of the screen and moves up far enough,
//     more up than sideways (like the "home" gesture of the StackChan demo)
//   - Tap: little movement, short
#pragma once
#include <stdint.h>

namespace touch {

constexpr uint32_t RELEASE_MS = 80;  // contact lost this long = released
constexpr int SWIPE_START_Y = 150;   // swipe must start below this line
constexpr int SWIPE_MIN_DY = 80;     // and move up at least this far
constexpr uint32_t SWIPE_MAX_MS = 1500;
constexpr int TAP_MAX_MOVE = 20;
constexpr uint32_t TAP_MAX_MS = 800;

enum class Kind : uint8_t { None, Tap, SwipeUp, Other };

inline const char* kindName(Kind k) {
    switch (k) {
        case Kind::Tap:
            return "tap";
        case Kind::SwipeUp:
            return "swipe-up";
        case Kind::Other:
            return "other";
        default:
            return "none";
    }
}

struct Event {
    Kind kind = Kind::None;
    int x0 = 0, y0 = 0;  // where the touch started
    int x1 = 0, y1 = 0;  // last position
    uint32_t ms = 0;     // duration
};

class Tracker {
  public:
    // Call every loop. Returns true when a touch has ended (ev is filled)
    bool update(uint32_t nowMs, bool pressed, int x, int y, Event& ev) {
        if (pressed) {
            if (!down_) {
                down_ = true;
                x0_ = x;
                y0_ = yMin_ = y;
                t0_ = nowMs;
            }
            x1_ = x;
            y1_ = y;
            // The highest point reached (a swipe may end with a small jitter
            // downwards when the finger lifts)
            if (y < yMin_) yMin_ = y;
            lastPressMs_ = nowMs;
            return false;
        }
        if (!down_ || (nowMs - lastPressMs_) < RELEASE_MS) return false;
        down_ = false;
        ev.x0 = x0_;
        ev.y0 = y0_;
        ev.x1 = x1_;
        ev.y1 = y1_;
        ev.ms = lastPressMs_ - t0_;
        ev.kind = classify();
        return true;
    }

    bool down() const { return down_; }

  private:
    Kind classify() const {
        int dx = x1_ - x0_, dy = y1_ - y0_;
        int up = y0_ - yMin_;  // furthest upward travel
        int adx = dx < 0 ? -dx : dx;
        uint32_t ms = lastPressMs_ - t0_;
        if (y0_ >= SWIPE_START_Y && up >= SWIPE_MIN_DY && up > adx &&
            ms <= SWIPE_MAX_MS)
            return Kind::SwipeUp;
        int ady = dy < 0 ? -dy : dy;
        if (adx <= TAP_MAX_MOVE && ady <= TAP_MAX_MOVE && ms <= TAP_MAX_MS)
            return Kind::Tap;
        return Kind::Other;
    }

    bool down_ = false;
    int x0_ = 0, y0_ = 0, x1_ = 0, y1_ = 0, yMin_ = 0;
    uint32_t t0_ = 0, lastPressMs_ = 0;
};

}  // namespace touch
