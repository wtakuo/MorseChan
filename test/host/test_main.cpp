// Host-side unit tests (Mac, g++/clang). Build and run: make -C test/host test
// Covers the Arduino-independent parts: recognizer, dialog rules, message
// assembler, dialog state machine, gesture limits, and an end-to-end path
// through the MorseComm protocol code (keying -> packets -> receiver ->
// decoder -> assembler -> recognizer).
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "../../protocol.h"
#include "../../jitter.h"
#include "../../keyer.h"
#include "../../morse_decoder.h"
#include "../../recognizer.h"
#include "../../dialog_rules.h"
#include "../../dialog.h"
#include "../../assembler.h"
#include "../../gestures.h"
#include "../../settings_menu.h"
#include "../../touch_gesture.h"

using namespace chat;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                             \
    do {                                                             \
        if (cond) {                                                  \
            g_pass++;                                                \
        } else {                                                     \
            g_fail++;                                                \
            printf("  FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); \
            printf(__VA_ARGS__);                                     \
            printf("\n");                                            \
        }                                                            \
    } while (0)

static Recognizer rec(PHRASES, PHRASE_COUNT);

static void expectIntent(const char* in, Intent want, MatchKind kind) {
    Result r = rec.recognize(in);
    CHECK(r.intent == want && r.kind == kind, "\"%s\" -> %s/%s (want %s/%s)",
          in, intentName(r.intent), matchKindName(r.kind), intentName(want),
          matchKindName(kind));
}

// =================================================================
static void testNormalize() {
    printf("[recognizer] normalize\n");
    char out[NORM_MAX];
    normalize("  hel   lo  ", out, sizeof(out));
    CHECK(strcmp(out, "HEL LO") == 0, "got \"%s\"", out);
    normalize("ENJOY?", out, sizeof(out));
    CHECK(strcmp(out, "ENJOY") == 0, "got \"%s\"", out);
    normalize("73 [SK]", out, sizeof(out));
    CHECK(strcmp(out, "73") == 0, "got \"%s\"", out);
    char bad[] = {'H', cw::CHAR_UNKNOWN, 'I', 0};
    normalize(bad, out, sizeof(out));
    CHECK(strcmp(out, "H#I") == 0, "got \"%s\"", out);
    normalize("?!.", out, sizeof(out));
    CHECK(out[0] == 0, "got \"%s\"", out);
}

static void testRecognizer() {
    printf("[recognizer] matching\n");
    // Acceptance 1, 2
    expectIntent("HELLO", Intent::Hello, MatchKind::Exact);
    expectIntent("HEL LO", Intent::Hello, MatchKind::Spaces);
    expectIntent("HE LLO", Intent::Hello, MatchKind::Spaces);
    expectIntent("H E L L O", Intent::Hello, MatchKind::Spaces);
    expectIntent("hello", Intent::Hello, MatchKind::Exact);
    expectIntent("HI", Intent::Hello, MatchKind::Exact);
    expectIntent("HOW ARE YOU", Intent::HowAreYou, MatchKind::Exact);
    expectIntent("HOWAREYOU", Intent::HowAreYou, MatchKind::Spaces);
    expectIntent("HOW ARE YOU?", Intent::HowAreYou, MatchKind::Exact);
    expectIntent("HO WAR EYOU", Intent::HowAreYou, MatchKind::Spaces);
    expectIntent("GM", Intent::GoodMorning, MatchKind::Exact);
    expectIntent("GOOD MORNING", Intent::GoodMorning, MatchKind::Exact);
    expectIntent("SOS", Intent::Sos, MatchKind::Exact);
    expectIntent("S O S", Intent::Sos, MatchKind::Spaces);
    expectIntent("73", Intent::Bye, MatchKind::Exact);
    expectIntent("7 3", Intent::Bye, MatchKind::Spaces);
    expectIntent("TNX", Intent::Thanks, MatchKind::Exact);
    expectIntent("YES", Intent::Yes, MatchKind::Exact);
    expectIntent("Y ES", Intent::Yes, MatchKind::Spaces);
    expectIntent("NO", Intent::No, MatchKind::Exact);
    expectIntent("OK", Intent::Ok, MatchKind::Exact);
    expectIntent("FINE", Intent::Fine, MatchKind::Exact);
    // Contained (whole words only)
    expectIntent("HI HI", Intent::Hello, MatchKind::Exact);
    expectIntent("HELLO STACKCHAN", Intent::Hello, MatchKind::Contained);
    expectIntent("HEL LO STACKCHAN", Intent::Hello, MatchKind::Contained);
    expectIntent("TNX 73", Intent::Bye, MatchKind::Contained);
    expectIntent("SOS HELP", Intent::Sos, MatchKind::Contained);
    expectIntent("I KNOW", Intent::None, MatchKind::None);  // not "NO"
    // Fuzzy only for long phrases
    expectIntent("HELLP", Intent::Hello, MatchKind::Fuzzy);
    expectIntent("HELO", Intent::Hello, MatchKind::Exact);
    expectIntent("THANKZ", Intent::Thanks, MatchKind::Fuzzy);
    expectIntent("HOWAREYOV", Intent::HowAreYou, MatchKind::Fuzzy);
    expectIntent("YEX", Intent::None, MatchKind::None);
    expectIntent("NI", Intent::None, MatchKind::None);
    expectIntent("OKK", Intent::None, MatchKind::None);
    expectIntent("E", Intent::None, MatchKind::None);
    // Ambiguous: different intents with the same priority
    expectIntent("YES NO", Intent::None, MatchKind::Ambiguous);
    // Noise
    expectIntent("", Intent::None, MatchKind::Empty);
    expectIntent("  ?  ", Intent::None, MatchKind::Empty);
    char unk[] = {cw::CHAR_UNKNOWN, 0};
    expectIntent(unk, Intent::None, MatchKind::None);
    // Very long input does not crash
    std::string longIn(500, 'A');
    Result r = rec.recognize(longIn.c_str());
    CHECK(r.kind == MatchKind::None, "long input");
}

// Every '|'-separated variant is sendable, non-empty and short enough
static bool sendable(const char* s) {
    int n = variantCount(s);
    for (int k = 0; k < n; k++) {
        char v[64];
        variantAt(s, k, v, sizeof(v));
        if (!v[0] || (int)strlen(v) > cfg::REPLY_MAX) return false;
        for (const char* p = v; *p; p++)
            if (*p != ' ' && !cw::morseEncode(*p)) return false;
    }
    return true;
}

// sent is one of the variants of spec ("FB|GOOD")
static bool isVariant(const std::string& sent, const char* spec) {
    int n = variantCount(spec);
    for (int k = 0; k < n; k++) {
        char v[64];
        variantAt(spec, k, v, sizeof(v));
        if (sent == v) return true;
    }
    return false;
}

static void testRules() {
    printf("[rules] table consistency\n");
    char a[NORM_MAX], b[NORM_MAX], n[NORM_MAX];
    for (int i = 0; i < PHRASE_COUNT; i++) {
        normalize(PHRASES[i].text, n, sizeof(n));
        CHECK(strcmp(n, PHRASES[i].text) == 0, "phrase not normalized: %s",
              PHRASES[i].text);
        compact(PHRASES[i].text, a, sizeof(a));
        for (int j = i + 1; j < PHRASE_COUNT; j++) {
            compact(PHRASES[j].text, b, sizeof(b));
            CHECK(strcmp(a, b) != 0 || PHRASES[i].intent == PHRASES[j].intent,
                  "\"%s\" and \"%s\" collide", PHRASES[i].text,
                  PHRASES[j].text);
        }
    }
    for (int k = 1; k < (int)Intent::COUNT; k++) {
        int nr = 0, np = 0;
        for (int i = 0; i < RULE_COUNT; i++)
            if ((int)RULES[i].intent == k) nr++;
        for (int i = 0; i < PHRASE_COUNT; i++)
            if ((int)PHRASES[i].intent == k) np++;
        CHECK(nr == 1, "intent %s has %d rules", intentName((Intent)k), nr);
        CHECK(np >= 1, "intent %s has no phrase", intentName((Intent)k));
    }
    for (int i = 0; i < RULE_COUNT; i++) {
        if (!RULES[i].reply) continue;
        CHECK(sendable(RULES[i].reply), "reply %s", RULES[i].reply);
    }
    for (int i = 0; i < QUESTION_COUNT; i++) {
        CHECK(sendable(QUESTIONS[i].text), "q %s", QUESTIONS[i].text);
        CHECK(sendable(QUESTIONS[i].positiveReply), "q+");
        CHECK(sendable(QUESTIONS[i].negativeReply), "q-");
    }
    CHECK(sendable(UNKNOWN_RULE.reply) && sendable(CALLOUT_TEXT), "misc");
}

// =================================================================
static void testAssembler() {
    printf("[assembler]\n");
    MessageAssembler as;
    as.setEndGap(2000);
    char out[64];
    int peer;
    CHECK(!as.poll(0, out, sizeof(out), peer), "idle");
    as.onKey(1, 100);
    as.onChar(1, 'H', 300);
    as.onChar(1, ' ', 900);
    as.onChar(1, ' ', 950);   // repeated space collapsed
    as.onChar(2, 'X', 1000);  // other station ignored
    as.onChar(1, 'I', 1500);
    CHECK(!as.poll(3400, out, sizeof(out), peer), "too early");
    as.onKey(1, 3000);  // keying keeps it open
    CHECK(!as.poll(4900, out, sizeof(out), peer), "kept open");
    as.onChar(1, ' ', 3200);
    CHECK(as.poll(5300, out, sizeof(out), peer), "finished");
    CHECK(strcmp(out, "H I") == 0 && peer == 1, "got \"%s\" peer %d", out,
          peer);
    CHECK(as.ignored() == 1, "ignored %u", as.ignored());
    // Only keying: an empty message
    as.onKey(3, 10000);
    CHECK(as.poll(12100, out, sizeof(out), peer) && out[0] == 0, "empty");
    // Overflow finalizes at once
    for (int i = 0; i < 100; i++) as.onChar(0, 'A', 20000 + i);
    CHECK(as.poll(20100, out, sizeof(out), peer), "full");
    CHECK((int)strlen(out) == cfg::MSG_MAX_LEN, "len %zu", strlen(out));
}

// =================================================================
// Dialog driver: runs time forward, simulating the Morse engine (a send keeps
// it busy for 100ms per character)
struct DialogSim {
    Dialog d;
    uint32_t now = 1000;
    uint32_t txBusyUntil = 0;
    std::vector<std::string> sent;
    std::vector<bot::Reaction> reacts;

    DialogSim() {
        d.begin(now);
        d.setCallout(0);
    }
    void run(uint32_t ms) {
        for (uint32_t end = now + ms; now < end; now += 10) {
            d.tick(now);
            Action a;
            while (d.popAction(a)) {
                if (a.kind == Action::Send) {
                    sent.push_back(a.text);
                    txBusyUntil = now + 100 * (uint32_t)strlen(a.text);
                } else {
                    reacts.push_back(a.reaction);
                }
            }
            d.onTxStatus((int32_t)(now - txBusyUntil) < 0, now);
        }
    }
    void say(const char* text) {
        d.onRxActivity(now);
        run(500);
        d.onMessage(text, now);
        run(20);
    }
    std::string lastSent() const { return sent.empty() ? "" : sent.back(); }
};

static void testDialogFlow() {
    printf("[dialog] HELLO -> HI -> question -> YES / NO\n");
    {
        DialogSim s;
        s.say("HEL LO");
        CHECK(s.d.state() == State::Responding, "state %s",
              stateName(s.d.state()));
        CHECK(
            !s.reacts.empty() && s.reacts.back().emotion == bot::Emotion::Happy,
            "happy");
        s.run(5000);
        CHECK(s.sent.size() == 2 && isVariant(s.sent[0], "HI|HELLO"),
              "sent %zu", s.sent.size());
        int q = s.d.waitingQuestion();
        CHECK(q >= 0 && s.sent[1] == QUESTIONS[q].text, "question");
        CHECK(s.d.state() == State::WaitingForAnswer, "waiting");
        s.say("YES");
        s.run(3000);
        CHECK(isVariant(s.lastSent(), QUESTIONS[q].positiveReply), "got %s",
              s.lastSent().c_str());
        CHECK(s.d.state() == State::Idle, "idle %s", stateName(s.d.state()));
        CHECK(s.d.waitingQuestion() < 0, "no question");
    }
    {
        DialogSim s;
        s.say("HELLO");
        s.run(5000);
        int q = s.d.waitingQuestion();
        s.say("N O");
        s.run(3000);
        CHECK(isVariant(s.lastSent(), QUESTIONS[q].negativeReply), "got %s",
              s.lastSent().c_str());
        CHECK(s.reacts.back().emotion == bot::Emotion::Sad, "sad");
    }
    printf("[dialog] fixed replies\n");
    struct {
        const char* in;
        const char* out;
    } cases[] = {{"GM", "GM|MORNING"},
                 {"HOW ARE YOU", "FINE|GOOD"},
                 {"FINE", "FB|GOOD"},
                 {"YES", "OK|GREAT"},
                 {"NO", "OH"},
                 {"OK", "FB|GOOD"},
                 {"SOS", "SOS?|HELP?"},
                 {"73", "73|BYE"},
                 {"BYE", "73|BYE"},
                 {"TNX", "TU|THANKS"},
                 {"THANKS", "TU|THANKS"},
                 {"XYZZY", "?|WHAT?"}};
    for (auto& c : cases) {
        DialogSim s;
        s.say(c.in);
        s.run(3000);
        CHECK(!s.sent.empty() && isVariant(s.sent[0], c.out), "%s -> %s", c.in,
              s.sent.empty() ? "(none)" : s.sent[0].c_str());
    }
    {
        DialogSim s;
        s.say("SOS");
        CHECK(s.reacts.back().emotion == bot::Emotion::Surprised &&
                  s.reacts.back().led == bot::Led::Alert,
              "sos reaction");
        s.run(3000);
        s.say("QQQQ");
        CHECK(s.reacts.back().emotion == bot::Emotion::Confused, "confused");
    }
}

static void testDialogWaiting() {
    printf("[dialog] waiting for an answer\n");
    {  // Timeout returns to Idle without sending
        DialogSim s;
        s.say("HI");
        s.run(5000);
        CHECK(s.d.state() == State::WaitingForAnswer, "waiting");
        size_t n = s.sent.size();
        s.run(cfg::ANSWER_TIMEOUT_MS + 100);
        CHECK(s.d.state() == State::Idle, "idle after timeout");
        CHECK(s.sent.size() == n, "nothing sent");
    }
    {  // Special inputs still work while waiting
        DialogSim s;
        s.say("HI");
        s.run(5000);
        s.say("SOS");
        s.run(3000);
        CHECK(isVariant(s.lastSent(), "SOS?|HELP?"), "got %s",
              s.lastSent().c_str());
        CHECK(s.d.waitingQuestion() < 0, "question cancelled");
        CHECK(s.d.state() == State::Idle, "idle");
    }
    {  // 73 while waiting
        DialogSim s;
        s.say("HI");
        s.run(5000);
        s.say("73");
        s.run(3000);
        CHECK(isVariant(s.lastSent(), "73|BYE"), "got %s",
              s.lastSent().c_str());
    }
    {  // Unknown while waiting: "?" and keep waiting; then answer
        DialogSim s;
        s.say("HI");
        s.run(5000);
        int q = s.d.waitingQuestion();
        s.say("ZZZ");
        s.run(3000);
        CHECK(isVariant(s.lastSent(), UNKNOWN_RULE.reply), "got %s",
              s.lastSent().c_str());
        CHECK(s.d.state() == State::WaitingForAnswer &&
                  s.d.waitingQuestion() == q,
              "still waiting %s", stateName(s.d.state()));
        s.say("OK");
        s.run(3000);
        CHECK(isVariant(s.lastSent(), QUESTIONS[q].positiveReply), "answer");
    }
    {  // Empty message (noise) during waiting goes back to waiting
        DialogSim s;
        s.say("HI");
        s.run(5000);
        s.say("");
        CHECK(s.d.state() == State::WaitingForAnswer, "back to waiting %s",
              stateName(s.d.state()));
    }
    {  // Receiving that never finishes times out
        DialogSim s;
        s.d.onRxActivity(s.now);
        CHECK(s.d.state() == State::Receiving, "receiving");
        s.run(cfg::RECEIVE_TIMEOUT_MS + 100);
        CHECK(s.d.state() == State::Idle, "idle");
    }
    {  // A message arriving while we talk is handled afterwards
        DialogSim s;
        s.say("TNX");
        s.run(800);  // reply is being sent
        s.d.onMessage("73", s.now);
        s.run(3000);
        CHECK(s.sent.size() == 2 && s.sent[1] == "73", "deferred %zu",
              s.sent.size());
    }
    {  // The question rotates
        DialogSim s;
        s.say("HI");
        s.run(5000);
        int q1 = s.d.waitingQuestion();
        s.say("HI");
        s.run(5000);
        int q2 = s.d.waitingQuestion();
        CHECK(q1 >= 0 && q2 >= 0 && q1 != q2, "%d %d", q1, q2);
    }
}

static void testCallout() {
    printf("[dialog] call-out\n");
    DialogSim s;
    s.d.setCallout(60000);
    s.run(59000);
    CHECK(s.sent.empty(), "not too early");
    s.run(3000);
    CHECK(s.sent.size() == 1 && isVariant(s.sent[0], CALLOUT_TEXT), "call-out");
    CHECK(s.d.state() == State::Idle, "idle after call-out");
    // Visitor answers HI: StackChan goes straight to its question
    s.say("HI");
    s.run(5000);
    CHECK(s.sent.size() == 2 && s.d.waitingQuestion() >= 0 &&
              s.sent[1] == QUESTIONS[s.d.waitingQuestion()].text,
          "question after call-out (%zu sent)", s.sent.size());
    s.say("YES");
    s.run(3000);
    // No more than CALLOUT_MAX call-outs without input
    DialogSim t;
    t.d.setCallout(10000);
    t.run(200000);
    CHECK((int)t.sent.size() == cfg::CALLOUT_MAX, "%zu call-outs",
          t.sent.size());
}

static void testVariants() {
    printf("[dialog] reply variants\n");
    char v[32];
    CHECK(variantCount("FB|GOOD") == 2 && variantCount("OH") == 1, "count");
    variantAt("FB|GOOD", 1, v, sizeof(v));
    CHECK(strcmp(v, "GOOD") == 0, "got %s", v);
    variantAt("FB|GOOD", 0, v, sizeof(v));
    CHECK(strcmp(v, "FB") == 0, "got %s", v);
    variantAt("OH", 0, v, sizeof(v));
    CHECK(strcmp(v, "OH") == 0, "got %s", v);
    // Both variants of 73|BYE show up over several conversations
    int ham = 0, plain = 0;
    for (uint32_t seed = 1; seed <= 40; seed++) {
        DialogSim s;
        s.d.setSeed(seed * 2654435761u);
        s.say("73");
        s.run(3000);
        if (s.lastSent() == "73") ham++;
        if (s.lastSent() == "BYE") plain++;
    }
    CHECK(ham + plain == 40 && ham >= 5 && plain >= 5, "73=%d BYE=%d", ham,
          plain);
}

static void testPresence() {
    printf("[dialog] no call-out to an empty channel\n");
    DialogSim s;
    s.d.setCallout(20000);
    s.d.setPresence(false, s.now);
    s.run(60000);
    CHECK(s.sent.empty(), "no call-out without a station");
    // A station appears: the call-out interval starts again
    s.d.setPresence(true, s.now);
    s.run(15000);
    CHECK(s.sent.empty(), "not at once");
    s.run(8000);
    CHECK(s.sent.size() == 1, "call-out after the interval (%zu)",
          s.sent.size());
}

static void testMenu() {
    printf("[menu] stepping and layout\n");
    Settings s;
    CHECK(menu::step(s, menu::Item::Channel, +1) && s.channel == 2, "ch+");
    for (int i = 0; i < 20; i++) menu::step(s, menu::Item::Channel, +1);
    CHECK(s.channel == cfg::CH_MAX, "ch max %u", s.channel);
    CHECK(!menu::step(s, menu::Item::Channel, +1), "no change at max");
    for (int i = 0; i < 20; i++) menu::step(s, menu::Item::Channel, -1);
    CHECK(s.channel == cfg::CH_MIN, "ch min %u", s.channel);
    for (int i = 0; i < 10; i++) menu::step(s, menu::Item::Volume, +1);
    CHECK(s.volume == 5, "vol %u", s.volume);
    s.calloutS = 180;
    menu::step(s, menu::Item::Callout, +1);
    CHECK(s.calloutS == 300, "callout %u", s.calloutS);
    for (int i = 0; i < 10; i++) menu::step(s, menu::Item::Callout, -1);
    CHECK(s.calloutS == 0, "callout off %u", s.calloutS);
    s.dozeS = 45;  // set by serial, not in the list
    menu::step(s, menu::Item::Doze, +1);
    CHECK(s.dozeS == 60, "doze %u", s.dozeS);
    s.dozeS = 45;
    menu::step(s, menu::Item::Doze, -1);
    CHECK(s.dozeS == 30, "doze %u", s.dozeS);
    s.wpm = 30;
    CHECK(!menu::step(s, menu::Item::Speed, +1) && s.wpm == 30, "wpm max");
    menu::step(s, menu::Item::RxTone, +1);
    CHECK(s.rxTone == 1, "rxtone");
    char val[16];
    s.channel = 3;
    menu::valueText(s, menu::Item::Channel, val, sizeof(val));
    CHECK(strcmp(val, "CH3") == 0, "%s", val);
    s.dozeS = 0;
    menu::valueText(s, menu::Item::Doze, val, sizeof(val));
    CHECK(strcmp(val, "OFF") == 0, "%s", val);
    // Layout: rows and buttons do not overlap and fit on the screen
    int row = -1;
    CHECK(
        menu::hitTest(10, menu::ROW_Y0 + 1, row) == menu::Hit::Row && row == 0,
        "row 0");
    CHECK(menu::hitTest(10, menu::ROW_Y0 + menu::ROW_H * menu::ITEM_COUNT - 1,
                        row) == menu::Hit::Row &&
              row == menu::ITEM_COUNT - 1,
          "last row");
    CHECK(menu::hitTest(50, 220, row) == menu::Hit::Minus, "minus");
    CHECK(menu::hitTest(160, 220, row) == menu::Hit::Plus, "plus");
    CHECK(menu::hitTest(260, 220, row) == menu::Hit::Done, "done");
    CHECK(menu::hitTest(160, 5, row) == menu::Hit::None, "title");
    CHECK(menu::BTN_Y + menu::BTN_H <= 240 && menu::BTN_H >= 36, "buttons");
    CHECK(menu::BTN_DONE_X + menu::BTN_W <= menu::SCREEN_W, "fits");
}

// Feed a touch path: points (x, y) every stepMs; dropouts lists indexes
// where the panel reports no touch. Returns the event after release
static touch::Event runTouch(std::vector<std::pair<int, int>> pts,
                             uint32_t stepMs, std::vector<int> dropouts = {}) {
    touch::Tracker t;
    touch::Event ev;
    uint32_t now = 1000;
    int n = 0;
    for (size_t i = 0; i < pts.size(); i++, now += stepMs) {
        bool drop = false;
        for (int d : dropouts) drop |= d == (int)i;
        if (t.update(now, !drop, pts[i].first, pts[i].second, ev)) n++;
    }
    for (int i = 0; i < 50; i++, now += 5)
        if (t.update(now, false, 0, 0, ev)) n++;
    CHECK(n == 1, "%d events", n);
    return ev;
}

static std::vector<std::pair<int, int>> line(int x0, int y0, int x1, int y1,
                                             int steps) {
    std::vector<std::pair<int, int>> v;
    for (int i = 0; i <= steps; i++)
        v.push_back({x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps});
    return v;
}

static void testTouch() {
    printf("[touch] tap / swipe up\n");
    using touch::Kind;
    // Swipe up from the bottom
    CHECK(runTouch(line(160, 230, 165, 100, 20), 15).kind == Kind::SwipeUp,
          "swipe");
    // With contact dropouts (shorter than RELEASE_MS) in the middle
    CHECK(runTouch(line(160, 230, 150, 90, 30), 10, {5, 6, 7, 15, 20}).kind ==
              Kind::SwipeUp,
          "swipe with dropouts");
    // Lifting the finger with a little jitter down at the end
    auto p = line(160, 235, 160, 110, 20);
    p.push_back({160, 125});
    CHECK(runTouch(p, 15).kind == Kind::SwipeUp, "jitter at the end");
    // Not a swipe: starts too high, too short, sideways, downwards
    CHECK(runTouch(line(160, 100, 160, 10, 20), 15).kind != Kind::SwipeUp,
          "from the middle");
    CHECK(runTouch(line(160, 230, 160, 190, 10), 15).kind != Kind::SwipeUp,
          "too short");
    CHECK(runTouch(line(20, 230, 300, 160, 20), 15).kind != Kind::SwipeUp,
          "sideways");
    CHECK(runTouch(line(160, 160, 160, 235, 20), 15).kind != Kind::SwipeUp,
          "down");
    // Taps, also with a short dropout
    std::vector<std::pair<int, int>> tap(10, {200, 120});
    touch::Event e = runTouch(tap, 10);
    CHECK(e.kind == Kind::Tap && e.x0 == 200 && e.y0 == 120, "tap");
    CHECK(runTouch(tap, 10, {4}).kind == Kind::Tap, "tap with dropout");
    // A long press is neither
    std::vector<std::pair<int, int>> hold(200, {200, 120});
    CHECK(runTouch(hold, 10).kind == Kind::Other, "hold");
}

// =================================================================
static void testGestures() {
    printf("[gestures] safe range\n");
    for (int g = 1; g < bot::GESTURE_COUNT; g++) {
        bot::GestureDef d = bot::gestureDef((bot::Gesture)g);
        CHECK(d.count > 0, "gesture %d empty", g);
        for (int level = 1; level <= 2; level++) {
            for (int i = 0; i < d.count; i++) {
                const bot::Keyframe& k = d.frames[i];
                // The table itself stays inside the limits (no clamping)
                int yaw = level == 1 ? k.yaw / 2 : k.yaw;
                int pitch =
                    cfg::REST_PITCH + (level == 1 ? k.pitch / 2 : k.pitch);
                CHECK(yaw >= -cfg::YAW_LIMIT && yaw <= cfg::YAW_LIMIT, "yaw");
                CHECK(pitch >= cfg::PITCH_MIN && pitch <= cfg::PITCH_MAX,
                      "pitch %d", pitch);
                CHECK(k.speed > 0 && k.speed <= 1000, "speed");
                bot::Pose p = bot::keyframePose(k, level);
                CHECK(p.yaw == yaw && p.pitch == pitch, "pose");
            }
        }
        const bot::Keyframe& last = d.frames[d.count - 1];
        CHECK(last.yaw == 0 && last.pitch == 0 && last.faceTilt == 0,
              "gesture %s ends at rest", bot::gestureName((bot::Gesture)g));
        CHECK(bot::gestureDuration((bot::Gesture)g) <= 4000, "short");
    }
    // The clamp itself
    bot::Keyframe wild{5000, -5000, 500, 100, 0};
    bot::Pose p = bot::keyframePose(wild, 2);
    CHECK(p.yaw == cfg::YAW_LIMIT && p.pitch == cfg::PITCH_MIN, "clamp");
    bot::Keyframe up{-5000, 5000, 500, 100, 0};
    p = bot::keyframePose(up, 2);
    CHECK(p.yaw == -cfg::YAW_LIMIT && p.pitch == cfg::PITCH_MAX, "clamp");
    bot::Pose dz = bot::dozePose();
    CHECK(dz.pitch == cfg::REST_PITCH + cfg::DOZE_PITCH_OFFSET &&
              dz.pitch >= cfg::PITCH_MIN && dz.pitch <= cfg::PITCH_MAX,
          "doze pose %d", dz.pitch);
    // Library limits: yaw -1280..1280, pitch 0..900
    CHECK(
        cfg::YAW_LIMIT <= 1280 && cfg::PITCH_MIN >= 0 && cfg::PITCH_MAX <= 900,
        "within library limits");
}

// =================================================================
// End-to-end through the MorseComm protocol code
struct Tr {
    uint32_t t;
    uint8_t s;
};

// Human-like keying of text: dit length ditMs; gaps between letters are
// letterGap dits, except where extraGapAfter[i] (index of a letter) asks for a
// longer pause (in dits). Timing jitter +-jitterPct % on every element
static std::vector<Tr> humanKeying(const char* text, uint32_t ditMs,
                                   std::vector<std::pair<int, int>> pauses,
                                   int jitterPct, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> J(-jitterPct, jitterPct);
    auto len = [&](uint32_t base) {
        return (uint32_t)(base * (100 + J(rng)) / 100);
    };
    std::vector<Tr> tl;
    uint32_t t = 1000;
    int letter = 0;
    for (const char* p = text; *p; p++) {
        if (*p == ' ') {
            t += len(4 * ditMs);  // 3 already added after the letter
            continue;
        }
        const char* code = cw::morseEncode(*p);
        for (const char* c = code; *c; c++) {
            tl.push_back({t, 1});
            t += len((*c == '-' ? 3 : 1) * ditMs);
            tl.push_back({t, 0});
            t += len(ditMs);
        }
        int gap = 3;
        for (auto& pa : pauses)
            if (pa.first == letter) gap = pa.second;
        t += len((gap - 1) * ditMs);
        letter++;
    }
    return tl;
}

// Sends a timeline through TxHistory / TxScheduler / encode, delivers the
// packets (with loss) to a Receiver, decodes the playback and assembles the
// message. Returns the last assembled message (all of them in *all)
static std::string transmitAndReceive(const std::vector<Tr>& tl, int lossPct,
                                      unsigned seed,
                                      std::vector<std::string>* all = nullptr) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> L(0, 99), D(2, 15);
    cw::TxHistory hist(1234);
    cw::TxScheduler sched;
    cw::Receiver rx;
    cw::MorseDecoder dec;
    MessageAssembler as;
    struct InFlight {
        uint32_t at;
        std::vector<uint8_t> data;
    };
    std::vector<InFlight> air;
    const uint32_t clockSkew = 777777;  // receiver clock differs
    size_t next = 0;
    uint16_t seq = 0;
    uint32_t end = tl.back().t + 6000;
    std::string out;
    for (uint32_t t = tl.front().t - 500; t < end; t++) {
        while (next < tl.size() && tl[next].t <= t) {
            hist.push(tl[next].s, tl[next].t);
            sched.onTransition(tl[next].s, tl[next].t);
            next++;
        }
        if (sched.poll(t)) {
            cw::Packet p;
            hist.build(p, t, cfg::HIST_N, 1, seq++);
            uint8_t buf[cw::PKT_MAX_SIZE];
            size_t n = cw::encodePacket(p, buf, sizeof(buf));
            if (L(rng) >= lossPct)
                air.push_back({t + D(rng), std::vector<uint8_t>(buf, buf + n)});
        }
        uint32_t rt = t + clockSkew;
        for (size_t i = 0; i < air.size();) {
            if (air[i].at <= t) {
                cw::Packet p;
                if (cw::decodePacket(air[i].data.data(), air[i].data.size(), p))
                    rx.onPacket(p, rt);
                air.erase(air.begin() + i);
            } else {
                i++;
            }
        }
        rx.update(rt);
        cw::PlayedEvent ev;
        while (rx.pop(ev)) {
            dec.onTransition(ev.state, ev.playMs);
            as.onKey(0, rt);
        }
        dec.update(rt);
        char c;
        while (dec.pop(c)) as.onChar(0, c, rt);
        char msg[64];
        int peer;
        if (as.poll(rt, msg, sizeof(msg), peer)) {
            out = msg;
            if (all) all->push_back(msg);
        }
    }
    return out;
}

static void testEndToEnd() {
    printf("[e2e] keying -> packets -> receiver -> decoder -> recognizer\n");
    // MorseChan's own transmission (StreamKeyer, as used by the engine)
    // decodes correctly on a MorseComm receiver
    for (int wpm : {12, 20, 25}) {
        for (const char* text : {"HI", "ENJOY?", "FB!", "73", "SOS?"}) {
            cw::StreamKeyer k;
            k.setWpm(wpm);
            std::vector<Tr> tl;
            uint32_t t = 1000;
            for (const char* p = text; *p || !k.ready(); t++) {
                if (k.ready() && *p) {
                    char unit[2] = {*p++, 0};
                    k.send(unit, t);
                }
                k.update(t);
                cw::KeyEvent ev;
                while (k.pop(ev)) tl.push_back({ev.t, ev.state});
            }
            std::string got = transmitAndReceive(tl, 0, 1);
            CHECK(got == text, "wpm %d \"%s\" -> \"%s\"", wpm, text,
                  got.c_str());
        }
    }
    // A visitor keying HELLO with an over-long gap between L and L decodes as
    // "HEL LO" and is still recognized
    {
        auto tl = humanKeying("HELLO", 100, {{2, 9}}, 0, 1);
        std::string got = transmitAndReceive(tl, 0, 2);
        CHECK(got == "HEL LO", "got \"%s\"", got.c_str());
        Result r = rec.recognize(got.c_str());
        CHECK(r.intent == Intent::Hello, "recognized");
    }
    // A slow, uneven visitor (dit 100ms = 12 WPM, long letter gaps, 10% packet
    // loss). The decoder (MorseComm's, unchanged) adapts its dit estimate and
    // keeps it per station, so the visitor's first message ("HI") is a warm-up
    auto slowHello = [](unsigned seed, uint32_t dit, int jitter,
                        std::string& got) {
        auto tl = humanKeying("HI", dit, {}, jitter, seed + 100);
        auto t2 = humanKeying("HELLO", dit, {{1, 8}, {3, 8}}, jitter, seed);
        uint32_t off = tl.back().t + 4000 - t2.front().t;
        for (auto& e : t2) tl.push_back({e.t + off, e.s});
        std::vector<std::string> all;
        transmitAndReceive(tl, 10, seed, &all);
        got = all.empty() ? "" : all.back();
        return all.size() == 2 &&
               rec.recognize(got.c_str()).intent == Intent::Hello;
    };
    for (unsigned seed = 1; seed <= 20; seed++) {
        std::string got;
        CHECK(slowHello(seed, 100, 10, got), "seed %u: \"%s\"", seed,
              got.c_str());
    }
    // Statistic only: at dit 110ms (11 WPM), +-15% timing errors sometimes make
    // the decoder read a long dit as a dah (MorseComm shows the same text on
    // its own screen)
    int ok = 0;
    for (unsigned seed = 1; seed <= 50; seed++) {
        std::string got;
        ok += slowHello(seed, 110, 15, got);
    }
    printf(
        "  info: HELLO at 11 WPM with +-15%% timing errors: %d/50 recognized\n",
        ok);
    // First contact at keyer speed (20 WPM, exact timing as from MorseComm's
    // iambic keyer) needs no warm-up
    {
        auto tl = humanKeying("HOW ARE YOU", 60, {}, 0, 1);
        std::string got = transmitAndReceive(tl, 0, 1);
        CHECK(got == "HOW ARE YOU", "got \"%s\"", got.c_str());
    }
    for (unsigned seed = 1; seed <= 10; seed++) {
        auto tl = humanKeying("HOW ARE YOU", 90, {}, 10, seed);
        auto t0 = humanKeying("OK", 90, {}, 10, seed + 50);
        uint32_t off = t0.back().t + 4000 - tl.front().t;
        for (auto& e : tl) t0.push_back({e.t + off, e.s});
        std::string got = transmitAndReceive(t0, 5, seed);
        Result r = rec.recognize(got.c_str());
        CHECK(r.intent == Intent::HowAreYou, "seed %u: \"%s\"", seed,
              got.c_str());
    }
}

int main() {
    testNormalize();
    testRecognizer();
    testRules();
    testAssembler();
    testDialogFlow();
    testDialogWaiting();
    testCallout();
    testVariants();
    testPresence();
    testMenu();
    testTouch();
    testGestures();
    testEndToEnd();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
