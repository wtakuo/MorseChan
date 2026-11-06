// MorseChan - StackChan talks with MorseComm in Morse code
// setup()/loop(): ties together the Morse engine (engine.cpp, own task), the
// message assembler, the dialog engine and the robot (face, servos, LEDs),
// plus the serial commands and the settings (Preferences).
//
// Data flow:
//   engine (1ms task) --OutItem queue--> loop: assembler -> dialog -> actions
//   actions: React -> robot / Send -> engine::send (g_typeBuf)
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <string.h>
#include <string>
#include "assembler.h"
#include "board.h"
#include "config.h"
#include "dialog.h"
#include "engine.h"
#include "robot.h"
#include "settings_menu.h"
#include "version.h"

namespace {

constexpr size_t SERIAL_TX_BUFFER = 4096;
constexpr uint32_t STATUS_INTERVAL_MS = 100;
constexpr uint32_t STATS_INTERVAL_MS = 5000;
constexpr const char* PREF_NS = "morsechan";

Preferences prefs;
chat::MessageAssembler assembler;
chat::Dialog dialog;

bool logOn = false;    // log characters and key events
bool statsOn = false;  // print stats every 5 s
uint32_t lastStatusMs = 0;
uint32_t lastStatsMs = 0;
chat::State lastState = chat::State::Idle;
char lastWord[24] = {};
uint32_t noSignalSince = 0;  // last time a station was on our channel

// Settings screen
bool menuOpen = false;
int menuSel = 0;
uint32_t menuLastTouchMs = 0;
uint32_t menuOpenedMs = 0;
bool menuRequest = false;  // "menu" serial command: open / close
bool menuDirty = false;
bool rxKeyDown = false;
std::string txEcho;

// ---- Settings ----
Settings getSettings() {
    lockShared();
    Settings s = g_settings;
    unlockShared();
    return s;
}

void applyLocal(const Settings& s) {
    assembler.setEndGap(s.endGapMs);
    dialog.setCallout((uint32_t)s.calloutS * 1000);
    robot::setMotionLevel(s.motion);
    robot::setLedLevel(s.ledLevel);
}

void putSettings(const Settings& s, bool save) {
    lockShared();
    g_settings = s;
    g_settingsVer++;
    unlockShared();
    applyLocal(s);
    if (!save) return;
    prefs.putUChar("ch", s.channel);
    prefs.putUChar("wpm", s.wpm);
    prefs.putUShort("txhz", s.txHz);
    prefs.putUShort("rxhz", s.rxHz);
    prefs.putUChar("rxtone", s.rxTone);
    prefs.putUChar("vol", s.volume);
    prefs.putUChar("pfilt", s.peerFilter);
    prefs.putBytes("pmac", s.peerMac, 6);
    prefs.putUChar("motion", s.motion);
    prefs.putUChar("led", s.ledLevel);
    prefs.putUShort("callout", s.calloutS);
    prefs.putUShort("endgap", s.endGapMs);
    prefs.putUShort("doze", s.dozeS);
}

template <typename T>
T clampv(T v, T lo, T hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

void loadSettings() {
    Settings d;
    Settings s;
    s.channel = clampv<uint8_t>(prefs.getUChar("ch", d.channel), cfg::CH_MIN,
                                cfg::CH_MAX);
    s.wpm = clampv<uint8_t>(prefs.getUChar("wpm", d.wpm), cfg::WPM_MIN,
                            cfg::WPM_MAX);
    s.txHz = clampv<uint16_t>(prefs.getUShort("txhz", d.txHz), cfg::TONE_HZ_MIN,
                              cfg::TONE_HZ_MAX);
    s.rxHz = clampv<uint16_t>(prefs.getUShort("rxhz", d.rxHz), cfg::TONE_HZ_MIN,
                              cfg::TONE_HZ_MAX);
    s.rxTone = prefs.getUChar("rxtone", d.rxTone) ? 1 : 0;
    s.volume = clampv<uint8_t>(prefs.getUChar("vol", d.volume), 0, 5);
    s.peerFilter = prefs.getUChar("pfilt", 0) ? 1 : 0;
    if (prefs.getBytes("pmac", s.peerMac, 6) != 6) {
        memset(s.peerMac, 0, 6);
        s.peerFilter = 0;
    }
    s.motion = clampv<uint8_t>(prefs.getUChar("motion", d.motion), 0, 2);
    s.ledLevel = clampv<uint8_t>(prefs.getUChar("led", d.ledLevel), 0, 3);
    s.calloutS = prefs.getUShort("callout", d.calloutS);
    s.endGapMs =
        clampv<uint16_t>(prefs.getUShort("endgap", d.endGapMs),
                         cfg::MSG_END_GAP_MIN_MS, cfg::MSG_END_GAP_MAX_MS);
    s.dozeS = prefs.getUShort("doze", d.dozeS);
    putSettings(s, false);
}

void printMac(const uint8_t* m) {
    Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4],
                  m[5]);
}

