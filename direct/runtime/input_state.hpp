#pragma once
// Platform input for the original program's keyboard reads. The game reads
// Windows virtual-key codes with getkey (Enter 13, Space 32, arrows 37-40,
// Z/X/C/A/S, Esc 27, Shift 16, Ctrl 17, ...) and the arrows/Space/Enter/Ctrl/
// Esc through HSP's stick. On iOS those come from on-screen touch controls,
// a connected game controller (GameController framework, iOSBridge.mm) and,
// for unattended simulator runs, a scripted test input; this table
// holds the resulting key state that getkey and stick read.
//
// Sources are kept apart so that releasing a touch control does not release
// the same key held by another source.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace cp2 {
namespace input {

enum Source { kTouch = 0, kScript = 1, kController = 2, kSources = 3 };

class KeyState {
public:
    void set(Source source, int key, bool down) {
        if (key < 0 || key > 255) return;
        down_[source][key] = down;
    }
    void clear(Source source) { std::memset(down_[source], 0, sizeof(down_[source])); }
    bool down_in(Source source, int key) const { return key >= 0 && key <= 255 && down_[source][key]; }
    bool down(int key) const {
        if (key < 0 || key > 255) return false;
        for (int s = 0; s < kSources; ++s)
            if (down_[s][key]) return true;
        return false;
    }

    // HSP's stick bits (hgio_stick's Windows layout): left 1, up 2, right 4,
    // down 8, Space 16, Enter 32, Ctrl 64, Esc 128, Tab 1024. The mouse bit
    // (256) stays with the platform's touch state.
    int stick_bits() const {
        static const int keys[][2] = {{37, 1}, {38, 2}, {39, 4}, {40, 8}, {32, 16}, {13, 32}, {17, 64},
                                      {27, 128}, {9, 1024}};
        int bits = 0;
        for (const auto& k : keys)
            if (down(k[0])) bits |= k[1];
        return bits;
    }

private:
    bool down_[kSources][256] = {};
};

// Scripted test input: "KEY@FROM-TO,KEY@FROM-TO,..." holds virtual key KEY
// down from presented frame FROM up to (not including) frame TO. Used only by
// unattended simulator runs (CP2_INPUT_SCRIPT); never by the game itself.
class InputScript {
public:
    struct Press {
        int key, from, to;
    };
    bool parse(const char* text, std::string& error) {
        presses_.clear();
        if (!text) return true;
        const std::string s(text);
        std::size_t at = 0;
        while (at < s.size()) {
            const std::size_t end = s.find(',', at);
            const std::string item = s.substr(at, end == std::string::npos ? std::string::npos : end - at);
            at = end == std::string::npos ? s.size() : end + 1;
            if (item.empty()) continue;
            Press p{};
            char tail = 0;
            if (std::sscanf(item.c_str(), "%d@%d-%d%c", &p.key, &p.from, &p.to, &tail) != 3 || p.key < 0 ||
                p.key > 255 || p.from < 0 || p.to <= p.from) {
                error = "bad input script item '" + item + "'";
                presses_.clear();
                return false;
            }
            presses_.push_back(p);
        }
        return true;
    }
    // Applies the script for presented frame `frame` to the key state.
    void apply(KeyState& keys, int frame) const {
        keys.clear(kScript);
        for (const Press& p : presses_)
            if (frame >= p.from && frame < p.to) keys.set(kScript, p.key, true);
    }
    bool empty() const { return presses_.empty(); }

private:
    std::vector<Press> presses_;
};

inline KeyState& keys() {
    static KeyState instance;
    return instance;
}

}  // namespace input
}  // namespace cp2
