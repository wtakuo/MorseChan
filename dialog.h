// dialog.h - rule-based dialog engine (finite-state machine)
// Arduino-independent: time is passed as nowMs, and the results are abstract
// Actions (a reaction, or text to send) that MorseChan.ino hands to the robot
// and to the Morse engine.
//
// States (requirements sec. 4.4):
//   Idle             waiting for a visitor (call-out after a long silence)
//   Receiving        a visitor is keying a message
//   Responding       reacting and sending the reply
//   Asking           sending a question (or the call-out)
//   WaitingForAnswer waiting for the answer to a question
#pragma once
#include <stdint.h>
#include <string.h>
#include "actions.h"
#include "config.h"
#include "dialog_rules.h"
#include "recognizer.h"

namespace chat {

enum class State : uint8_t {
    Idle,
    Receiving,
    Responding,
    Asking,
    WaitingForAnswer,
};

inline const char* stateName(State s) {
    switch (s) {
        case State::Idle:
            return "Idle";
        case State::Receiving:
            return "Receiving";
        case State::Responding:
            return "Responding";
        case State::Asking:
            return "Asking";
        case State::WaitingForAnswer:
            return "WaitingForAnswer";
    }
    return "?";
}

struct Action {
    enum Kind : uint8_t { React, Send } kind = React;
    bot::Reaction reaction;
    char text[cfg::REPLY_MAX + 1] = {};
};

class Dialog {
  public:
    Dialog() : rec_(PHRASES, PHRASE_COUNT) {}

    void begin(uint32_t nowMs) {
        state_ = State::Idle;
        lastInteractionMs_ = nowMs;
        calloutCount_ = 0;
        calledOut_ = false;
        waitingQ_ = -1;
        nextQ_ = -1;
        pendingAsk_ = -1;
        hasSend_ = false;
        txWait_ = false;
        deferred_[0] = 0;
        hasDeferred_ = false;
        aHead_ = aTail_ = 0;
    }

    // Idle call-out interval in ms (0 disables it)
    void setCallout(uint32_t ms) { calloutMs_ = ms; }
    // Whether a MorseComm station is on the channel. Without one there is
    // nobody to call out to; when one appears, the call-out interval starts
    // again
    void setPresence(bool present, uint32_t nowMs) {
        if (present && !present_) {
            lastInteractionMs_ = nowMs;
            calloutCount_ = 0;
        }
        present_ = present;
    }
    // Seed for choosing reply variants (0 is replaced by 1)
    void setSeed(uint32_t seed) { rng_ = seed ? seed : 1; }

    State state() const { return state_; }
    const Result& last() const { return last_; }
    // Question being waited for (-1 if none)
    int waitingQuestion() const { return waitingQ_; }
    const Recognizer& recognizer() const { return rec_; }

    // A visitor is keying (called for each received key transition)
    void onRxActivity(uint32_t nowMs) {
        lastRxMs_ = nowMs;
        if (state_ == State::Idle || state_ == State::WaitingForAnswer) {
            prevState_ = state_;
            setState(State::Receiving);
            pushReact(
                {bot::Emotion::Thinking, bot::Gesture::None, bot::Led::Keep});
        }
    }

    // A finished message (may be empty: keying that did not decode)
    void onMessage(const char* raw, uint32_t nowMs) {
        if (state_ == State::Responding || state_ == State::Asking) {
            // We are talking: handle it when our transmission is over
            if (raw[0]) {
                strncpy(deferred_, raw, sizeof(deferred_) - 1);
                deferred_[sizeof(deferred_) - 1] = 0;
                hasDeferred_ = true;
            }
            return;
        }
        handleMessage(raw, nowMs);
    }

    // Report whether the Morse engine is still sending (call every loop)
    void onTxStatus(bool busy, uint32_t nowMs) {
        if (!txWait_) return;
        if (busy) {
            txSeenBusy_ = true;
            return;
        }
        // Done when it has been busy and is idle again. Safety nets: text that
        // never started (nothing sendable) and a transmission that never ends
        bool started = txSeenBusy_ || (nowMs - txStartMs_) > 300;
        if (started || (nowMs - txStartMs_) > cfg::TX_GUARD_MS) {
            txWait_ = false;
            onTxDone(nowMs);
        }
    }