void printSettings() {
    Settings s = getSettings();
    Serial.printf(
        "ch=%u wpm=%u txhz=%u rxhz=%u rxtone=%s vol=%u motion=%u led=%u "
        "callout=%us doze=%us endgap=%ums peer=",
        s.channel, s.wpm, s.txHz, s.rxHz, s.rxTone ? "on" : "off", s.volume,
        s.motion, s.ledLevel, s.calloutS, s.dozeS, s.endGapMs);
    if (s.peerFilter)
        printMac(s.peerMac);
    else
        Serial.print("any");
    Serial.print("\r\n");
}

void printStats() {
    lockShared();
    Snapshot sn = g_snapshot;
    unlockShared();
    Serial.printf(
        "stats: stations=%d rssi=%d rx(pkts=%lu lost=%lu late=%lu resync=%lu "
        "wd=%lu) tx(pkts=%lu sent=%lu err=%lu) rxq_drop=%lu filtered=%lu "
        "ignored=%lu state=%s heap=%lu\r\n",
        sn.stations, sn.rssi, (unsigned long)sn.rx.packets,
        (unsigned long)sn.rx.lost, (unsigned long)sn.rx.late,
        (unsigned long)sn.rx.resyncs, (unsigned long)sn.rx.watchdogs,
        (unsigned long)sn.txPackets, (unsigned long)sn.net.sent,
        (unsigned long)sn.net.sendErr, (unsigned long)sn.net.rxDropped,
        (unsigned long)sn.filtered, (unsigned long)assembler.ignored(),
        chat::stateName(dialog.state()), (unsigned long)ESP.getFreeHeap());
}

// ---- Serial commands ----
char lineBuf[96];
int lineLen = 0;

const char* resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:
            return "poweron";
        case ESP_RST_SW:
            return "software";
        case ESP_RST_PANIC:
            return "panic";
        case ESP_RST_INT_WDT:
            return "int_wdt";
        case ESP_RST_TASK_WDT:
            return "task_wdt";
        case ESP_RST_WDT:
            return "wdt";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_USB:
            return "usb";
        default:
            return "other";
    }
}

bool parseMac(const char* s, uint8_t* mac) {
    unsigned v[6];
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4],
               &v[5]) != 6)
        return false;
    for (int i = 0; i < 6; i++) {
        if (v[i] > 255) return false;
        mac[i] = (uint8_t)v[i];
    }
    return true;
}

template <typename E>
bool findByName(const char* name, int count, const char* (*nameOf)(E), E& out) {
    for (int i = 0; i < count; i++)
        if (strcasecmp(name, nameOf((E)i)) == 0) {
            out = (E)i;
            return true;
        }
    return false;
}

