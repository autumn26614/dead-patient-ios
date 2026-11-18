#pragma once
// On-screen touch controls: layout, hit testing and the Windows virtual keys
// they hold (direct/runtime/input_state.hpp, source kTouch). Drawing lives in
// the iOS bridge (Classes/iOSBridge.mm); this part is portable and tested on
// the host (direct/tests/touch_controls_test.cpp).
//
// The keys are the ones the original program reads (nyuryoku, recovered.hsp):
// arrows 37-40 through stick, X (OK, button1p_1), C (cancel, button1p_2),
// Z (action, button1p_3), S (menu, 18 + 65, button1p_4) and Esc (run,
// button1p_6). The game logic is unchanged; the controls only press keys.
//
// Left: a joystick. Its knob follows the thumb (clamped to the base) and the
// thumb's direction from the base centre holds one or two arrow keys (eight
// 45 degree sectors, with a dead zone). Right: four small round buttons in a
// diamond, and run in the top right corner.
#include "input_state.hpp"
#include <cmath>
#include <cstdint>
#include <vector>

namespace cp2 {
namespace touch {

enum Icon { kIconNone, kIconOk, kIconCancel, kIconAction, kIconMenu, kIconRun };

struct Button {
    float x = 0, y = 0, radius = 0;  // view pixels, top-left origin
    int key = 0;
    Icon icon = kIconNone;
    bool pressed = false;
};

struct Stick {
    float x = 0, y = 0, radius = 0;  // the base
    float knob_x = 0, knob_y = 0;    // the knob's centre (the base centre at rest)
    float knob_radius = 0;
    int direction = -1;  // -1 none, else 0..7 (right, down-right, down, ..., up-right)
    bool held = false;
};

class Controls {
public:
    static constexpr float kPi = 3.14159265358979f;
    // How far the knob travels from the centre, in base radii.
    static constexpr float kTravel = 0.58f;
    // The dead zone, in base radii.
    static constexpr float kDeadZone = 0.22f;

    // Places the controls for a view of width x height pixels. Insets are the
    // safe-area margins in pixels (left, right, bottom).
    void layout(float width, float height, float left, float right, float bottom) {
        if (width == width_ && height == height_ && left == left_ && right == right_ && bottom == bottom_) return;
        width_ = width;
        height_ = height;
        left_ = left;
        right_ = right;
        bottom_ = bottom;
        const float u = std::fmin(width, height);
        const float margin = 0.045f * u;
        stick_.radius = 0.15f * u;
        stick_.knob_radius = 0.38f * stick_.radius;
        stick_.x = left + margin + stick_.radius;
        stick_.y = height - bottom - margin - stick_.radius;
        stick_.knob_x = stick_.x;
        stick_.knob_y = stick_.y;
        const float r = 0.06f * u;                  // action button radius
        const float spread = 0.105f * u;            // distance from the diamond centre
        const float cx = width - right - margin - r - spread;
        const float cy = height - bottom - margin - r - spread;
        buttons_.assign(5, Button());
        set(buttons_[0], cx + spread, cy, r, 88, kIconOk);       // right: OK (X)
        set(buttons_[1], cx, cy + spread, r, 67, kIconCancel);   // bottom: cancel (C)
        set(buttons_[2], cx - spread, cy, r, 90, kIconAction);   // left: action (Z)
        set(buttons_[3], cx, cy - spread, r, 83, kIconMenu);     // top: menu (S)
        const float small = 0.045f * u;                          // run (Esc), top right
        set(buttons_[4], width - right - margin - small, margin + small, small, 27, kIconRun);
        release_all();
    }

    bool laid_out() const { return width_ > 0; }

    // Hidden while a game controller is connected: nothing is drawn, touches
    // go to the game, and held touch keys are released.
    void set_hidden(bool hidden) {
        if (hidden == hidden_) return;
        hidden_ = hidden;
        release_all();
    }
    bool hidden() const { return hidden_; }
    const Stick& stick() const { return stick_; }
    const std::vector<Button>& buttons() const { return buttons_; }

    // Touch events in view pixels. phase: 0 began, 1 moved, 2 ended or
    // cancelled. Returns true when the touch belongs to the controls (it then
    // is not passed to the game as a mouse touch).
    bool touch(long id, float x, float y, int phase) {
        if (!laid_out() || hidden_) return false;
        Track* t = find(id);
        if (phase == 0) {
            const int target = hit(x, y);
            if (target == kNone) return false;
            if (!t) {
                tracks_.push_back(Track{id, target, x, y});
                t = &tracks_.back();
            }
            t->target = target;
            t->x = x;
            t->y = y;
        } else {
            if (!t) return false;
            t->x = x;
            t->y = y;
            if (phase == 2) {
                for (std::size_t i = 0; i < tracks_.size(); ++i)
                    if (tracks_[i].id == id) {
                        tracks_.erase(tracks_.begin() + static_cast<std::ptrdiff_t>(i));
                        break;
                    }
            }
        }
        update();
        return true;
    }