    void tick(uint32_t nowMs) {
        switch (state_) {
            case State::Receiving:
                if ((nowMs - lastRxMs_) >= cfg::RECEIVE_TIMEOUT_MS)
                    setState(prevState_);
                break;
            case State::Responding:
            case State::Asking:
                if (hasSend_ && (int32_t)(nowMs - sendAtMs_) >= 0) {
                    hasSend_ = false;
                    Action a;
                    a.kind = Action::Send;
                    strncpy(a.text, sendText_, sizeof(a.text) - 1);
                    push(a);
                    txWait_ = true;
                    txSeenBusy_ = false;
                    txStartMs_ = nowMs;
                }
                if (txWait_ && (nowMs - txStartMs_) > cfg::TX_GUARD_MS) {
                    txWait_ = false;
                    onTxDone(nowMs);
                }
                break;
            case State::WaitingForAnswer:
                if ((int32_t)(nowMs - answerDeadline_) >= 0) {
                    waitingQ_ = -1;
                    pushReact(NO_ANSWER_REACTION);
                    lastInteractionMs_ = nowMs;
                    setState(State::Idle);
                }
                break;
            case State::Idle:
                if (calloutMs_ && present_ &&
                    calloutCount_ < cfg::CALLOUT_MAX &&
                    (nowMs - lastInteractionMs_) >= calloutMs_) {
                    calloutCount_++;
                    calledOut_ = true;
                    calloutAtMs_ = nowMs;
                    lastInteractionMs_ = nowMs;
                    pushReact({bot::Emotion::Happy, bot::Gesture::LookAround,
                               bot::Led::Happy});
                    nextQ_ = -1;
                    pendingAsk_ = -1;
                    scheduleSend(CALLOUT_TEXT, nowMs + cfg::REPLY_DELAY_MS);
                    setState(State::Asking);
                }
                break;
        }
    }

    bool popAction(Action& a) {
        if (aHead_ == aTail_) return false;
        a = actions_[aHead_];
        aHead_ = (aHead_ + 1) % ACTION_Q;
        return true;
    }

    // Number of state changes (for tests / logs)
    uint32_t transitions() const { return transitions_; }

  private:
    static constexpr int ACTION_Q = 8;

    void setState(State s) {
        if (s != state_) transitions_++;
        state_ = s;
    }

    void push(const Action& a) {
        int next = (aTail_ + 1) % ACTION_Q;
        if (next == aHead_) aHead_ = (aHead_ + 1) % ACTION_Q;
        actions_[aTail_] = a;
        aTail_ = next;
    }

    void pushReact(const bot::Reaction& r) {
        Action a;
        a.kind = Action::React;
        a.reaction = r;
        push(a);
    }

    // Pseudo-random numbers (xorshift32) for choosing reply variants
    uint32_t nextRandom() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return rng_;
    }

    // text may list '|'-separated variants; one is chosen at random
    void scheduleSend(const char* text, uint32_t atMs) {
        int n = variantCount(text);
        variantAt(text, n > 1 ? (int)(nextRandom() % n) : 0, sendText_,
                  sizeof(sendText_));
        sendAtMs_ = atMs;
        hasSend_ = true;
    }

    static const Rule* findRule(Intent i) {
        for (int k = 0; k < RULE_COUNT; k++)
            if (RULES[k].intent == i) return &RULES[k];
        return nullptr;
    }

    int takeNextQuestion() {
        qIndex_ = (qIndex_ + 1) % QUESTION_COUNT;
        return qIndex_;
    }

