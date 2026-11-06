// net.h - ESP-NOW send/receive, compatible with MorseComm (adapted from
// MorseComm's net.h / net.cpp / net_espnow.cpp; LoRa and loss injection
// removed).
// Broadcast (FF:FF:FF:FF:FF:FF) on the fixed Wi-Fi channel cfg::ESPNOW_CHANNEL,
// as MorseComm does. The receive callback only records the receive time and
// pushes the packet to a queue; it is processed in the engine task.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "protocol.h"

namespace net {

struct RxItem {
    uint32_t rxMs;  // millis() at the moment of reception
    int8_t rssi;    // dBm
    uint8_t len;
    uint8_t src[6];  // sender MAC address
    uint8_t data[cw::PKT_MAX_SIZE];
};

struct Stats {
    uint32_t sent = 0;       // esp_now_send succeeded
    uint32_t sendErr = 0;    // esp_now_send failed
    uint32_t cbOk = 0;       // send callback: success
    uint32_t cbFail = 0;     // send callback: failure
    uint32_t rxDropped = 0;  // receive queue overflow
    uint32_t rxBad = 0;      // received frames that are too long
};

bool begin();
bool send(const uint8_t* data, size_t len);
bool receive(RxItem& item);  // take one item from the queue (does not wait)
Stats stats();
void ownMac(uint8_t mac[6]);

}  // namespace net