void printHelp() {
    Serial.print(
        "commands:\r\n"
        "  show              settings and state\r\n"
        "  stats [on|off]    statistics (on: every 5 s)\r\n"
        "  log 0|1           log received characters and key events\r\n"
        "  ch N              MorseComm channel 1..8\r\n"
        "  wpm N             transmit speed 5..50\r\n"
        "  txhz N / rxhz N   tone of our sending / of received keying\r\n"
        "  rxtone on|off     also play received keying\r\n"
        "  vol N             speaker volume 0..5\r\n"
        "  peer MAC|any      talk only to one MorseComm (aa:bb:cc:dd:ee:ff)\r\n"
        "  motion 0|1|2      head motion off / small / normal\r\n"
        "  led 0..3          LED brightness\r\n"
        "  callout N         idle call-out after N s (0 = never)\r\n"
        "  doze N            doze after N s with no station (0 = never)\r\n"
        "  endgap MS         silence that ends a message (800..10000)\r\n"
        "  send TEXT         send TEXT in Morse code\r\n"
        "  sim TEXT          handle TEXT as if received (no radio)\r\n"
        "  rec TEXT          show how TEXT is recognized\r\n"
        "  face NAME         neutral happy excited sad surprised confused "
        "thinking\r\n"
        "  gesture NAME      lookleft lookright lookaround nod tilt shake bow "
        "wiggle\r\n"
        "  ledtest NAME      idle happy excited sad alert confused\r\n"
        "  status on|off     status lines on the face (or tap the screen)\r\n"
        "  menu              open / close the settings screen\r\n"
        "  (swipe up from the bottom of the screen: settings screen)\r\n"
        "  defaults          restore the default settings\r\n"
        "  reboot            restart\r\n");
}

