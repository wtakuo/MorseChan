// dialog_rules.h - the conversation data: phrases, replies, questions
// Arduino-independent. Edit this file to change what StackChan understands and
// says (see README "Changing the dialog rules").
//
// Replies and questions are sent in Morse code, so keep them short. Only
// characters in morse_table.h can be sent (A-Z, 0-9 and . , ? / = + - ( ) ' :
// ; " @ ! _ &).
//
// A reply may list variants separated by '|' ("FB|GOOD"): one of them is
// picked at random each time. Use this to mix ham radio words with plain
// words.
#pragma once
#include "actions.h"
#include "recognizer.h"

namespace chat {

using bot::Emotion;
using bot::Gesture;
using bot::Led;

inline const char* intentName(Intent i) {
    static const char* const n[(int)Intent::COUNT] = {
        "none", "hello",  "gm",   "ga", "ge",  "howareyou",
        "fine", "bad",    "yes",  "no", "ok",  "sos",
        "bye",  "thanks", "name", "cq", "cute"};
    return (int)i < (int)Intent::COUNT ? n[(int)i] : "?";
}

// ---- Phrases (upper case, single spaces) ----
// Several phrases may map to the same intent. Phrases that become identical
// once spaces are removed must have the same intent (checked by the host
// tests)
constexpr Phrase PHRASES[] = {
    {Intent::Hello, "HELLO"},
    {Intent::Hello, "HI"},
    {Intent::Hello, "HEY"},
    {Intent::Hello, "HELO"},
    {Intent::Hello, "HI HI"},
    {Intent::GoodMorning, "GM"},
    {Intent::GoodMorning, "GOOD MORNING"},
    {Intent::GoodAfternoon, "GA"},
    {Intent::GoodAfternoon, "GOOD AFTERNOON"},
    {Intent::GoodEvening, "GE"},
    {Intent::GoodEvening, "GOOD EVENING"},
    {Intent::HowAreYou, "HOW ARE YOU"},
    {Intent::HowAreYou, "HOW ARE U"},
    {Intent::HowAreYou, "HOW R U"},
    {Intent::HowAreYou, "HRU"},
    {Intent::Fine, "FINE"},
    {Intent::Fine, "GOOD"},
    {Intent::Fine, "GREAT"},
    {Intent::Fine, "FB"},
    {Intent::Fine, "I AM FINE"},
    {Intent::Fine, "IM FINE"},
    {Intent::Fine, "VERY GOOD"},
    {Intent::Fine, "HAPPY"},
    {Intent::Bad, "BAD"},
    {Intent::Bad, "NOT GOOD"},
    {Intent::Bad, "NG"},
    {Intent::Bad, "TIRED"},
    {Intent::Bad, "SAD"},
    {Intent::Yes, "YES"},
    {Intent::Yes, "YEAH"},
    {Intent::Yes, "YEP"},
    {Intent::No, "NO"},
    {Intent::No, "NOPE"},
    {Intent::Ok, "OK"},
    {Intent::Ok, "OKAY"},
    {Intent::Ok, "ROGER"},
    {Intent::Sos, "SOS"},
    {Intent::Bye, "73"},
    {Intent::Bye, "BYE"},
    {Intent::Bye, "BYE BYE"},
    {Intent::Bye, "GOODBYE"},
    {Intent::Bye, "SEE YOU"},
    {Intent::Bye, "CU"},
    {Intent::Bye, "CUL"},
    {Intent::Bye, "GN"},
    {Intent::Thanks, "TNX"},
    {Intent::Thanks, "THANKS"},
    {Intent::Thanks, "THANK YOU"},
    {Intent::Thanks, "TU"},
    {Intent::Thanks, "TKS"},
    {Intent::Thanks, "TY"},
    {Intent::Name, "WHO ARE YOU"},
    {Intent::Name, "NAME"},
    {Intent::Name, "YOUR NAME"},
    {Intent::Name, "WHAT IS YOUR NAME"},
    {Intent::Cq, "CQ"},
    {Intent::Cq, "CQ CQ"},
    {Intent::Cute, "CUTE"},
    {Intent::Cute, "KAWAII"},
    {Intent::Cute, "LOVE"},
    {Intent::Cute, "I LOVE YOU"},
};
constexpr int PHRASE_COUNT = sizeof(PHRASES) / sizeof(PHRASES[0]);

// ---- Rules: what to do when an intent is received in normal conversation ----
// ask = true: after the reply, StackChan asks one of QUESTIONS.
// reply = nullptr: no reply (reaction only)
struct Rule {
    Intent intent;
    bot::Reaction reaction;
    const char* reply;
    bool ask;
};

constexpr Rule RULES[] = {
    {Intent::Hello,
     {Emotion::Happy, Gesture::Bow, Led::Happy},
     "HI|HELLO",
     true},
    {Intent::GoodMorning,
     {Emotion::Happy, Gesture::Bow, Led::Happy},
     "GM|MORNING",
     true},
    {Intent::GoodAfternoon,
     {Emotion::Happy, Gesture::Bow, Led::Happy},
     "GA|HELLO",
     true},
    {Intent::GoodEvening,
     {Emotion::Happy, Gesture::Bow, Led::Happy},
     "GE|EVENING",
     true},
    {Intent::HowAreYou,
     {Emotion::Happy, Gesture::Nod, Led::Happy},
     "FINE|GOOD",
     false},
    {Intent::Fine,
     {Emotion::Happy, Gesture::Nod, Led::Happy},
     "FB|GOOD",
     false},
    {Intent::Bad, {Emotion::Sad, Gesture::None, Led::Sad}, "OH|OH NO", false},
    {Intent::Yes,
     {Emotion::Excited, Gesture::Nod, Led::Excited},
     "OK|GREAT",
     false},
    {Intent::No, {Emotion::Sad, Gesture::Shake, Led::Sad}, "OH", false},
    {Intent::Ok, {Emotion::Happy, Gesture::Nod, Led::Happy}, "FB|GOOD", false},
    {Intent::Sos,
     {Emotion::Surprised, Gesture::LookAround, Led::Alert},
     "SOS?|HELP?",
     false},
    {Intent::Bye, {Emotion::Happy, Gesture::Bow, Led::Happy}, "73|BYE", false},
    {Intent::Thanks,
     {Emotion::Happy, Gesture::Bow, Led::Happy},
     "TU|THANKS",
     false},
    {Intent::Name,
     {Emotion::Happy, Gesture::Nod, Led::Happy},
     "STACKCHAN",
     false},
    {Intent::Cq,
     {Emotion::Excited, Gesture::Wiggle, Led::Excited},
     "DE STACKCHAN K",
     false},
    {Intent::Cute,
     {Emotion::Excited, Gesture::Wiggle, Led::Excited},
     "TU|THANKS",
     false},
};
constexpr int RULE_COUNT = sizeof(RULES) / sizeof(RULES[0]);

// Reaction to a message that was not understood (or was ambiguous)
constexpr Rule UNKNOWN_RULE = {
    Intent::None,
    {Emotion::Confused, Gesture::Tilt, Led::Confused},
    "?|WHAT?",
    false};

// ---- Questions asked by StackChan ----
// An answer is positive (YES, OK, FINE, ...) or negative (NO, BAD); anything
// else is handled by the normal RULES (so HELLO, SOS, 73 still work while a
// question is pending)
struct Question {
    const char* text;
    bot::Reaction positive;
    const char* positiveReply;
    bot::Reaction negative;
    const char* negativeReply;
};

constexpr Question QUESTIONS[] = {
    {"ENJOY?",
     {Emotion::Excited, Gesture::Wiggle, Led::Excited},
     "FB!|GOOD!",
     {Emotion::Sad, Gesture::Shake, Led::Sad},
     "OH"},
    {"FUN?",
     {Emotion::Excited, Gesture::Wiggle, Led::Excited},
     "FB!|GOOD!",
     {Emotion::Sad, Gesture::Shake, Led::Sad},
     "OH"},
    {"HOW ARE YOU?",
     {Emotion::Happy, Gesture::Nod, Led::Happy},
     "FB|GOOD",
     {Emotion::Sad, Gesture::None, Led::Sad},
     "OH"},
    {"OK?",
     {Emotion::Happy, Gesture::Nod, Led::Happy},
     "FB|GOOD",
     {Emotion::Sad, Gesture::Shake, Led::Sad},
     "OH"},
};
constexpr int QUESTION_COUNT = sizeof(QUESTIONS) / sizeof(QUESTIONS[0]);

// Answer classes
inline bool isPositive(Intent i) {
    return i == Intent::Yes || i == Intent::Ok || i == Intent::Fine ||
           i == Intent::Cute;
}
inline bool isNegative(Intent i) { return i == Intent::No || i == Intent::Bad; }

// Number of '|'-separated variants in a reply
inline int variantCount(const char* s) {
    int n = 1;
    for (; *s; s++)
        if (*s == '|') n++;
    return n;
}

// Copy variant k (0-based) of a reply
inline void variantAt(const char* s, int k, char* out, int cap) {
    for (; k > 0 && *s; s++)
        if (*s == '|') k--;
    int n = 0;
    for (; *s && *s != '|' && n < cap - 1; s++) out[n++] = *s;
    out[n] = 0;
}

// Call-out sent after a long idle time
constexpr const char* CALLOUT_TEXT = "HI|HELLO";

// When no answer comes, StackChan just looks a bit sad (no transmission)
constexpr bot::Reaction NO_ANSWER_REACTION = {Emotion::Sad, Gesture::None,
                                              Led::Idle};

}  // namespace chat
