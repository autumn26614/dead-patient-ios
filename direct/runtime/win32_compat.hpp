#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>

namespace cp2 {

// These are Windows wire layouts, not native UIKit/window object layouts.
struct Rect32 {
    std::int32_t left, top, right, bottom;
};
static_assert(sizeof(Rect32) == 16, "Win32 RECT must remain 16 bytes");

// Only windows backed by an actual HSP surface may be registered. The caller
// owns their handles and must remove them when the associated surface closes.
// On iOS the application surface has no desktop window decoration; style bits
// are retained as legacy window metadata, without inventing desktop controls.
class Win32Windows {
public:
    using Handle = std::uint32_t;
    enum : std::uint32_t {
        success = 0, invalid_parameter = 87,
        invalid_window_handle = 1400, invalid_index = 1413
    };

    bool register_window(Handle handle, std::int32_t width, std::int32_t height,
                         std::int32_t style = 0, std::int32_t extended_style = 0,
                         std::int32_t x = 0, std::int32_t y = 0) {
        if (!handle || windows_.count(handle) || !valid_bounds(x, y, width, height))
            return fail(invalid_parameter);
        windows_.emplace(handle, Window{x, y, width, height, style, extended_style});
        return done();
    }

    bool update_surface(Handle handle, std::int32_t width, std::int32_t height) {
        auto found = windows_.find(handle);
        if (found == windows_.end()) return fail(invalid_window_handle);
        Window& window = found->second;
        if (!valid_bounds(window.x, window.y, width, height)) return fail(invalid_parameter);
        window.width = width;
        window.height = height;
        return done();
    }

    bool remove_window(Handle handle) {
        if (!windows_.erase(handle)) return fail(invalid_window_handle);
        return done();
    }

    bool get_window_long(Handle handle, std::int32_t index, std::int32_t& result) {
        auto found = windows_.find(handle);
        if (found == windows_.end()) return fail(invalid_window_handle);
        if (index == -16) result = found->second.style; // GWL_STYLE
        else if (index == -20) result = found->second.extended_style; // GWL_EXSTYLE
        else return fail(invalid_index);
        return done();
    }

    bool set_window_long(Handle handle, std::int32_t index, std::int32_t value,
                         std::int32_t& previous) {
        auto found = windows_.find(handle);
        if (found == windows_.end()) return fail(invalid_window_handle);
        std::int32_t* field;
        if (index == -16) field = &found->second.style;
        else if (index == -20) field = &found->second.extended_style;
        else return fail(invalid_index);
        previous = *field;
        *field = value;
        return done();
    }

    bool get_window_rect(Handle handle, Rect32* rectangle) {
        auto found = windows_.find(handle);
        if (found == windows_.end()) return fail(invalid_window_handle);
        if (!rectangle) return fail(invalid_parameter);
        const Window& window = found->second;
        *rectangle = Rect32{window.x, window.y, window.x + window.width,
                           window.y + window.height};
        return done();
    }

    bool get_client_rect(Handle handle, Rect32* rectangle) {
        auto found = windows_.find(handle);
        if (found == windows_.end()) return fail(invalid_window_handle);
        if (!rectangle) return fail(invalid_parameter);
        *rectangle = Rect32{0, 0, found->second.width, found->second.height};
        return done();
    }