void handleLine(char* line) {
    char* arg = strchr(line, ' ');
    if (arg) {
        *arg++ = 0;
        while (*arg == ' ') arg++;
    } else {
        arg = line + strlen(line);
    }
    uint32_t now = millis();
    Settings s = getSettings();
    int v = atoi(arg);
    bool save = true;

    if (!strcmp(line, "help") || !strcmp(line, "?")) {
        printHelp();
        return;
    } else if (!strcmp(line, "show")) {
        uint8_t mac[6];
        net::ownMac(mac);
        Serial.printf("MorseChan %s on %s, MAC ", VERSION, board::NAME);
        printMac(mac);
        Serial.printf(
            "\r\nuptime=%lus reset=%s heap=%lu draw_stack_free=%lu\r\n",
            (unsigned long)(millis() / 1000),
            resetReasonName(esp_reset_reason()),
            (unsigned long)ESP.getFreeHeap(),
            (unsigned long)robot::drawStackFree());
        Serial.printf("state=%s last=\"%s\" status=%s log=%d\r\n",
                      chat::stateName(dialog.state()), lastWord,
                      robot::statusShown() ? "on" : "off", logOn);
        printSettings();
        return;
    } else if (!strcmp(line, "stats")) {
        if (!strcmp(arg, "on")) statsOn = true;
        if (!strcmp(arg, "off")) statsOn = false;
        printStats();
        return;
    } else if (!strcmp(line, "log")) {
        logOn = v != 0;
        Serial.printf("log=%d\r\n", logOn);
        return;
    } else if (!strcmp(line, "ch") && v >= cfg::CH_MIN && v <= cfg::CH_MAX) {
        s.channel = v;
    } else if (!strcmp(line, "wpm") && v >= cfg::WPM_MIN && v <= cfg::WPM_MAX) {
        s.wpm = v;
    } else if (!strcmp(line, "txhz") && v >= cfg::TONE_HZ_MIN &&
               v <= cfg::TONE_HZ_MAX) {
        s.txHz = v;
    } else if (!strcmp(line, "rxhz") && v >= cfg::TONE_HZ_MIN &&
               v <= cfg::TONE_HZ_MAX) {
        s.rxHz = v;
    } else if (!strcmp(line, "rxtone") &&
               (!strcmp(arg, "on") || !strcmp(arg, "off"))) {
        s.rxTone = !strcmp(arg, "on");
    } else if (!strcmp(line, "vol") && *arg && v >= 0 && v <= 5) {
        s.volume = v;
    } else if (!strcmp(line, "peer") && !strcmp(arg, "any")) {
        s.peerFilter = 0;
    } else if (!strcmp(line, "peer") && parseMac(arg, s.peerMac)) {
        s.peerFilter = 1;
    } else if (!strcmp(line, "motion") && *arg && v >= 0 && v <= 2) {
        s.motion = v;
    } else if (!strcmp(line, "led") && *arg && v >= 0 && v <= 3) {
        s.ledLevel = v;
    } else if (!strcmp(line, "callout") && *arg && v >= 0 && v <= 3600) {
        s.calloutS = v;
    } else if (!strcmp(line, "doze") && *arg && v >= 0 && v <= 3600) {
        s.dozeS = v;
    } else if (!strcmp(line, "endgap") && v >= (int)cfg::MSG_END_GAP_MIN_MS &&
               v <= (int)cfg::MSG_END_GAP_MAX_MS) {
        s.endGapMs = v;
    } else if (!strcmp(line, "defaults")) {
        prefs.clear();
        s = Settings();
    } else if (!strcmp(line, "send") && *arg) {
        bool ok = engine::send(arg);
        Serial.printf("%s\r\n", ok ? "queued" : "buffer full");
        return;
    } else if (!strcmp(line, "sim") && *arg) {
        dialog.onRxActivity(now);
        dialog.onMessage(arg, now);
        const chat::Result& r = dialog.last();
        Serial.printf("SIM \"%s\" -> %s (%s)\r\n", arg,
                      chat::intentName(r.intent), chat::matchKindName(r.kind));
        return;
    } else if (!strcmp(line, "rec") && *arg) {
        chat::Result r = dialog.recognizer().recognize(arg);
        Serial.printf("\"%s\" -> \"%s\" intent=%s match=%s\r\n", arg,
                      r.normalized, chat::intentName(r.intent),
                      chat::matchKindName(r.kind));
        return;
    } else if (!strcmp(line, "face")) {
        bot::Emotion e;
        if (findByName<bot::Emotion>(arg, bot::EMOTION_COUNT, bot::emotionName,
                                     e)) {
            robot::setEmotion(e, now);
            return;
        }
        Serial.print("unknown face\r\n");
        return;
    } else if (!strcmp(line, "gesture")) {
        bot::Gesture g;
        if (findByName<bot::Gesture>(arg, bot::GESTURE_COUNT, bot::gestureName,
                                     g)) {
            robot::gesture(g, now, true);
            return;
        }
        Serial.print("unknown gesture\r\n");
        return;
    } else if (!strcmp(line, "ledtest")) {
        bot::Led l;
        if (findByName<bot::Led>(arg, bot::LED_COUNT, bot::ledName, l)) {
            robot::led(l, now);
            return;
        }
        Serial.print("unknown led\r\n");
        return;
    } else if (!strcmp(line, "layershot")) {
        robot::dumpLayer();
        return;
    } else if (!strcmp(line, "bat")) {
        robot::printBattery();
        return;
    } else if (!strcmp(line, "menu")) {
        menuRequest = true;
        return;
    } else if (!strcmp(line, "status")) {
        robot::showStatus(!strcmp(arg, "on"));
        return;
    } else if (!strcmp(line, "reboot")) {
        Serial.print("rebooting\r\n");
        Serial.flush();
        delay(100);
        ESP.restart();
    } else if (!line[0]) {
        return;
    } else {
        Serial.print("? (help for commands)\r\n");
        return;
    }
    putSettings(s, save);
    printSettings();
}

void pollSerial() {
    while (Serial.available()) {
        int c = Serial.read();
        if (c == '\r' || c == '\n') {
            if (lineLen == 0) continue;
            lineBuf[lineLen] = 0;
            Serial.print("\r\n");
            handleLine(lineBuf);
            lineLen = 0;
            Serial.print("chan> ");
        } else if (c == 8 || c == 127) {
            if (lineLen > 0) {
                lineLen--;
                Serial.print("\b \b");
            }
        } else if (c >= 32 && c < 127 && lineLen < (int)sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = (char)c;
            Serial.write((char)c);
        }
    }
}

