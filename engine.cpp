// engine.cpp - Morse engine task (adapted from MorseComm's engine.cpp)
// Transmission: text from g_typeBuf -> StreamKeyer -> TxHistory /
// TxScheduler -> packets (identical to MorseComm's terminal mode / stored
// message, so MorseComm plays and decodes it as usual).
// Reception: packets -> PeerTable (Receiver + MorseDecoder per station).
#include "engine.h"
#include <Arduino.h>
#include <esp_random.h>
#include <string.h>
#include "config.h"
#include "morse_decoder.h"
#include "peers.h"
#include "protocol.h"
#include "tone_out.h"

// ---- Shared data (declared in shared.h) ----
Settings g_settings;
uint32_t g_settingsVer = 0;
Snapshot g_snapshot;
cw::TypeBuffer g_typeBuf;
bool g_keyerBusy = false;

namespace {
portMUX_TYPE sharedMux = portMUX_INITIALIZER_UNLOCKED;
}
void lockShared() { portENTER_CRITICAL(&sharedMux); }
void unlockShared() { portEXIT_CRITICAL(&sharedMux); }

namespace engine {
namespace {

constexpr int OUT_QUEUE_LEN = 128;
// Higher than loop() (1) and the Avatar tasks (1, 2); lower than Wi-Fi
constexpr UBaseType_t TASK_PRIORITY = 5;
constexpr uint32_t TASK_STACK = 6144;
constexpr uint32_t SNAPSHOT_INTERVAL_MS = 20;

QueueHandle_t outQ = nullptr;
ToneOut toneOut;

cw::StreamKeyer streamKeyer;
cw::TxHistory hist;
cw::TxScheduler sched;
cw::PeerTable peers;
cw::MorseDecoder decLocal;

Settings set;
uint32_t setVer = 0xFFFFFFFF;

uint16_t pktSeq = 0;
uint32_t txPackets = 0;
uint32_t filtered = 0;
uint8_t localState = 0;
uint32_t lastSnapshotMs = 0;
uint16_t curHz = 0;
uint32_t previewUntil = 0;  // preview beep after a volume / tone change

void out(const OutItem& it) { xQueueSend(outQ, &it, 0); }

// Tone: our transmission at txHz; received keying at rxHz if enabled
void updateTone(uint32_t now) {
    bool tx = localState;
    bool rx = set.rxTone && peers.anyDown();
    bool preview = previewUntil && (int32_t)(now - previewUntil) < 0;
    if (!preview) previewUntil = 0;
    if (!tx && !rx && !preview) {
        toneOut.off();
        return;
    }
    uint16_t hz = (tx || preview) ? set.txHz : set.rxHz;
    if (hz != curHz) {
        toneOut.setFrequency(hz);
        curHz = hz;
    }
    toneOut.on();
}

void setLocal(uint8_t st, uint32_t t) {
    if (!hist.push(st, t)) return;
    sched.onTransition(st, t);
    decLocal.onTransition(st, t);
    localState = st;
}

void syncSettings(uint32_t now) {
    bool first = setVer == 0xFFFFFFFF;
    lockShared();
    bool changed = g_settingsVer != setVer;
    Settings old = set;
    if (changed) {
        set = g_settings;
        setVer = g_settingsVer;
    }
    unlockShared();
    if (!changed) return;
    streamKeyer.setWpm(set.wpm);
    toneOut.setVolume(set.volume);
    if (set.channel != old.channel) peers.clear();
    if (!first && (set.volume != old.volume || set.txHz != old.txHz)) {
        previewUntil = now + cfg::PREVIEW_MS;
        if (!previewUntil) previewUntil = 1;
    }
}

// Send queued text one unit at a time (as MorseComm's terminal mode does)
void sendText(uint32_t now) {
    char unit[cw::TypeBuffer::MAX_UNIT + 1];
    while (streamKeyer.ready()) {
        lockShared();
        bool got = g_typeBuf.takeUnit(unit);
        if (got) g_keyerBusy = true;
        unlockShared();
        if (!got) break;
        streamKeyer.send(unit, now);
    }
    streamKeyer.update(now);
    cw::KeyEvent ev;
    while (streamKeyer.pop(ev)) setLocal(ev.state, ev.t);
    if (streamKeyer.ready()) {
        lockShared();
        if (g_typeBuf.size() == 0) g_keyerBusy = false;
        unlockShared();
    }
}

void transmit(uint32_t now) {
    if (!sched.poll(now)) return;
    cw::Packet p;
    hist.build(p, now, cfg::HIST_N, set.channel, pktSeq++);
    txPackets++;
    uint8_t buf[cw::PKT_MAX_SIZE];
    size_t len = cw::encodePacket(p, buf, sizeof(buf));
    if (len) net::send(buf, len);
}

void receive(uint32_t now) {
    net::RxItem item;
    while (net::receive(item)) {
        cw::Packet p;
        if (!cw::decodePacket(item.data, item.len, p) ||
            p.channel != set.channel)
            continue;
        if (set.peerFilter && memcmp(item.src, set.peerMac, 6) != 0) {
            filtered++;
            continue;
        }
        int idx;
        cw::Peer& peer = peers.get(item.src, item.rxMs, idx);
        peer.rssi = item.rssi;
        peer.lastRxMs = item.rxMs;
        peer.rx.onPacket(p, item.rxMs);
    }
    for (int i = 0; i < cw::PeerTable::MAX; i++) {
        cw::Peer& peer = peers.at(i);
        if (!peer.used) continue;
        peer.rx.update(now);
        cw::PlayedEvent ev;
        while (peer.rx.pop(ev)) {
            peer.dec.onTransition(ev.state, ev.playMs);
            OutItem it{OutItem::KeyRx, 0, ev.state, (uint8_t)i, {}, now};
            memcpy(it.mac, peer.mac, 6);
            out(it);
            peer.lastPlayMs = now;
        }
    }
}

void decode(uint32_t now) {
    char c;
    decLocal.update(now);
    while (decLocal.pop(c)) out(OutItem{OutItem::CharTx, c, 0, 0, {}, now});
    for (int i = 0; i < cw::PeerTable::MAX; i++) {
        cw::Peer& peer = peers.at(i);
        if (!peer.used) continue;
        peer.dec.update(now);
        while (peer.dec.pop(c)) {
            OutItem it{OutItem::CharRx, c, 0, (uint8_t)i, {}, now};
            memcpy(it.mac, peer.mac, 6);
            out(it);
        }
    }
}

void publishSnapshot(uint32_t now) {
    if (now - lastSnapshotMs < SNAPSHOT_INTERVAL_MS) return;
    lastSnapshotMs = now;
    Snapshot s;
    s.rx = peers.totalStats();
    s.net = net::stats();
    if (const cw::Peer* p = peers.latestRx()) s.rssi = p->rssi;
    s.stations = peers.activeCount(now);
    s.localDown = localState;
    s.remoteDown = peers.anyDown();
    s.txPackets = txPackets;
    s.filtered = filtered;
    s.nowMs = now;
    lockShared();
    g_snapshot = s;
    unlockShared();
}

void task(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        uint32_t now = millis();
        syncSettings(now);
        sendText(now);
        transmit(now);
        receive(now);
        updateTone(now);
        decode(now);
        publishSnapshot(now);
        vTaskDelayUntil(&last, 1);
    }
}

}  // namespace

