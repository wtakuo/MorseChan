// gestures.h - head motions as keyframe tables, and the safe-range clamp
// Arduino-independent (the host tests check that every keyframe stays inside
// the configured limits).
//
// Angles are in 0.1 degree, as in the M5StackChan library. yaw: positive =
// left. pitch: offset from cfg::REST_PITCH, positive = up. speed: 0..1000
// (library). holdMs: time until the next keyframe. faceTilt: rotation of the
// face on the screen in degrees (StackChan has no roll axis, so a head tilt is
// shown by rotating the face)
#pragma once
#include <stdint.h>
#include "actions.h"
#include "config.h"

namespace bot {

struct Keyframe {
    int16_t yaw;
    int16_t pitch;
    int16_t speed;
    uint16_t holdMs;
    int8_t faceTilt;
};

struct GestureDef {
    const Keyframe* frames;
    int count;
};

namespace detail {
// Every gesture ends at the resting pose (0, 0)
constexpr Keyframe LOOK_LEFT[] = {{250, 30, 300, 1200, 0}, {0, 0, 300, 600, 0}};
constexpr Keyframe LOOK_RIGHT[] = {{-250, 30, 300, 1200, 0},
                                   {0, 0, 300, 600, 0}};
constexpr Keyframe LOOK_AROUND[] = {
    {300, 50, 300, 900, 0}, {-300, 50, 300, 1100, 0}, {0, 0, 300, 600, 0}};
constexpr Keyframe NOD[] = {{0, -120, 700, 300, 0},
                            {0, 0, 700, 300, 0},
                            {0, -120, 700, 300, 0},
                            {0, 0, 600, 400, 0}};
constexpr Keyframe TILT[] = {{120, 80, 400, 1500, 15}, {0, 0, 400, 600, 0}};
constexpr Keyframe SHAKE[] = {{200, 0, 700, 300, 0},
                              {-200, 0, 700, 300, 0},
                              {200, 0, 700, 300, 0},
                              {0, 0, 600, 400, 0}};
constexpr Keyframe BOW[] = {{0, -150, 400, 900, 0}, {0, 0, 400, 700, 0}};
constexpr Keyframe WIGGLE[] = {{150, 100, 800, 250, -8},
                               {-150, 100, 800, 250, 8},
                               {150, 100, 800, 250, -8},
                               {-150, 100, 800, 250, 8},
                               {0, 0, 600, 500, 0}};
constexpr Keyframe WAKE_UP[] = {{0, 120, 900, 500, 0}, {0, 0, 400, 600, 0}};
}  // namespace detail

inline GestureDef gestureDef(Gesture g) {
    using namespace detail;
#define GDEF(a) \
    GestureDef { a, (int)(sizeof(a) / sizeof(a[0])) }
    switch (g) {
        case Gesture::LookLeft:
            return GDEF(LOOK_LEFT);
        case Gesture::LookRight:
            return GDEF(LOOK_RIGHT);
        case Gesture::LookAround:
            return GDEF(LOOK_AROUND);
        case Gesture::Nod:
            return GDEF(NOD);
        case Gesture::Tilt:
            return GDEF(TILT);
        case Gesture::Shake:
            return GDEF(SHAKE);
        case Gesture::Bow:
            return GDEF(BOW);
        case Gesture::Wiggle:
            return GDEF(WIGGLE);
        case Gesture::WakeUp:
            return GDEF(WAKE_UP);
        default:
            return GestureDef{nullptr, 0};
    }
#undef GDEF
}

inline int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

// Absolute servo targets for a keyframe at motion level 1 (small: half the
// amplitude) or 2 (normal), clamped to the safe range
struct Pose {
    int yaw;
    int pitch;
};

inline Pose keyframePose(const Keyframe& k, int level) {
    int yaw = k.yaw, dp = k.pitch;
    if (level <= 1) {
        yaw /= 2;
        dp /= 2;
    }
    return Pose{clampInt(yaw, -cfg::YAW_LIMIT, cfg::YAW_LIMIT),
                clampInt(cfg::REST_PITCH + dp, cfg::PITCH_MIN, cfg::PITCH_MAX)};
}

inline Pose restPose() { return Pose{0, cfg::REST_PITCH}; }

// Head pose while dozing (drooping a little)
inline Pose dozePose() {
    return Pose{0, clampInt(cfg::REST_PITCH + cfg::DOZE_PITCH_OFFSET,
                            cfg::PITCH_MIN, cfg::PITCH_MAX)};
}

// Total duration of a gesture in ms
inline uint32_t gestureDuration(Gesture g) {
    GestureDef d = gestureDef(g);
    uint32_t t = 0;
    for (int i = 0; i < d.count; i++) t += d.frames[i].holdMs;
    return t;
}

}  // namespace bot