// ---- Engine output ----
void drainEngine(uint32_t now) {
    OutItem it;
    while (engine::popOut(it)) {
        switch (it.kind) {
            case OutItem::KeyRx:
                rxKeyDown = it.state;
                assembler.onKey(it.peer, now);
                if (assembler.active() && assembler.peer() == it.peer)
                    dialog.onRxActivity(now);
                if (logOn)
                    Serial.printf("RXKEY %c p%u\r\n", it.state ? 'D' : 'U',
                                  it.peer);
                break;
            case OutItem::CharRx:
                assembler.onChar(it.peer, it.c, now);
                if (logOn) {
                    Serial.printf("RXCHR '%c' p%u ",
                                  it.c == cw::CHAR_UNKNOWN ? '#' : it.c,
                                  it.peer);
                    printMac(it.mac);
                    Serial.print("\r\n");
                }
                break;
            case OutItem::CharTx:
                if (it.c != ' ') txEcho += it.c;
                break;
        }
    }
}

void handleMessages(uint32_t now) {
    char msg[cfg::MSG_MAX_LEN + 1];
    int peer;
    if (!assembler.poll(now, msg, sizeof(msg), peer)) return;
    dialog.onMessage(msg, now);
    const chat::Result& r = dialog.last();
    Serial.printf("RX \"%s\" p%d -> %s (%s)\r\n", msg, peer,
                  chat::intentName(r.intent), chat::matchKindName(r.kind));
    if (r.kind != chat::MatchKind::Empty) {
        strncpy(lastWord, r.normalized, sizeof(lastWord) - 1);
        lastWord[sizeof(lastWord) - 1] = 0;
    }
}

void runDialog(uint32_t now, bool txBusy) {
    dialog.onTxStatus(txBusy, now);
    dialog.tick(now);
    chat::Action a;
    while (dialog.popAction(a)) {
        if (a.kind == chat::Action::Send) {
            Serial.printf("TX \"%s\"\r\n", a.text);
            if (!engine::send(a.text)) Serial.print("TX buffer full\r\n");
        } else {
            robot::react(a.reaction, now);
            noSignalSince = now;  // do not doze right after a reaction
        }
    }
    if (dialog.state() != lastState) {
        Serial.printf("STATE %s -> %s\r\n", chat::stateName(lastState),
                      chat::stateName(dialog.state()));
        lastState = dialog.state();
    }
}

// ---- Settings screen ----
void closeMenu() {
    menuOpen = false;
    robot::showMenu(false, getSettings(), menuSel);
    if (menuDirty) {
        putSettings(getSettings(), true);
        menuDirty = false;
        Serial.print("settings saved: ");
        printSettings();
    }
}

void openMenu(uint32_t now) {
    menuOpen = true;
    menuSel = 0;
    menuOpenedMs = now;
    menuLastTouchMs = now;
    robot::showMenu(true, getSettings(), menuSel);
    Serial.print("settings screen\r\n");
}

void handleScreen(uint32_t now) {
    if (menuRequest) {
        menuRequest = false;
        if (menuOpen)
            closeMenu();
        else
            openMenu(now);
    }
    touch::Event ev;
    if (!robot::pollScreen(ev)) {
        if (menuOpen && now - menuLastTouchMs >= cfg::MENU_TIMEOUT_MS)
            closeMenu();
        return;
    }
    if (logOn)
        Serial.printf("TOUCH %s (%d,%d)->(%d,%d) %lums\r\n",
                      touch::kindName(ev.kind), ev.x0, ev.y0, ev.x1, ev.y1,
                      (unsigned long)ev.ms);
    if (!menuOpen) {
        if (ev.kind == touch::Kind::SwipeUp) {
            openMenu(now);
        } else if (ev.kind == touch::Kind::Tap) {
            robot::showStatus(!robot::statusShown());
        }
        return;
    }
    menuLastTouchMs = now;
    // Only taps operate the menu, and not right after opening it
    if (ev.kind != touch::Kind::Tap || now - menuOpenedMs < cfg::MENU_GUARD_MS)
        return;
    int row = 0;
    Settings s = getSettings();
    menu::Hit hit = menu::hitTest(ev.x0, ev.y0, row);
    if (hit == menu::Hit::Row) {
        menuSel = row;
    } else if (hit == menu::Hit::Minus || hit == menu::Hit::Plus) {
        int dir = hit == menu::Hit::Plus ? 1 : -1;
        if (menu::step(s, (menu::Item)menuSel, dir)) {
            putSettings(s, true);  // takes effect and is saved (NVS) now
            menuDirty = true;
        }
    } else if (hit == menu::Hit::Done) {
        closeMenu();
        return;
    }
    robot::showMenu(true, getSettings(), menuSel);
}