    // Lifts every touch (the app went to the background, the layout changed).
    void release_all() {
        tracks_.clear();
        update();
    }

private:
    static constexpr int kNone = -2, kStickTarget = -1;
    struct Track {
        long id;
        int target;  // kStickTarget or a button index
        float x, y;
    };

    static void set(Button& b, float x, float y, float r, int key, Icon icon) {
        b.x = x;
        b.y = y;
        b.radius = r;
        b.key = key;
        b.icon = icon;
        b.pressed = false;
    }

    Track* find(long id) {
        for (Track& t : tracks_)
            if (t.id == id) return &t;
        return nullptr;
    }

    // A touch begins on the stick within 1.5 radii, on a button within 1.35
    // (the small buttons keep a comfortable touch target).
    int hit(float x, float y) const {
        const float px = x - stick_.x, py = y - stick_.y;
        if (px * px + py * py <= (1.5f * stick_.radius) * (1.5f * stick_.radius)) return kStickTarget;
        int best = kNone;
        float best_d = 0;
        for (std::size_t i = 0; i < buttons_.size(); ++i) {
            const float dx = x - buttons_[i].x, dy = y - buttons_[i].y;
            const float d = dx * dx + dy * dy, reach = 1.35f * buttons_[i].radius;
            if (d <= reach * reach && (best == kNone || d < best_d)) {
                best = static_cast<int>(i);
                best_d = d;
            }
        }
        return best;
    }

    // Eight directions around the base centre, with a dead zone; a dragged
    // stick touch keeps steering wherever it moves.
    int direction(float x, float y) const {
        const float dx = x - stick_.x, dy = y - stick_.y;
        const float dead = kDeadZone * stick_.radius;
        if (dx * dx + dy * dy < dead * dead) return -1;
        float angle = std::atan2(dy, dx);  // screen y grows downwards
        if (angle < 0) angle += 2 * kPi;
        return static_cast<int>(std::floor(angle / (kPi / 4) + 0.5f)) % 8;
    }

    // Recomputes the pressed state and the touch keys from the active touches.
    // A button stays held while its touch remains within 1.7 radii.
    void update() {
        input::KeyState& keys = input::keys();
        keys.clear(input::kTouch);
        stick_.direction = -1;
        stick_.held = false;
        stick_.knob_x = stick_.x;
        stick_.knob_y = stick_.y;
        for (Button& b : buttons_) b.pressed = false;
        for (const Track& t : tracks_) {
            if (t.target == kStickTarget) {
                stick_.held = true;
                const int d = direction(t.x, t.y);
                if (d >= 0) stick_.direction = d;
                // The knob follows the thumb, up to kTravel radii out.
                float dx = t.x - stick_.x, dy = t.y - stick_.y;
                const float length = std::sqrt(dx * dx + dy * dy), travel = kTravel * stick_.radius;
                if (length > travel) {
                    dx *= travel / length;
                    dy *= travel / length;
                }
                stick_.knob_x = stick_.x + dx;
                stick_.knob_y = stick_.y + dy;
                continue;
            }
            Button& b = buttons_[static_cast<std::size_t>(t.target)];
            const float dx = t.x - b.x, dy = t.y - b.y, reach = 1.7f * b.radius;
            if (dx * dx + dy * dy <= reach * reach) b.pressed = true;
        }
        if (stick_.direction >= 0) {
            // Direction d covers right (0), down (2), left (4), up (6) and the
            // diagonals between them.
            static const int keys_for[8][2] = {{39, 0}, {39, 40}, {40, 0}, {40, 37},
                                               {37, 0}, {37, 38}, {38, 0}, {38, 39}};
            for (int k : keys_for[stick_.direction])
                if (k) keys.set(input::kTouch, k, true);
        }
        for (const Button& b : buttons_)
            if (b.pressed) keys.set(input::kTouch, b.key, true);
    }

    float width_ = 0, height_ = 0, left_ = 0, right_ = 0, bottom_ = 0;
    bool hidden_ = false;
    Stick stick_;
    std::vector<Button> buttons_;
    std::vector<Track> tracks_;
};

inline Controls& controls() {
    static Controls instance;
    return instance;
}

}  // namespace touch
}  // namespace cp2
