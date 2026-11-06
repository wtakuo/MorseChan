// assembler.h - collects decoded characters into messages
// Arduino-independent. MorseComm has no end-of-message marker (it sends a
// continuous keying stream), so a message ends when the sending station has
// been silent (no keying, no new character) for endGapMs.
//
// While a message is being assembled the assembler is locked to the station
// that started it; characters and keying from other stations are ignored (and
// counted) until the message ends.
#pragma once
#include <stdint.h>
#include <string.h>
#include "config.h"

namespace chat {

class MessageAssembler {
  public:
    static constexpr int CAP = cfg::MSG_MAX_LEN + 1;

    void setEndGap(uint32_t ms) { endGapMs_ = ms; }
    uint32_t endGap() const { return endGapMs_; }
    bool active() const { return active_; }
    int peer() const { return peer_; }
    uint32_t ignored() const { return ignored_; }
    const char* text() const { return buf_; }  // message so far

    void reset() {
        active_ = false;
        len_ = 0;
        buf_[0] = 0;
    }

    // Keying (a played-back DOWN/UP transition) of a station. Keeps the
    // message open; also starts one (so the robot can show it is listening
    // before the first character is decoded)
    void onKey(int peer, uint32_t nowMs) {
        if (!accept(peer, nowMs)) return;
        lastMs_ = nowMs;
    }

    // A decoded character (' ' = word gap)
    void onChar(int peer, char c, uint32_t nowMs) {
        if (!accept(peer, nowMs)) return;
        lastMs_ = nowMs;
        if (c == ' ' && (len_ == 0 || buf_[len_ - 1] == ' ')) return;
        if (len_ < CAP - 1) {
            buf_[len_++] = c;
            buf_[len_] = 0;
        }
        if (len_ >= CAP - 1) full_ = true;
    }

    // Returns true and the finished message when the sender has been silent
    // long enough (or the buffer is full). The text may be empty when there
    // was only keying that did not decode into characters
    bool poll(uint32_t nowMs, char* out, int cap, int& peer) {
        if (!active_) return false;
        if (!full_ && (nowMs - lastMs_) < endGapMs_) return false;
        active_ = false;
        full_ = false;
        // Trim the trailing space
        while (len_ > 0 && buf_[len_ - 1] == ' ') buf_[--len_] = 0;
        int n = len_ < cap - 1 ? len_ : cap - 1;
        memcpy(out, buf_, n);
        out[n] = 0;
        peer = peer_;
        len_ = 0;
        buf_[0] = 0;
        return true;
    }

  private:
    bool accept(int peer, uint32_t nowMs) {
        if (!active_) {
            active_ = true;
            peer_ = peer;
            len_ = 0;
            buf_[0] = 0;
            lastMs_ = nowMs;
            return true;
        }
        if (peer != peer_) {
            ignored_++;
            return false;
        }
        return true;
    }

    uint32_t endGapMs_ = cfg::MSG_END_GAP_DEFAULT_MS;
    bool active_ = false;
    bool full_ = false;
    int peer_ = -1;
    uint32_t lastMs_ = 0;
    uint32_t ignored_ = 0;
    char buf_[CAP] = {};
    int len_ = 0;
};

}  // namespace chat
