// shared.h - data shared between the Morse engine task (engine.cpp) and
// loop() (dialog, robot, serial). Always read/write the shared structures
// between lockShared()/unlockShared().
#pragma once
#include <stdint.h>
#include "config.h"
#include "jitter.h"
#include "keyer.h"
#include "net.h"

// Persistent settings (Preferences namespace "morsechan")
struct Settings {
    uint8_t channel = cfg::CH_DEFAULT;  // MorseComm communication channel
    uint8_t wpm = cfg::WPM_DEFAULT;     // transmit speed
    uint16_t txHz = cfg::TX_HZ_DEFAULT;
    uint16_t rxHz = cfg::RX_HZ_DEFAULT;
    uint8_t rxTone = 0;  // also play received keying on the speaker
    uint8_t volume = cfg::VOLUME_DEFAULT;
    uint8_t peerFilter = 0;  // 1 = accept only peerMac
    uint8_t peerMac[6] = {};
    uint8_t motion = cfg::MOTION_DEFAULT;        // 0 off, 1 small, 2 normal
    uint8_t ledLevel = cfg::LED_LEVEL_DEFAULT;   // 0..3
    uint16_t calloutS = cfg::CALLOUT_DEFAULT_S;  // 0 = no call-out
    uint16_t endGapMs = cfg::MSG_END_GAP_DEFAULT_MS;
    uint16_t dozeS = cfg::DOZE_DEFAULT_S;  // 0 = never doze
};

// Engine -> loop state snapshot (updated every 20ms)
struct Snapshot {
    cw::RxStats rx;
    net::Stats net;
    int8_t rssi = 0;
    int stations = 0;         // stations with a live link
    bool localDown = false;   // our key (transmission) is DOWN
    bool remoteDown = false;  // a received station is DOWN (playback)
    uint32_t txPackets = 0;
    uint32_t filtered = 0;  // packets dropped by the peer filter
    uint32_t nowMs = 0;
};

// Engine -> loop events
struct OutItem {
    enum Kind : uint8_t {
        CharRx,  // decoded received character (c), from station peer
        KeyRx,   // played-back key transition (state) of station peer
        CharTx,  // our own transmission decoded locally (for the log)
    } kind;
    char c;
    uint8_t state;
    uint8_t peer;  // slot in the PeerTable
    uint8_t mac[6];
    uint32_t t;
};

void lockShared();
void unlockShared();

extern Settings g_settings;
extern uint32_t g_settingsVer;  // incremented on every change
extern Snapshot g_snapshot;
// Text waiting to be sent (loop appends, engine takes it unit by unit)
extern cw::TypeBuffer g_typeBuf;
// The engine is sending a unit (set together with taking it from g_typeBuf)
extern bool g_keyerBusy;
