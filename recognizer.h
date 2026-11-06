// recognizer.h - normalizes a received message and matches it against the
// registered phrases (requirements sec. 5). Arduino-independent.
//
// Matching, in order of preference:
//   1. Exact:     normalized words equal a phrase ("HOW ARE YOU")
//   2. Spaces:    equal after removing all spaces ("HEL LO", "HOWAREYOU").
//                 Only used to compare with registered phrases; the received
//                 text itself is never reinterpreted
//   3. Contained: a run of whole words (joined without spaces) equals a
//                 phrase ("HI HI", "TNX 73", "HEL LO STACKCHAN"). Never matches
//                 inside a word ("KNOW" does not contain "NO")
//   4. Fuzzy:     edit distance 1 from a phrase whose length (spaces removed)
//                 is at least cfg::FUZZY_MIN_LEN ("HELLP" -> HELLO). Short
//                 words like YES / NO / OK are never matched approximately
// If two different intents tie, the result is Ambiguous (not guessed).
#pragma once
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "morse_table.h"

namespace chat {

// Intents. To add one: add it here, to INTENT_NAMES, to PHRASES and to RULES
// in dialog_rules.h
enum class Intent : uint8_t {
    None,
    Hello,
    GoodMorning,
    GoodAfternoon,
    GoodEvening,
    HowAreYou,
    Fine,
    Bad,
    Yes,
    No,
    Ok,
    Sos,
    Bye,
    Thanks,
    Name,
    Cq,
    Cute,
    COUNT
};

enum class MatchKind : uint8_t {
    None,  // nothing matched (unknown message)
    Exact,
    Spaces,
    Contained,
    Fuzzy,
    Ambiguous,  // several intents matched equally well
    Empty,      // nothing left after normalization (noise)
};

inline const char* matchKindName(MatchKind k) {
    switch (k) {
        case MatchKind::None:
            return "none";
        case MatchKind::Exact:
            return "exact";
        case MatchKind::Spaces:
            return "spaces";
        case MatchKind::Contained:
            return "contained";
        case MatchKind::Fuzzy:
            return "fuzzy";
        case MatchKind::Ambiguous:
            return "ambiguous";
        case MatchKind::Empty:
            return "empty";
    }
    return "?";
}

struct Phrase {
    Intent intent;
    const char* text;  // upper case, single spaces
};

// Priority when a message contains several intents ("TNX 73" -> Bye).
// Higher wins; equal priorities with different intents are Ambiguous
inline int intentPriority(Intent i) {
    switch (i) {
        case Intent::Sos:
            return 3;
        case Intent::Bye:
            return 2;
        default:
            return 1;
    }
}

constexpr int NORM_MAX = cfg::MSG_MAX_LEN + 1;

struct Result {
    Intent intent = Intent::None;
    MatchKind kind = MatchKind::None;
    char normalized[NORM_MAX] = {};  // normalized text (for logs / display)
    bool matched() const {
        return kind == MatchKind::Exact || kind == MatchKind::Spaces ||
               kind == MatchKind::Contained || kind == MatchKind::Fuzzy;
    }
};

// ---- Normalization ----
// Upper case, prosigns ("[SK]") and punctuation removed, an undecodable
// character (cw::CHAR_UNKNOWN) becomes '#', runs of spaces become one space,
// leading/trailing spaces removed. Letters, digits and '#' are kept
inline void normalize(const char* in, char* out, int cap) {
    int n = 0;
    bool space = false;
    bool inBracket = false;
    for (const char* p = in; *p; p++) {
        char c = *p;
        if (c == '[') {
            inBracket = true;
            continue;
        }
        if (c == ']') {
            inBracket = false;
            space = true;
            continue;
        }
        if (inBracket) continue;
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        if (c == cw::CHAR_UNKNOWN) c = '#';
        bool keep =
            (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '#';
        if (!keep) {
            // Spaces and punctuation separate words
            space = true;
            continue;
        }
        if (space && n > 0 && n < cap - 1) out[n++] = ' ';
        space = false;
        if (n < cap - 1) out[n++] = c;
    }
    out[n] = 0;
}

// Copy without spaces
inline void compact(const char* in, char* out, int cap) {
    int n = 0;
    for (const char* p = in; *p && n < cap - 1; p++)
        if (*p != ' ') out[n++] = *p;
    out[n] = 0;
}

// Edit distance, bounded: returns limit + 1 as soon as it exceeds limit
inline int editDistance(const char* a, const char* b, int limit) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la - lb > limit || lb - la > limit) return limit + 1;
    if (la >= NORM_MAX || lb >= NORM_MAX) return limit + 1;
    int prev[NORM_MAX + 1], cur[NORM_MAX + 1];
    for (int j = 0; j <= lb; j++) prev[j] = j;
    for (int i = 1; i <= la; i++) {
        cur[0] = i;
        int rowMin = cur[0];
        for (int j = 1; j <= lb; j++) {
            int sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            int del = prev[j] + 1;
            int ins = cur[j - 1] + 1;
            int m = sub < del ? sub : del;
            cur[j] = m < ins ? m : ins;
            if (cur[j] < rowMin) rowMin = cur[j];
        }
        if (rowMin > limit) return limit + 1;
        for (int j = 0; j <= lb; j++) prev[j] = cur[j];
    }
    return prev[lb];
}