bool begin() {
    outQ = xQueueCreate(OUT_QUEUE_LEN, sizeof(OutItem));
    lockShared();
    Settings s0 = g_settings;
    unlockShared();
    toneOut.setVolume(s0.volume);
    toneOut.begin(s0.txHz);
    curHz = s0.txHz;
    hist = cw::TxHistory((uint16_t)esp_random());
    pktSeq = (uint16_t)esp_random();
    bool ok = net::begin();
    xTaskCreate(task, "cw", TASK_STACK, nullptr, TASK_PRIORITY, nullptr);
    return ok;
}

bool popOut(OutItem& item) {
    return outQ && xQueueReceive(outQ, &item, 0) == pdTRUE;
}

bool send(const char* text) {
    lockShared();
    bool ok = g_typeBuf.space() >= (int)strlen(text) + 1;
    if (ok) {
        // Separate from text still waiting with a word gap
        if (g_typeBuf.size() > 0 && g_typeBuf.back() != ' ')
            g_typeBuf.push(' ');
        for (const char* p = text; *p; p++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            g_typeBuf.push(c);
        }
    }
    unlockShared();
    return ok;
}

void cancelSend() {
    lockShared();
    g_typeBuf.clear();
    unlockShared();
}

bool txBusy() {
    lockShared();
    bool b = g_keyerBusy || g_typeBuf.size() > 0;
    unlockShared();
    return b;
}

}  // namespace engine
