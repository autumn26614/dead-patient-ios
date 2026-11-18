// Build as i686 on Windows, then pass the original zgp1807.hpi path.
// Executes only isolated math exports; never initializes graphics or the game.
#include "../runtime/zgp_math.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

using MathFunction = int (__stdcall*)(std::int32_t*, int, int, int);
using ScalarFunction = int (__stdcall*)(int, int, int, int);

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const HMODULE module = LoadLibraryA(argv[1]);
    if (!module) { std::printf("LoadLibrary failed: %lu\n", GetLastError()); return 2; }
    const auto sine = reinterpret_cast<MathFunction>(GetProcAddress(module, "_z_sin@16"));
    const auto cosine = reinterpret_cast<MathFunction>(GetProcAddress(module, "_z_cos@16"));
    const auto scos = reinterpret_cast<MathFunction>(GetProcAddress(module, "_z_scos@16"));
    const auto adif = reinterpret_cast<MathFunction>(GetProcAddress(module, "_z_adif@16"));
    const auto length = reinterpret_cast<ScalarFunction>(GetProcAddress(module, "_z_len@16"));
    const auto length3 = reinterpret_cast<MathFunction>(GetProcAddress(module, "_z_len3@16"));
    const auto direction = reinterpret_cast<ScalarFunction>(GetProcAddress(module, "_z_dir@16"));
    const auto angle_period = reinterpret_cast<ScalarFunction>(GetProcAddress(module, "_setmaxangle@16"));
    if (!sine || !cosine || !scos || !adif || !length || !length3 || !direction || !angle_period) return 2;
    unsigned checks = 0;
    unsigned mismatches = 0;
    auto compare = [&](int expected, int actual) {
        ++checks;
        if (expected != actual) {
            if (mismatches < 10) std::printf("Expected %d, got %d\n", expected, actual);
            ++mismatches;
        }
    };
    for (int turn : {0, 160, 256, 36000, 65535}) {
        for (int angle = -131070; angle <= 131070; angle += 37) {
            for (int amplitude : {0, 30, 1000, 65535, -3000}) {
                std::int32_t original[2] = {};
                compare(0, sine(original, angle, amplitude, turn));
                compare(original[0], cp2::zgp_sin(angle, amplitude, turn));
                compare(0, cosine(original, angle, amplitude, turn));
                compare(original[0], cp2::zgp_cos(angle, amplitude, turn));
                compare(0, scos(original, angle, amplitude, turn));
                std::int32_t actual[2];
                cp2::zgp_scos(actual, angle, amplitude, turn);
                compare(original[0], actual[0]);
                compare(original[1], actual[1]);
            }
            for (int second : {-65535, -1, 0, 128, 32767, 65535}) {
                std::int32_t original[2] = {}, actual[2] = {};
                compare(0, adif(original, angle, second, turn));
                cp2::zgp_adif(actual, angle, second, turn);
                compare(original[0], actual[0]);
                compare(original[1], actual[1]);
            }
        }
    }
    for (int period : {256, 160, 36000, 65535}) {
        compare(0, angle_period(period, period, 0, 0));
        for (int x = -1000; x <= 1000; x += 13) {
            for (int y = -1000; y <= 1000; y += 17) {
                const std::int32_t origin[] = {x, y, x + y};
                compare(length(x, y, -17, 23), cp2::zgp_len(x, y, -17, 23));
                compare(length3(const_cast<std::int32_t*>(origin), -17, 23, 99), cp2::zgp_len3(origin, -17, 23, 99));
                compare(direction(x, y, -17, 23), cp2::zgp_dir(x, y, -17, 23, static_cast<float>(period)));
            }
        }
    }
    for (int x : {-2147483647 - 1, -65535, -50000, -46341, 0, 46341, 50000, 65535, 2147483647}) {
        for (int y : {-2147483647 - 1, -50000, 0, 50000, 2147483647}) {
            std::int32_t origin[] = {0, 0, 0};
            compare(length(x, y, 0, 0), cp2::zgp_len(x, y, 0, 0));
            compare(length3(origin, x, y, x), cp2::zgp_len3(origin, x, y, x));
        }
    }
    FreeLibrary(module);
    std::printf("ZGP original x86 reference: %u comparisons, %u mismatches\n", checks, mismatches);
    return mismatches ? 1 : 0;
}
