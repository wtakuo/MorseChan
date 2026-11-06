// net.cpp - ESP-NOW (broadcast) send/receive. Adapted from MorseComm's
// net.cpp / net_espnow.cpp (same Wi-Fi settings and callbacks)
#include "net.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include "config.h"

namespace net {
namespace {

constexpr int RX_QUEUE_LEN = 32;
const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

QueueHandle_t rxQueue = nullptr;
portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;
Stats st;

template <typename F>
void updateStats(F f) {
    portENTER_CRITICAL(&statsMux);
    f(st);
    portEXIT_CRITICAL(&statsMux);
}

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    RxItem item;
    item.rxMs = millis();
    if (len <= 0 || len > (int)sizeof(item.data)) {
        updateStats([](Stats& s) { s.rxBad++; });
        return;
    }
    item.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
    item.len = (uint8_t)len;
    memcpy(item.src, info->src_addr, 6);
    memcpy(item.data, data, len);
    if (xQueueSend(rxQueue, &item, 0) != pdTRUE)
        updateStats([](Stats& s) { s.rxDropped++; });
}

void onSent(const esp_now_send_info_t* info, esp_now_send_status_t status) {
    (void)info;
    bool ok = status == ESP_NOW_SEND_SUCCESS;
    updateStats([ok](Stats& s) { ok ? s.cbOk++ : s.cbFail++; });
}

}  // namespace

bool begin() {
    rxQueue = xQueueCreate(RX_QUEUE_LEN, sizeof(RxItem));
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    // Disable power saving to reduce receive latency
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_channel(cfg::ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() != ESP_OK) return false;
    esp_now_register_recv_cb(onRecv);
    esp_now_register_send_cb(onSent);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST, 6);
    peer.channel = cfg::ESPNOW_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    return esp_now_add_peer(&peer) == ESP_OK;
}

bool send(const uint8_t* data, size_t len) {
    bool ok = esp_now_send(BROADCAST, data, len) == ESP_OK;
    updateStats([ok](Stats& s) { ok ? s.sent++ : s.sendErr++; });
    return ok;
}

bool receive(RxItem& item) {
    return rxQueue && xQueueReceive(rxQueue, &item, 0) == pdTRUE;
}

Stats stats() {
    portENTER_CRITICAL(&statsMux);
    Stats s = st;
    portEXIT_CRITICAL(&statsMux);
    return s;
}

void ownMac(uint8_t mac[6]) { esp_read_mac(mac, ESP_MAC_WIFI_STA); }

}  // namespace net
