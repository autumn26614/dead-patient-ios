#include "win32_compat.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

static unsigned checks = 0;
static void require(bool value) {
    ++checks;
    if (!value) throw std::runtime_error("Win32 compatibility test failed");
}

int main() {
    cp2::Win32Windows windows;
    std::int32_t value = 0;
    cp2::Rect32 rectangle{};
    require(!windows.register_window(0, 640, 480));
    require(windows.register_window(0x1234, 1280, 720, 0x10000000, 0, -12, 30));
    require(!windows.register_window(0x1234, 10, 10));
    require(windows.window_count() == 1);
    require(windows.get_window_long(0x1234, -16, value) && value == 0x10000000);
    require(windows.set_window_long(0x1234, -16, value | 65536 | 262144, value) && value == 0x10000000);
    require(windows.get_window_long(0x1234, -16, value) && value == 0x10050000);
    require(windows.set_window_long(0x1234, -20, -1, value) && value == 0);
    require(windows.get_window_long(0x1234, -20, value) && value == -1);
    require(!windows.set_window_long(0x1234, -4, 1, value)); // never allow native WNDPROC pointers
    require(windows.last_error() == cp2::Win32Windows::invalid_index);
    require(windows.get_window_rect(0x1234, &rectangle) && rectangle.left == -12 &&
            rectangle.top == 30 && rectangle.right == 1268 && rectangle.bottom == 750);
    require(windows.get_client_rect(0x1234, &rectangle) && rectangle.left == 0 &&
            rectangle.top == 0 && rectangle.right == 1280 && rectangle.bottom == 720);
    require(windows.update_surface(0x1234, 1920, 1080));
    require(windows.get_client_rect(0x1234, &rectangle) && rectangle.right == 1920 && rectangle.bottom == 1080);
    require(!windows.update_surface(0x1234, -1, 1080));
    require(windows.last_error() == cp2::Win32Windows::invalid_parameter);
    require(!windows.register_window(1, 100, 100, 0, 0, std::numeric_limits<std::int32_t>::max(), 0));
    require(!windows.get_window_rect(0x1234, nullptr));
    require(!windows.get_window_long(0x9999, -16, value));
    require(windows.last_error() == cp2::Win32Windows::invalid_window_handle);
    require(windows.remove_window(0x1234) && windows.window_count() == 0);
    require(!windows.get_client_rect(0x1234, &rectangle));

    // Mirrors the game's int array: size=52, flags=255, six axes, button mask.
    cp2::JoyInfoEx32 joy{};
    joy.size = 52;
    joy.flags = 255;
    cp2::JoyInfoEx32 before = joy;
    require(cp2::joy_get_pos_ex(0, &joy, sizeof(joy)) == cp2::kJoyUnplugged);
    require(std::memcmp(&joy, &before, sizeof(joy)) == 0);
    require(cp2::joy_get_pos_ex(16, &joy, sizeof(joy)) == cp2::kMmBadDeviceId);
    require(cp2::joy_get_pos_ex(0, &joy, 51) == cp2::kMmInvalidParameter);
    require(cp2::joy_get_pos_ex(0, nullptr, 52) == cp2::kMmInvalidParameter);
    cp2::JoystickSnapshot input;
    input.connected = true;
    input.axes = {{0, 65535, 32768, 15, 29, 511}};
    input.buttons = 0x80000004;
    input.pov = 27000;
    require(cp2::joy_get_pos_ex(0, &joy, sizeof(joy), &input) == cp2::kJoyNoError);
    require(joy.x == 0 && joy.y == 65535 && joy.z == 32768 && joy.r == 15 && joy.u == 29 && joy.v == 511);
    require(joy.buttons == 0x80000004 && joy.button_number == 3 && joy.pov == 27000);
    require(!joy.reserved1 && !joy.reserved2);
    joy.flags = 1;
    require(cp2::joy_get_pos_ex(0, &joy, sizeof(joy), &input) == cp2::kJoyNoError);
    require(joy.x == 0 && joy.y == 0 && joy.buttons == 0 && joy.pov == 0);
    joy.flags = 0xff;
    input.buttons = 0;
    input.pov = 0xffff;
    require(cp2::joy_get_pos_ex(0, &joy, sizeof(joy), &input) == cp2::kJoyNoError && joy.pov == 0xffff && joy.button_number == 0);
    joy.flags = 0x100; // raw calibration is unsupported
    require(cp2::joy_get_pos_ex(0, &joy, sizeof(joy), &input) == cp2::kMmInvalidParameter);
    joy.flags = 0xff;
    joy.size = 51;
    require(cp2::joy_get_pos_ex(0, &joy, sizeof(joy), &input) == cp2::kMmInvalidParameter);
    joy.size = 52;
    input.pov = 4500;
    before = joy;
    require(cp2::joy_get_pos_ex(0, &joy, sizeof(joy), &input) == cp2::kMmInvalidParameter);
    require(std::memcmp(&joy, &before, sizeof(joy)) == 0);

    const std::array<unsigned char, 10> text{{0x42, 0x30, 0x3d, 0xd8, 0x00, 0xde, 0, 0, 99, 99}};
    require(cp2::lstrlen_utf16le(text.data(), text.size()) == 3); // Japanese BMP + surrogate pair
    require(cp2::lstrlen_utf16le(nullptr, 0) == 0);
    bool rejected = false;
    try { cp2::lstrlen_utf16le(text.data(), 7); } catch (const std::out_of_range&) { rejected = true; }
    require(rejected);
    std::array<unsigned char, 11> unaligned{};
    std::memcpy(unaligned.data() + 1, text.data(), text.size());
    require(cp2::lstrlen_utf16le(unaligned.data() + 1, 10) == 3);
    std::cout << checks << " Win32 compatibility checks passed\n";
}
