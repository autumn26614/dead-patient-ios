#pragma once
// Native state for the original ZGP 1.807 renderer (plugin/zgp1807.hpi).
//
// This header holds the parts of ZGP's global state that its startup commands
// change before a rendering device exists, transcribed from the original x86
// code (see docs/zgp-renderer.md). Device creation and drawing are separate.
#include <cstdint>
#include <cstring>

namespace cp2 {
namespace zgp {

// Camera object at 0x1005fee0, constructed by 0x10007e00.
struct Camera {
    float up[3] = {0.0f, 1.0f, 0.0f};    // +0x00
    float eye[3] = {0.0f, 0.0f, 16.0f};  // +0x0c
    float direction[3] = {0, 0, 0};      // +0x18 (target = eye + direction)
    float angles[3] = {0, 0, 0};         // +0x24 pitch, +0x28 yaw, +0x2c roll (degrees)
    float aspect = 1.0f;                 // +0x30
    float fov = 60.0f;                   // +0x34, degrees
    float near_plane = 1.0f;             // +0x38
    float far_plane = 256.0f;            // +0x3c
    std::int32_t field40 = 0;            // +0x40
    std::int32_t field44 = 0;            // +0x44
    std::int32_t field48 = -1;           // +0x48
    std::int32_t light = 0;              // +0x6cc, selected light index
};

// Allocation-once tables sized by setmaxsurf/setmaxunit (0x100198e0/0x100199c0).
struct Table {
    bool allocated = false;
    std::int32_t count = 0;
    std::uint32_t entry_bytes = 0;
};

struct State {
    Camera camera;
    // setmaxsurf: surface table, 0x21c bytes each, 1..512 (0x100605d0/0x100605b0).
    Table surfaces{false, 0, 0x21c};
    // setmaxunit: four unit tables (0x100605dc, 0x100605e0, 0x100605d4, 0x100605d8).
    Table units[4] = {{false, 0, 0x30}, {false, 0, 0x98}, {false, 0, 0x12c}, {false, 0, 0x40}};
    // setbmscr: automatic BMSCR argument kept for createdevm (0x100605cc).
    std::uint32_t bmscr_window = 0;
    bool bmscr_set = false;
    // setmaxangle / setstdscale globals (0x1005bd1c, 0x1005bd24, 0x1005bd20, 0x1005bd28).
    float angle_period_a = 256.0f;
    float angle_period_b = 256.0f;
    float standard_scale_a = 256.0f;
    float standard_scale_b = 256.0f;
};

inline State& state() {
    static State instance;
    return instance;
}

// _setaspect@16 (0x1001b050): camera aspect = value / 1000. ZGP runs this
// before createdevm, while the x87 unit is still in its default precision.
inline int setaspect(int value) {
    state().camera.aspect = static_cast<float>(static_cast<double>(value) / 1000.0);
    return 0;
}

inline std::int32_t clamp(std::int32_t value, std::int32_t low, std::int32_t high) {
    return value < low ? low : value > high ? high : value;
}

// _setmaxsurf@16 (0x100198e0): only the second argument is used; the table is
// allocated once and later calls are ignored.
inline int setmaxsurf(int, int count) {
    Table& t = state().surfaces;
    if (!t.allocated) {
        t.count = clamp(count, 1, 512);
        t.allocated = true;
    }
    return 0;
}

// _setmaxunit@16 (0x100199c0): each table is allocated once.
inline int setmaxunit(int a, int b, int c, int d) {
    const int requested[4] = {a, b, c, d};
    const int limits[4] = {0x800, 0x1000, 0x1000, 0x1000};
    for (int i = 0; i < 4; ++i) {
        Table& t = state().units[i];
        if (t.allocated) continue;
        t.count = clamp(requested[i], 1, limits[i]);
        t.allocated = true;
    }
    return 0;
}

// _setbmscr@16 (0x10019d30): stores the automatic BMSCR pointer; createdevm
// later reads its HWND (byte offset 0x34).
inline int setbmscr(std::uint32_t window) {
    state().bmscr_window = window;
    state().bmscr_set = true;
    return 0;
}

// _setmaxangle@16 (0x10019cd0): non-zero arguments replace the two periods.
inline int setmaxangle(int a, int b) {
    if (a) state().angle_period_a = static_cast<float>(a);
    if (b) state().angle_period_b = static_cast<float>(b);
    return 0;
}

}  // namespace zgp
}  // namespace cp2