    void handleMessage(const char* raw, uint32_t nowMs) {
        last_ = rec_.recognize(raw);
        if (last_.kind == MatchKind::Empty) {
            // Noise: go back to what we were doing
            if (state_ == State::Receiving) setState(prevState_);
            if (state_ == State::WaitingForAnswer) pushReact({});
            return;
        }
        lastInteractionMs_ = nowMs;
        calloutCount_ = 0;
        nextQ_ = -1;
        keepWaiting_ = false;

        // Answer to our question?
        if (waitingQ_ >= 0 && last_.matched() &&
            (isPositive(last_.intent) || isNegative(last_.intent))) {
            const Question& q = QUESTIONS[waitingQ_];
            bool pos = isPositive(last_.intent);
            pushReact(pos ? q.positive : q.negative);
            scheduleSend(pos ? q.positiveReply : q.negativeReply,
                         nowMs + cfg::REPLY_DELAY_MS);
            waitingQ_ = -1;
            setState(State::Responding);
            return;
        }

        const Rule* rule = last_.matched() ? findRule(last_.intent) : nullptr;
        if (!rule) {
            // Not understood: show it; a pending question stays pending
            rule = &UNKNOWN_RULE;
            keepWaiting_ = waitingQ_ >= 0;
        } else {
            waitingQ_ = -1;
        }
        pushReact(rule->reaction);
        if (rule->ask) nextQ_ = takeNextQuestion();
        // A greeting that answers our call-out: go straight to the question
        bool answersCallout = rule->ask && calledOut_ &&
                              (nowMs - calloutAtMs_) < cfg::GREET_WINDOW_MS;
        calledOut_ = false;
        if (answersCallout) {
            scheduleSend(QUESTIONS[nextQ_].text, nowMs + cfg::REPLY_DELAY_MS);
            pendingAsk_ = nextQ_;
            nextQ_ = -1;
            setState(State::Asking);
            return;
        }
        pendingAsk_ = -1;
        if (rule->reply) scheduleSend(rule->reply, nowMs + cfg::REPLY_DELAY_MS);
        setState(State::Responding);
        if (!rule->reply) onTxDone(nowMs);
    }

    void onTxDone(uint32_t nowMs) {
        lastInteractionMs_ = nowMs;
        if (state_ == State::Responding && nextQ_ >= 0) {
            // Follow-up question
            pendingAsk_ = nextQ_;
            nextQ_ = -1;
            scheduleSend(QUESTIONS[pendingAsk_].text,
                         nowMs + cfg::QUESTION_DELAY_MS);
            setState(State::Asking);
            return;
        }
        if (state_ == State::Asking && pendingAsk_ >= 0) {
            waitingQ_ = pendingAsk_;
            pendingAsk_ = -1;
            answerDeadline_ = nowMs + cfg::ANSWER_TIMEOUT_MS;
            setState(State::WaitingForAnswer);
        } else if (keepWaiting_ && waitingQ_ >= 0) {
            keepWaiting_ = false;
            answerDeadline_ = nowMs + cfg::ANSWER_TIMEOUT_MS;
            setState(State::WaitingForAnswer);
        } else {
            setState(State::Idle);
        }
        if (hasDeferred_) {
            hasDeferred_ = false;
            handleMessage(deferred_, nowMs);
        }
    }

    Recognizer rec_;
    Result last_;
    State state_ = State::Idle;
    State prevState_ = State::Idle;
    uint32_t transitions_ = 0;
    bool present_ = true;
    uint32_t rng_ = 2463534242u;

    uint32_t calloutMs_ = cfg::CALLOUT_DEFAULT_S * 1000;
    int calloutCount_ = 0;
    bool calledOut_ = false;
    uint32_t calloutAtMs_ = 0;
    uint32_t lastInteractionMs_ = 0;
    uint32_t lastRxMs_ = 0;

    int qIndex_ = -1;      // last question asked (rotates)
    int nextQ_ = -1;       // question to ask after the current reply
    int pendingAsk_ = -1;  // question being sent
    int waitingQ_ = -1;    // question waiting for an answer
    bool keepWaiting_ = false;
    uint32_t answerDeadline_ = 0;

    bool hasSend_ = false;
    char sendText_[cfg::REPLY_MAX + 1] = {};
    uint32_t sendAtMs_ = 0;
    bool txWait_ = false;
    bool txSeenBusy_ = false;
    uint32_t txStartMs_ = 0;

    char deferred_[cfg::MSG_MAX_LEN + 1] = {};
    bool hasDeferred_ = false;

    Action actions_[ACTION_Q];
    int aHead_ = 0, aTail_ = 0;
};

}  // namespace chat
