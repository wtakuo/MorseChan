// robot.cpp - StackChan hardware: M5StackChan library (servos, RGB LEDs, head
// touch) and M5Stack-Avatar (face).
//
// APIs used (checked in the installed libraries):
//   M5StackChan.begin() / update()                    (M5StackChan 1.1.1)
//   M5StackChan.Motion.move(yaw, pitch, speed)         0.1 degree, 0..1000
//   M5StackChan.Motion.setAutoAngleSyncEnabled(false)  smooth keyframes
//   M5StackChan.showRgbColor(r, g, b)                  12 LEDs
//   M5StackChan.TouchSensor.wasClicked()
//   m5avatar::Avatar init / setExpression / setMouthOpenRatio /
//   setEyeOpenRatio / setIsAutoBlink / setRotation     (M5Stack_Avatar 0.10.0)
#include "robot.h"
#include <Arduino.h>
#include <Avatar.h>
#include <M5StackChan.h>
#include <math.h>
#include <string.h>
#include "config.h"
#include "gestures.h"
#include "settings_menu.h"
#include "touch_gesture.h"
#include "version.h"

using bot::Emotion;
using bot::Gesture;
using bot::Led;
using namespace m5avatar;

// Task handle of the Avatar draw task (defined in M5Stack_Avatar's
// Avatar.cpp; used only to report its free stack)
namespace m5avatar {
extern TaskHandle_t drawTaskHandle;
}

namespace robot {
namespace {

// ---- Overlay layer ----
// The extra marks (?, !, ..., sparkles, Zzz), the status lines and the
// settings screen are drawn by loop() into a full-screen 1-bit layer, the same
// size and format as the Avatar face canvas. The Avatar draw task only merges
// the finished layer into the face (OR, or a plain copy for the settings
// screen). Drawing text inside the draw task overflowed its 2 KB stack (it
// crashed with the status lines and the "?" mark), so nothing heavy runs there.
// Two layers: loop() draws into the back one while the draw task may read the
// front one.
portMUX_TYPE layerMux = portMUX_INITIALIZER_UNLOCKED;
M5Canvas* layers[2] = {nullptr, nullptr};
int frontLayer = -1;        // -1 = nothing to merge
int layerInUse = -1;        // being read by the draw task
bool frontReplace = false;  // copy instead of OR (settings screen)

// What loop() draws into the layer
Status ovStatus;
bool ovStatusOn = false;
bool ovMenuOn = false;
Settings ovMenuSet;
int ovMenuSel = 0;
bool ovDirty = true;
uint32_t ovLastMs = 0;
constexpr uint32_t OVERLAY_INTERVAL_MS = 100;  // mark animation

// Wraps one face part (the left eyebrow, which the default face does not
// show) so that it runs inside the Avatar draw task, after the face parts
class Overlay final : public Drawable {
  public:
    explicit Overlay(Drawable* inner) : inner_(inner) {}
    ~Overlay() override { delete inner_; }

    void draw(M5Canvas* spr, BoundingRect rect, DrawContext* ctx) override {
        inner_->draw(spr, rect, ctx);
        portENTER_CRITICAL(&layerMux);
        int f = frontLayer;
        bool replace = frontReplace;
        layerInUse = f;
        portEXIT_CRITICAL(&layerMux);
        if (f >= 0 && ctx->getColorDepth() == 1) {
            uint8_t* d = (uint8_t*)spr->getBuffer();
            const uint8_t* src = (const uint8_t*)layers[f]->getBuffer();
            size_t n = spr->bufferLength();
            if (d && src && n == layers[f]->bufferLength()) {
                if (replace)
                    memcpy(d, src, n);
                else
                    for (size_t i = 0; i < n; i++) d[i] |= src[i];
            }
        }
        portENTER_CRITICAL(&layerMux);
        layerInUse = -1;
        portEXIT_CRITICAL(&layerMux);
    }

