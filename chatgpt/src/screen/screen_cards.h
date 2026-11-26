#pragma once
// screen_cards.h — the line-3 status card model (pure C++, host-testable).
//
// The status screen keeps line 1 (network), line 2 (uptime/heap/storage) fixed
// and rotates the cards of line 3, so a 3-line OLED still shows everything.
//
// This header owns *what a card is*; screen/screen_cards_pack.h owns *who
// builds which card*. ScreenManager only rotates the list, so the core file has
// no per-pack #if in the status path and a new module never edits it.
//
// Fixed buffers on purpose: the rotation is walked every 6 s for the whole life
// of the device, so cards use no String and cause no heap churn.
//
// A card carries two forms:
//   text    — full text (21 chars on a 2.42" panel)
//   compact — short text for a narrow panel (10 chars on a 0.96" panel)
// plus `weight` (turns per rotation cycle) and `stale` (the data behind it is
// older than its freshness window; the renderer prefixes '~').

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define SCREEN_CARD_TEXT_MAX    64   // full form, NUL included
#define SCREEN_CARD_COMPACT_MAX 24   // short form, NUL included
#define SCREEN_CARD_MAX         16   // cards in one rotation

struct ScreenCard {
    char    text[SCREEN_CARD_TEXT_MAX];
    char    compact[SCREEN_CARD_COMPACT_MAX];
    uint8_t weight;   // 1..255, turns per cycle
    bool    stale;    // reading older than its freshness window
};

// Copy into a fixed buffer, always NUL-terminated (truncates silently).
inline void screenCardCopy(char* dst, size_t size, const char* src) {
    if (!dst || size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (src[i] && (i + 1) < size) { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
}

class ScreenCardList {
public:
    ScreenCardList() : count_(0) {}

    void clear() { count_ = 0; }
    int  count() const { return count_; }
    bool empty() const { return count_ == 0; }

    // Adds a card and returns its index, or -1 when the list is full or the
    // text is null. `weight` 0 is stored as 1 (a card always gets one turn).
    int add(const char* text, uint8_t weight = 1, bool stale = false) {
        if (!text || count_ >= SCREEN_CARD_MAX) return -1;
        ScreenCard& c = cards_[count_];
        screenCardCopy(c.text, sizeof(c.text), text);
        c.compact[0] = '\0';
        c.weight = weight ? weight : 1;
        c.stale = stale;
        return count_++;
    }

    // Same, with the short form used when the panel is too narrow.
    int addCompact(const char* text, const char* compact,
                   uint8_t weight = 1, bool stale = false) {
        const int idx = add(text, weight, stale);
        if (idx >= 0) screenCardCopy(cards_[idx].compact, sizeof(cards_[idx].compact), compact);
        return idx;
    }

    void setWeight(int idx, uint8_t w) { if (inRange(idx)) cards_[idx].weight = w ? w : 1; }
    void setStale(int idx, bool s) { if (inRange(idx)) cards_[idx].stale = s; }

    const ScreenCard* at(int idx) const { return inRange(idx) ? &cards_[idx] : nullptr; }

    // Weights for screenRotationIndex().
    void weights(uint8_t* out) const {
        if (!out) return;
        for (int i = 0; i < count_; ++i) out[i] = cards_[i].weight ? cards_[i].weight : 1;
    }

    // Text to render for a row `cols` characters wide: the compact form when
    // the full one does not fit but the compact one does, then a leading '~'
    // when the card is stale (old value, not live). Always returns `out`.
    const char* displayText(int idx, int cols, char* out, size_t outSize) const;

private:
    bool inRange(int idx) const { return idx >= 0 && idx < count_; }
    ScreenCard cards_[SCREEN_CARD_MAX];
    int        count_;
};

inline const char* ScreenCardList::displayText(int idx, int cols, char* out, size_t outSize) const {
    if (!out || outSize == 0) return "";
    out[0] = '\0';
    const ScreenCard* c = at(idx);
    if (!c) return out;

    const char* src = c->text;
    if (c->compact[0] && cols > 0) {
        const int fullLen = (int)strlen(c->text);
        const int cmpLen  = (int)strlen(c->compact);
        if (fullLen > cols && cmpLen <= cols) src = c->compact;
    }
    if (c->stale && outSize > 3) {
        out[0] = '~';
        out[1] = ' ';
        screenCardCopy(out + 2, outSize - 2, src);
    } else {
        screenCardCopy(out, outSize, src);
    }
    return out;
}