// ---- Dozing: nobody on the channel ----
void updateDoze(uint32_t now, const Snapshot& sn, bool txBusy) {
    bool present = sn.stations > 0;
    dialog.setPresence(present, now);
    if (present) {
        if (robot::dozing()) {
            Serial.printf("WAKE: a station is on CH%u\r\n",
                          getSettings().channel);
            robot::setDozing(false, now);
        }
        noSignalSince = now;
        return;
    }
    uint16_t dozeS = getSettings().dozeS;
    if (dozeS && !robot::dozing() && !menuOpen && !txBusy &&
        dialog.state() == chat::State::Idle &&
        now - noSignalSince >= (uint32_t)dozeS * 1000) {
        Serial.print("DOZE: no station on the channel\r\n");
        robot::setDozing(true, now);
    }
}

}  // namespace

void setup() {
    // USB CDC: the TX buffer is set before begin; never block on output
    Serial.setTxBufferSize(SERIAL_TX_BUFFER);
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    robot::begin();  // M5StackChan.begin() (M5.begin), face, servos, LEDs
    prefs.begin(PREF_NS, false);
    loadSettings();
    bool ok = engine::begin();
    uint32_t now = millis();
    dialog.begin(now);
    dialog.setSeed(esp_random());
    noSignalSince = now;
    applyLocal(getSettings());
    uint8_t mac[6];
    net::ownMac(mac);
    Serial.printf("MorseChan start (%s): MAC ", board::NAME);
    printMac(mac);
    Serial.printf(" espnow=%s version=%s\r\n", ok ? "ok" : "FAILED", VERSION);
    printSettings();
    Serial.print("chan> ");
}

void loop() {
    uint32_t now = millis();
    pollSerial();
    drainEngine(now);
    handleMessages(now);
    bool txBusy = engine::txBusy();
    runDialog(now, txBusy);
    if (!txBusy && !txEcho.empty()) {
        if (logOn) Serial.printf("TXCHR \"%s\"\r\n", txEcho.c_str());
        txEcho.clear();
    }

    lockShared();
    Snapshot sn = g_snapshot;
    unlockShared();
    if (robot::headTapped()) {
        Serial.print("head tapped\r\n");
        noSignalSince = now;
        if (robot::dozing())
            robot::setDozing(false, now);  // wake up with a start
        else
            robot::react(
                {bot::Emotion::Happy, bot::Gesture::Nod, bot::Led::Happy}, now);
    }
    handleScreen(now);
    updateDoze(now, sn, txBusy);
    robot::update(now, sn.localDown, sn.remoteDown,
                  dialog.state() == chat::State::Idle && !txBusy);

    if (now - lastStatusMs >= STATUS_INTERVAL_MS) {
        lastStatusMs = now;
        robot::Status st;
        Settings s = getSettings();
        st.channel = s.channel;
        st.stations = sn.stations;
        st.link = sn.stations > 0;
        st.rx = sn.remoteDown || assembler.active();
        st.tx = txBusy;
        strncpy(st.state, chat::stateName(dialog.state()),
                sizeof(st.state) - 1);
        strncpy(st.lastWord, lastWord, sizeof(st.lastWord) - 1);
        robot::setStatus(st);
    }
    if (statsOn && now - lastStatsMs >= STATS_INTERVAL_MS) {
        lastStatsMs = now;
        printStats();
    }
    delay(2);
}