  private:
    Drawable* inner_;
};

// ---- Drawing into the layer (loop() only; color 1 = face color) ----
void drawSparkle(M5Canvas* s, int x, int y) {
    s->fillTriangle(x - 3, y, x + 3, y, x, y - 12, 1);
    s->fillTriangle(x - 3, y, x + 3, y, x, y + 12, 1);
    s->fillTriangle(x, y - 3, x, y + 3, x - 12, y, 1);
    s->fillTriangle(x, y - 3, x, y + 3, x + 12, y, 1);
}

// Returns false if there is no mark for this emotion
bool drawMark(M5Canvas* s, Emotion e, uint32_t t) {
    switch (e) {
        case Emotion::Confused:
            s->setTextDatum(middle_center);
            s->setTextColor(1);
            s->setFont(&fonts::Font4);
            s->setTextSize(2);
            s->drawString("?", 285, 50);
            return true;
        case Emotion::Surprised:
            s->fillRect(282, 20, 8, 38, 1);
            s->fillCircle(286, 70, 5, 1);
            return true;
        case Emotion::Thinking: {
            int n = (t / 400) % 4;  // animated dots
            for (int i = 0; i < n; i++) s->fillCircle(250 + i * 18, 40, 4, 1);
            return true;
        }
        case Emotion::Excited: {
            bool ph = (t / 300) % 2;
            drawSparkle(s, ph ? 40 : 30, ph ? 40 : 50);
            drawSparkle(s, ph ? 285 : 275, ph ? 45 : 35);
            return true;
        }
        case Emotion::Sad:
            s->fillCircle(270, 70, 7, 1);  // a drop
            s->fillTriangle(263, 68, 277, 68, 270, 52, 1);
            return true;
        case Emotion::Sleepy: {
            // "Z z z" rising one after another
            int n = (t / 700) % 4;
            s->setTextDatum(middle_center);
            s->setTextColor(1);
            s->setFont(&fonts::Font4);
            for (int i = 0; i < n; i++) {
                s->setTextSize(i == 0 ? 1.0f : i == 1 ? 1.4f : 1.8f);
                s->drawString("Z", 240 + i * 26, 70 - i * 22);
            }
            return true;
        }
        default:
            return false;
    }
}

void drawStatusLines(M5Canvas* s, const Status& st) {
    char line[48];
    s->setFont(&fonts::Font0);
    s->setTextSize(1);
    s->setTextColor(1);
    s->setTextDatum(top_left);
    snprintf(line, sizeof(line), "CH%u %s%s %s%s", st.channel,
             st.link ? "LINK" : "----", st.stations > 1 ? "+" : "", st.state,
             st.tx   ? " TX"
             : st.rx ? " RX"
                     : "");
    s->drawString(line, 4, 4);
    if (st.lastWord[0]) {
        s->setTextDatum(bottom_left);
        snprintf(line, sizeof(line), "> %s", st.lastWord);
        s->drawString(line, 4, 236);
    }
}

// Button with a label (text), or a triangle pointing left (dir < 0) or right
// (dir > 0)
void drawButton(M5Canvas* s, int x, const char* text, int dir = 0) {
    using namespace menu;
    s->drawRoundRect(x, BTN_Y, BTN_W, BTN_H, 6, 1);
    s->drawRoundRect(x + 1, BTN_Y + 1, BTN_W - 2, BTN_H - 2, 5, 1);
    int cx = x + BTN_W / 2, cy = BTN_Y + BTN_H / 2;
    if (dir < 0) {
        s->fillTriangle(cx - 12, cy, cx + 10, cy - 13, cx + 10, cy + 13, 1);
        return;
    }
    if (dir > 0) {
        s->fillTriangle(cx + 12, cy, cx - 10, cy - 13, cx - 10, cy + 13, 1);
        return;
    }
    s->setFont(&fonts::Font4);
    s->setTextSize(1);
    s->setTextDatum(middle_center);
    s->drawString(text, cx, cy);
}

// Battery (updated by update() every BATTERY_INTERVAL_MS; -1 = unknown)
int batteryLevel = -1;
bool batteryCharging = false;
uint32_t lastBatteryMs = 0;
constexpr uint32_t BATTERY_INTERVAL_MS = 5000;

// Battery icon and percentage, right-aligned at (right, y)
void drawBattery(M5Canvas* s, int right, int y) {
    char txt[8];
    if (batteryLevel >= 0)
        snprintf(txt, sizeof(txt), "%d%%%s", batteryLevel,
                 batteryCharging ? "+" : "");
    else
        snprintf(txt, sizeof(txt), "--%%");
    s->setFont(&fonts::Font2);
    s->setTextSize(1);
    s->setTextDatum(top_right);
    s->setTextColor(1);
    s->drawString(txt, right, y);
    int w = s->textWidth(txt);
    // Icon: body 22 x 11, terminal 2 x 5, fill by level
    int bx = right - w - 6 - 24, by = y + 3;
    s->drawRect(bx, by, 22, 11, 1);
    s->fillRect(bx + 22, by + 3, 2, 5, 1);
    int lv = batteryLevel < 0 ? 0 : batteryLevel > 100 ? 100 : batteryLevel;
    int fw = (18 * lv + 50) / 100;
    if (fw > 0) s->fillRect(bx + 2, by + 2, fw, 7, 1);
}

// The settings screen (covers the whole face)
void drawMenu(M5Canvas* s, const Settings& set, int sel) {
    using namespace menu;
    s->setFont(&fonts::Font2);
    s->setTextSize(1);
    s->setTextDatum(top_left);
    s->setTextColor(1);
    s->drawString("MorseChan SETTINGS  v" VERSION, 6, 3);
    drawBattery(s, SCREEN_W - 6, 3);
    s->drawFastHLine(0, TITLE_H, SCREEN_W, 1);
    char val[16];
    for (int i = 0; i < ITEM_COUNT; i++) {
        int y = ROW_Y0 + i * ROW_H;
        bool on = i == sel;
        if (on) s->fillRect(0, y, SCREEN_W, ROW_H, 1);
        s->setTextColor(on ? 0 : 1);
        s->setTextDatum(middle_left);
        s->drawString(label((Item)i), 12, y + ROW_H / 2);
        valueText(set, (Item)i, val, sizeof(val));
        s->setTextDatum(middle_right);
        s->drawString(val, SCREEN_W - 14, y + ROW_H / 2);
    }
    s->setTextColor(1);
    drawButton(s, BTN_MINUS_X, nullptr, -1);
    drawButton(s, BTN_PLUS_X, nullptr, +1);
    drawButton(s, BTN_DONE_X, "DONE");
}

// Redraw the layer (loop()). emotion: the mark to show
void renderLayer(uint32_t now, Emotion emotion) {
    if (!layers[0] || !layers[1]) return;
    portENTER_CRITICAL(&layerMux);
    int back = frontLayer == 0 ? 1 : 0;
    bool busy = layerInUse == back;
    portEXIT_CRITICAL(&layerMux);
    if (busy) return;  // try again next loop
    M5Canvas* s = layers[back];
    s->fillSprite(0);
    bool any = false, replace = false;
    if (ovMenuOn) {
        drawMenu(s, ovMenuSet, ovMenuSel);
        any = replace = true;
    } else {
        any = drawMark(s, emotion, now);
        if (ovStatusOn) {
            drawStatusLines(s, ovStatus);
            any = true;
        }
    }
    portENTER_CRITICAL(&layerMux);
    frontLayer = any ? back : -1;
    frontReplace = replace;
    portEXIT_CRITICAL(&layerMux);
}

Avatar* avatar = nullptr;

// ---- Face ----
Emotion curEmotion = Emotion::Neutral;
uint32_t emotionUntil = 0;  // 0 = hold
uint32_t mouthAnimUntil = 0;

// ---- Gesture sequencer ----
uint8_t motionLevel = cfg::MOTION_DEFAULT;
Gesture curGesture = Gesture::None;
bot::GestureDef gdef{nullptr, 0};
int gIndex = 0;
uint32_t gNextMs = 0;
uint32_t lastGestureEndMs = 0;
uint32_t nextIdleLookMs = 0;

// ---- LEDs ----
uint8_t ledLevel = cfg::LED_LEVEL_DEFAULT;
Led ledMode = Led::Idle;
uint32_t ledUntil = 0;
uint32_t ledStartMs = 0;
uint32_t lastRgb = 0xFFFFFFFF;

bool headTap = false;
touch::Tracker touchTracker;
touch::Event screenEv;
bool hasScreenEv = false;

// ---- Dozing ----
bool dozingNow = false;
Emotion nextEmotion = Emotion::Neutral;  // after the current one times out

Emotion shownEmotion = Emotion::Neutral;  // its mark is on the layer

void applyEmotion(Emotion e) {
    Expression x = Expression::Neutral;
    float mouth = 0.0f;
    bool blink = true;
    switch (e) {
        case Emotion::Happy:
        case Emotion::Excited:
            x = Expression::Happy;
            break;
        case Emotion::Sad:
            x = Expression::Sad;
            break;
        case Emotion::Surprised:
            mouth = 1.0f;
            blink = false;
            break;
        case Emotion::Confused:
            x = Expression::Doubt;
            mouth = 0.2f;
            break;
        case Emotion::Sleepy:
            blink = false;  // eyes closed (below)
            break;
        default:
            break;
    }
    if (avatar->getExpression() != x) avatar->setExpression(x);
    avatar->setMouthOpenRatio(mouth);
    avatar->setIsAutoBlink(blink);
    if (!blink) avatar->setEyeOpenRatio(e == Emotion::Sleepy ? 0.0f : 1.0f);
    shownEmotion = e;
    ovDirty = true;
}

void startGesture(Gesture g, uint32_t now) {
    gdef = bot::gestureDef(g);
    if (!gdef.count) return;
    curGesture = g;
    gIndex = 0;
    gNextMs = now;
}

void stepGesture(uint32_t now) {
    if (curGesture == Gesture::None) return;
    if ((int32_t)(now - gNextMs) < 0) return;
    if (gIndex >= gdef.count) {
        curGesture = Gesture::None;
        lastGestureEndMs = now;
        avatar->setRotation(0);
        return;
    }
    const bot::Keyframe& k = gdef.frames[gIndex++];
    if (motionLevel > 0) {
        bot::Pose p = bot::keyframePose(k, motionLevel);
        int speed =
            k.speed < cfg::SERVO_SPEED_MAX ? k.speed : cfg::SERVO_SPEED_MAX;
        M5StackChan.Motion.move(p.yaw, p.pitch, speed);
    }
    avatar->setRotation(k.faceTilt * (float)M_PI / 180.0f);
    gNextMs = now + k.holdMs;
}

uint8_t scale(uint8_t c, uint8_t level) {
    static const uint8_t MAXV[4] = {0, 24, 64, 160};
    return (uint8_t)((uint32_t)c * MAXV[level > 3 ? 3 : level] / 255);
}

void writeRgb(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t v = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    if (v == lastRgb) return;
    lastRgb = v;
    M5StackChan.showRgbColor(r, g, b);
}

void updateLeds(uint32_t now, bool txKey, bool rxKey) {
    if (ledMode != Led::Idle && ledUntil && (int32_t)(now - ledUntil) >= 0)
        ledMode = Led::Idle;
    uint8_t r = 0, g = 0, b = 0, lv = ledLevel;
    if (dozingNow && ledMode == Led::Idle && !txKey && !rxKey) {
        // Slow breathing in faint blue (period 4 s, 8 steps)
        uint32_t ph = now % 4000;
        uint32_t tri = ph < 2000 ? ph : 4000 - ph;  // 0..2000
        uint8_t stepv = (uint8_t)(tri * 8 / 2001);  // 0..7
        writeRgb(0, 0, ledLevel ? (uint8_t)(1 + stepv * ledLevel) : 0);
        return;
    }
    if (ledMode == Led::Alert) {
        bool on = ((now - ledStartMs) / cfg::LED_ALERT_BLINK_MS) % 2 == 0;
        if (on) r = 255;
    } else if (txKey) {
        r = 255, g = 140, b = 40;  // sending: follows our keying
    } else if (rxKey) {
        r = 0, g = 160, b = 200;  // receiving: follows the visitor's keying
        lv = lv > 1 ? lv - 1 : lv;
    } else {
        switch (ledMode) {
            case Led::Happy:
                g = 255, b = 40;
                break;
            case Led::Excited:
                r = 255, g = 200;
                break;
            case Led::Sad:
                r = 90, b = 140;
                break;
            case Led::Confused:
                g = 40, b = 255;
                break;
            default:  // Idle: a faint glow
                r = 40, g = 40, b = 90;
                lv = lv > 0 ? 1 : 0;
                break;
        }
    }
    writeRgb(scale(r, lv), scale(g, lv), scale(b, lv));
}

}  // namespace

bool begin() {
    M5StackChan.begin();
    // Smooth, continuous keyframe motion (as in the library's Dance example)
    M5StackChan.Motion.setAutoAngleSyncEnabled(false);
    bot::Pose rest = bot::restPose();
    M5StackChan.Motion.move(rest.yaw, rest.pitch, cfg::REST_SPEED);

    // The default face, with the overlay wrapped around the (invisible) left
    // eyebrow. Positions are those of m5avatar::Face::Face()
    Face* face = new Face(
        new Mouth(50, 90, 4, 60), new BoundingRect(148, 163), new Eye(8, false),
        new BoundingRect(93, 90), new Eye(8, true), new BoundingRect(96, 230),
        new Eyeblow(32, 0, false), new BoundingRect(67, 96),
        new Overlay(new Eyeblow(32, 0, true)), new BoundingRect(72, 230));
    // Settings screen canvas: created like the Avatar face canvas (size, then
    // 1-bit color depth) so that the buffers have the same layout
    for (auto& l : layers) {
        l = new M5Canvas(&M5.Display);
        l->createSprite(menu::SCREEN_W, 240);
        l->setColorDepth(1);
    }
    avatar = new Avatar(face);
    avatar->init(1);
    applyEmotion(Emotion::Neutral);
    nextIdleLookMs = millis() + cfg::IDLE_LOOK_MAX_MS;
    return true;
}

void moveRest() {
    if (motionLevel == 0) return;
    bot::Pose rest = bot::restPose();
    M5StackChan.Motion.move(rest.yaw, rest.pitch, cfg::REST_SPEED);
}

void setEmotion(Emotion e, uint32_t now) {
    if (dozingNow && e != Emotion::Sleepy) {
        dozingNow = false;  // woken by something else (no wake-up show)
        moveRest();
    }
    nextEmotion = Emotion::Neutral;
    curEmotion = e;
    // Thinking and Neutral are held; the others return to neutral later
    emotionUntil = (e == Emotion::Thinking || e == Emotion::Neutral)
                       ? 0
                       : now + cfg::EMOTION_HOLD_MS;
    mouthAnimUntil = e == Emotion::Excited ? now + 1500 : 0;
    applyEmotion(e);
}

bool gesture(Gesture g, uint32_t now, bool force) {
    if (g == Gesture::None) return false;
    if (!force && (curGesture != Gesture::None ||
                   (now - lastGestureEndMs) < cfg::GESTURE_GAP_MS))
        return false;
    startGesture(g, now);
    return true;
}

void led(Led l, uint32_t now) {
    if (l == Led::Keep) return;
    ledMode = l;
    ledStartMs = now;
    ledUntil = l == Led::Idle    ? 0
               : l == Led::Alert ? now + 12 * cfg::LED_ALERT_BLINK_MS
                                 : now + cfg::LED_REACTION_MS;
}

void react(const bot::Reaction& r, uint32_t now) {
    setEmotion(r.emotion, now);
    gesture(r.gesture, now, true);
    led(r.led, now);
}

void update(uint32_t now, bool txKey, bool rxKey, bool idle) {
    M5StackChan.update();
    if (M5StackChan.TouchSensor.wasClicked()) headTap = true;
    // Raw touch state -> our own tap / swipe classification
    bool pressed = false;
    int tx = 0, ty = 0;
    if (M5.Touch.getCount()) {
        const auto& d = M5.Touch.getDetail(0);
        pressed = d.isPressed();
        tx = d.x;
        ty = d.y;
    }
    touch::Event tev;
    if (touchTracker.update(now, pressed, tx, ty, tev)) {
        screenEv = tev;
        hasScreenEv = true;
    }
    if (ovMenuOn) avatar->setRotation(0);

    if (emotionUntil && (int32_t)(now - emotionUntil) >= 0) {
        emotionUntil = 0;
        Emotion next = nextEmotion;
        if (next != Emotion::Neutral)
            setEmotion(next, now);
        else {
            curEmotion = Emotion::Neutral;
            applyEmotion(Emotion::Neutral);
        }
    }
    if (mouthAnimUntil) {
        if ((int32_t)(now - mouthAnimUntil) >= 0) {
            mouthAnimUntil = 0;
            avatar->setMouthOpenRatio(0);
        } else {
            avatar->setMouthOpenRatio(((now / 150) % 2) ? 0.8f : 0.3f);
        }
    }

    stepGesture(now);

    // Occasional look-around while nobody is talking (not while dozing)
    if (idle && !dozingNow && curGesture == Gesture::None &&
        (int32_t)(now - nextIdleLookMs) >= 0) {
        static const Gesture looks[] = {Gesture::LookLeft, Gesture::LookRight,
                                        Gesture::LookAround};
        gesture(looks[random(3)], now, false);
        nextIdleLookMs = now + cfg::IDLE_LOOK_MIN_MS +
                         random(cfg::IDLE_LOOK_MAX_MS - cfg::IDLE_LOOK_MIN_MS);
    }
    if (!idle) nextIdleLookMs = now + cfg::IDLE_LOOK_MIN_MS;

    updateLeds(now, txKey, rxKey);

    // Battery (I2C; read here in loop() only)
    if (lastBatteryMs == 0 || now - lastBatteryMs >= BATTERY_INTERVAL_MS) {
        lastBatteryMs = now ? now : 1;
        batteryLevel = M5.Power.getBatteryLevel();
        batteryCharging = M5.Power.isCharging() == m5::Power_Class::is_charging;
    }

    // Redraw the overlay layer (marks animate; status lines change)
    if (ovDirty || now - ovLastMs >= OVERLAY_INTERVAL_MS) {
        renderLayer(now, shownEmotion);
        ovLastMs = now;
        ovDirty = false;
    }
}

void setMotionLevel(uint8_t level) { motionLevel = level > 2 ? 2 : level; }

void setLedLevel(uint8_t level) {
    ledLevel = level > 3 ? 3 : level;
    lastRgb = 0xFFFFFFFF;
}

void setStatus(const Status& s) { ovStatus = s; }

void showStatus(bool on) {
    ovStatusOn = on;
    ovDirty = true;
}

bool statusShown() { return ovStatusOn; }
bool gestureActive() { return curGesture != Gesture::None; }
Emotion emotion() { return curEmotion; }

bool headTapped() {
    bool t = headTap;
    headTap = false;
    return t;
}

bool pollScreen(touch::Event& ev) {
    if (!hasScreenEv) return false;
    ev = screenEv;
    hasScreenEv = false;
    return true;
}

void setDozing(bool on, uint32_t now) {
    if (on == dozingNow) return;
    if (on) {
        curGesture = Gesture::None;
        mouthAnimUntil = 0;
        emotionUntil = 0;
        nextEmotion = Emotion::Neutral;
        curEmotion = Emotion::Sleepy;
        applyEmotion(Emotion::Sleepy);
        avatar->setRotation(0);
        dozingNow = true;
        led(Led::Idle, now);
        if (motionLevel > 0) {
            bot::Pose p = bot::dozePose();
            M5StackChan.Motion.move(p.yaw, p.pitch, cfg::DOZE_SPEED);
        }
        return;
    }
    // Startled awake: "!" and the head pops up, then a smile
    dozingNow = false;
    setEmotion(Emotion::Surprised, now);
    emotionUntil = now + cfg::WAKE_SURPRISE_MS;
    nextEmotion = Emotion::Happy;
    gesture(Gesture::WakeUp, now, true);
    led(Led::Happy, now);
    nextIdleLookMs = now + cfg::IDLE_LOOK_MAX_MS;
}

bool dozing() { return dozingNow; }

void showMenu(bool on, const Settings& s, int selected) {
    ovMenuOn = on;
    ovMenuSet = s;
    ovMenuSel = selected;
    renderLayer(millis(), shownEmotion);  // show it at once
}

void dumpLayer() {
    portENTER_CRITICAL(&layerMux);
    int f = frontLayer;
    portEXIT_CRITICAL(&layerMux);
    if (f < 0) {
        Serial.print("layer: empty\r\n");
        return;
    }
    const uint8_t* b = (const uint8_t*)layers[f]->getBuffer();
    int w = layers[f]->width(), h = layers[f]->height();
    int stride = (w + 7) / 8;
    Serial.printf("layer %d %d\r\n", w, h);
    // Row number first; a short pause per row so that the non-blocking USB
    // serial output does not drop lines
    char line[8 + 2 * 64];
    for (int y = 0; y < h; y++) {
        int n = snprintf(line, sizeof(line), "%03d:", y);
        for (int i = 0; i < stride && n < (int)sizeof(line) - 3; i++)
            n +=
                snprintf(line + n, sizeof(line) - n, "%02X", b[y * stride + i]);
        Serial.printf("%s\r\n", line);
        delay(3);
    }
    Serial.print("layer end\r\n");
}

void printBattery() {
    Serial.printf(
        "M5.Power: level=%ld%% voltage=%dmV charging=%d vbus=%dmV | "
        "StackChan INA226: voltage=%.3fV current=%.3fA\r\n",
        (long)M5.Power.getBatteryLevel(), M5.Power.getBatteryVoltage(),
        (int)M5.Power.isCharging(), M5.Power.getVBUSVoltage(),
        M5StackChan.getBatteryVoltage(), M5StackChan.getBatteryCurrent());
}

uint32_t drawStackFree() {
    return m5avatar::drawTaskHandle
               ? (uint32_t)uxTaskGetStackHighWaterMark(m5avatar::drawTaskHandle)
               : 0;
}

}  // namespace robot
