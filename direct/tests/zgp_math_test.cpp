#include "../runtime/zgp_math.hpp"
#include <cassert>
#include <cstdio>

int main() {
    assert(cp2::zgp_sin(0, 1000, 0) == 0);
    assert(cp2::zgp_cos(0, 1000, 0) == 1000);
    // Preserve the DLL's approximate PI and truncation at quarter turns.
    assert(cp2::zgp_sin(64, 1000, 256) == 999);
    assert(cp2::zgp_cos(128, 1000, 256) == -999);
    assert(cp2::zgp_sin(-64, 1000, 256) == -999);
    std::int32_t output[2] = {};
    cp2::zgp_scos(output, 0, 3000, 36000);
    assert(output[0] == 0 && output[1] == 3000);
    cp2::zgp_adif(output, 0, 255, 256);
    assert(output[0] == 1 && output[1] == -1);
    cp2::zgp_adif(output, 255, 0, 256);
    assert(output[0] == 1 && output[1] == 1);
    cp2::zgp_adif(output, -1, 256, 256);
    assert(output[0] == 1 && output[1] == 1);
    cp2::zgp_adif(output, 0, 128, 0);
    assert(output[0] == 128 && output[1] == 1);
    cp2::zgp_adif(output, 128, 0, 256);
    assert(output[0] == 128 && output[1] == -1);
    cp2::zgp_adif(output, -1, 255, 256);
    assert(output[0] == 0 && output[1] == 0);
    cp2::zgp_adif(output, 65534, 0, 65535);
    assert(output[0] == 1 && output[1] == 1);
    bool invalid = false;
    try { cp2::zgp_adif(output, 0, 0, -1); }
    catch (const std::invalid_argument&) { invalid = true; }
    assert(invalid);
    assert(cp2::zgp_len(0, 0, 3, 4) == -5);
    assert(cp2::zgp_len(0, 0, 4, 4) == -5);
    const std::int32_t origin[] = {1, 2, 3};
    assert(cp2::zgp_len3(origin, 3, 5, 9) == -7);
    assert(cp2::zgp_dir(0, 0, 0, -1) == 0);
    assert(cp2::zgp_dir(0, 0, 1, 0) == -64);
    assert(cp2::zgp_dir(0, 0, 0, 1) == -128);
    assert(cp2::zgp_dir(0, 0, -1, 0) == -192);
    assert(cp2::zgp_len(0, 0, 50000, 0) == 0); // wrapped square is negative
    const std::int32_t zero[] = {0, 0, 0};
    assert(cp2::zgp_len3(zero, 2147483647, 2147483647, 2147483647) == 575416511);
    std::puts("ZGP math: 23 boundary and convention checks passed");
}