    std::uint32_t last_error() const { return error_; }
    std::size_t window_count() const { return windows_.size(); }

private:
    struct Window {
        std::int32_t x, y, width, height, style, extended_style;
    };
    static bool valid_bounds(std::int32_t x, std::int32_t y,
                             std::int32_t width, std::int32_t height) {
        return width >= 0 && height >= 0 &&
            static_cast<std::int64_t>(x) + width <= std::numeric_limits<std::int32_t>::max() &&
            static_cast<std::int64_t>(y) + height <= std::numeric_limits<std::int32_t>::max();
    }
    bool fail(std::uint32_t error) { error_ = error; return false; }
    bool done() { error_ = success; return true; }
    std::map<Handle, Window> windows_;
    std::uint32_t error_ = success;
};

struct JoyInfoEx32 {
    std::uint32_t size, flags;
    std::uint32_t x, y, z, r, u, v;
    std::uint32_t buttons, button_number, pov, reserved1, reserved2;
};
static_assert(sizeof(JoyInfoEx32) == 52, "Win32 JOYINFOEX must remain 52 bytes");

// Platform input code can supply a calibrated snapshot. With no attached input
// device the original program receives JOYERR_UNPLUGGED and follows its normal
// keyboard path. A missing controller must never be reported as a live device.
struct JoystickSnapshot {
    bool connected = false;
    std::array<std::uint16_t, 6> axes{{32768, 32768, 32768, 32768, 32768, 32768}};
    std::uint32_t buttons = 0;
    std::uint32_t pov = 0xffff; // JOY_POVCENTERED (16-bit value in DWORD)
};

enum : std::uint32_t {
    kJoyNoError = 0, kMmBadDeviceId = 2, kMmInvalidParameter = 11,
    kJoyUnplugged = 167, kJoyReturnAll = 0xff
};

// Implements the calibrated JOY_RETURNALL subset requested by this program.
// Calibration/raw-data modes are deliberately rejected rather than guessed.
inline std::uint32_t joy_get_pos_ex(std::uint32_t device_id, void* output,
                                    std::size_t bytes,
                                    const JoystickSnapshot* snapshot = nullptr) {
    if (device_id >= 16) return kMmBadDeviceId;
    if (!output || bytes < sizeof(JoyInfoEx32)) return kMmInvalidParameter;
    JoyInfoEx32 request;
    std::memcpy(&request, output, sizeof(request));
    if (request.size != sizeof(JoyInfoEx32) || (request.flags & ~kJoyReturnAll))
        return kMmInvalidParameter;
    if (!snapshot || !snapshot->connected) return kJoyUnplugged;
    // No partial writes on failure, and no stale values in unrequested fields.
    JoyInfoEx32 result{};
    result.size = request.size;
    result.flags = request.flags;
    std::uint32_t* axes[] = {&result.x, &result.y, &result.z, &result.r, &result.u, &result.v};
    for (std::size_t axis = 0; axis < 6; ++axis)
        if (request.flags & (1u << axis)) *axes[axis] = snapshot->axes[axis];
    if (request.flags & 0x40) {
        // JOY_RETURNPOV accepts only cardinal positions. Continuous POV mode
        // needs a separately implemented JOY_RETURNPOVCTS contract.
        if (snapshot->pov != 0xffff && snapshot->pov != 0 && snapshot->pov != 9000 &&
            snapshot->pov != 18000 && snapshot->pov != 27000) return kMmInvalidParameter;
        result.pov = snapshot->pov;
    }
    if (request.flags & 0x80) {
        result.buttons = snapshot->buttons;
        for (std::uint32_t number = 1; number <= 32; ++number)
            if (snapshot->buttons & (std::uint32_t(1) << (number - 1))) {
                result.button_number = number;
                break;
            }
    }
    std::memcpy(output, &result, sizeof(result));
    return kJoyNoError;
}

// Windows WCHAR is always 16 bits, unlike wchar_t on Apple platforms. This
// operates on the already-converted UTF-16LE argument, not original CP932 bytes.
// The bridge must preserve HSP's wptr conversion before invoking this helper.
inline std::int32_t lstrlen_utf16le(const void* string, std::size_t bytes) {
    if (!string) return 0;
    const auto* data = static_cast<const std::uint8_t*>(string);
    for (std::size_t index = 0; index < bytes / 2; ++index) {
        if (!data[index * 2] && !data[index * 2 + 1]) {
            if (index > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
                throw std::overflow_error("UTF-16 length exceeds Win32 integer range");
            return static_cast<std::int32_t>(index);
        }
    }
    throw std::out_of_range("Unterminated UTF-16 argument");
}

} // namespace cp2