class Recognizer {
  public:
    Recognizer(const Phrase* phrases, int count)
        : phrases_(phrases), count_(count) {}

    Result recognize(const char* raw) const {
        Result r;
        normalize(raw, r.normalized, sizeof(r.normalized));
        if (!r.normalized[0]) {
            r.kind = MatchKind::Empty;
            return r;
        }
        char cmp[NORM_MAX], pc[NORM_MAX];
        compact(r.normalized, cmp, sizeof(cmp));

        // 1. Exact, 2. Spaces
        Intent exact = Intent::None, spaces = Intent::None;
        bool spacesAmb = false;
        for (int i = 0; i < count_; i++) {
            if (strcmp(r.normalized, phrases_[i].text) == 0) {
                exact = phrases_[i].intent;
                break;
            }
            compact(phrases_[i].text, pc, sizeof(pc));
            if (strcmp(cmp, pc) == 0) {
                if (spaces != Intent::None && spaces != phrases_[i].intent)
                    spacesAmb = true;
                spaces = phrases_[i].intent;
            }
        }
        if (exact != Intent::None) return done(r, exact, MatchKind::Exact);
        if (spacesAmb) return done(r, Intent::None, MatchKind::Ambiguous);
        if (spaces != Intent::None) return done(r, spaces, MatchKind::Spaces);

        // 3. Contained (runs of whole words)
        Intent best = Intent::None;
        bool amb = false;
        containedSearch(r.normalized, best, amb);
        if (amb) return done(r, Intent::None, MatchKind::Ambiguous);
        if (best != Intent::None) return done(r, best, MatchKind::Contained);

        // 4. Fuzzy (whole message, long phrases only)
        Intent fz = Intent::None;
        bool fzAmb = false;
        if ((int)strlen(cmp) >= cfg::FUZZY_MIN_LEN - 1) {
            for (int i = 0; i < count_; i++) {
                compact(phrases_[i].text, pc, sizeof(pc));
                if ((int)strlen(pc) < cfg::FUZZY_MIN_LEN) continue;
                if (editDistance(cmp, pc, 1) > 1) continue;
                if (fz != Intent::None && fz != phrases_[i].intent)
                    fzAmb = true;
                fz = phrases_[i].intent;
            }
        }
        if (fzAmb) return done(r, Intent::None, MatchKind::Ambiguous);
        if (fz != Intent::None) return done(r, fz, MatchKind::Fuzzy);
        return done(r, Intent::None, MatchKind::None);
    }

  private:
    static Result done(Result& r, Intent i, MatchKind k) {
        r.intent = i;
        r.kind = k;
        return r;
    }

    // Find phrases equal to a run of consecutive whole words joined without
    // spaces. Picks the highest priority intent; a tie between different
    // intents is ambiguous
    void containedSearch(const char* norm, Intent& best, bool& amb) const {
        // Split into words
        const int MAXW = NORM_MAX / 2 + 1;
        int start[MAXW], len[MAXW], nw = 0;
        for (int i = 0; norm[i] && nw < MAXW;) {
            while (norm[i] == ' ') i++;
            if (!norm[i]) break;
            start[nw] = i;
            while (norm[i] && norm[i] != ' ') i++;
            len[nw] = i - start[nw];
            nw++;
        }
        int bestPri = 0;
        char run[NORM_MAX], pc[NORM_MAX];
        for (int a = 0; a < nw; a++) {
            int n = 0;
            for (int b = a; b < nw; b++) {
                if (n + len[b] >= NORM_MAX) break;
                memcpy(run + n, norm + start[b], len[b]);
                n += len[b];
                run[n] = 0;
                for (int i = 0; i < count_; i++) {
                    compact(phrases_[i].text, pc, sizeof(pc));
                    if (strcmp(run, pc) != 0) continue;
                    Intent it = phrases_[i].intent;
                    int pri = intentPriority(it);
                    if (pri > bestPri) {
                        best = it;
                        bestPri = pri;
                        amb = false;
                    } else if (pri == bestPri && it != best) {
                        amb = true;
                    }
                }
            }
        }
    }

    const Phrase* phrases_;
    int count_;
};

}  // namespace chat
