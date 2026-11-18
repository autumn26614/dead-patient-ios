// Tests ZGP startup state commands against the original rules (docs/zgp-renderer.md).
#include "zgp_state.hpp"
#include <cstdio>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

int main() {
    using namespace cp2::zgp;
    const Camera& camera = state().camera;
    CHECK(camera.fov == 60.0f && camera.near_plane == 1.0f && camera.far_plane == 256.0f && camera.eye[2] == 16.0f);
    CHECK(setaspect(1777) == 0 && camera.aspect == 1.777f);

    CHECK(setmaxsurf(16, 9999) == 0 && state().surfaces.count == 512);
    CHECK(setmaxsurf(16, 4) == 0 && state().surfaces.count == 512);  // allocated once
    CHECK(setmaxunit(4096, 4096, 0, -5) == 0);
    CHECK(state().units[0].count == 2048 && state().units[1].count == 4096 && state().units[2].count == 1 &&
          state().units[3].count == 1);
    CHECK(setmaxunit(256, 256, 128, 32) == 0 && state().units[0].count == 2048);  // createdevm's defaults are ignored

    CHECK(setbmscr(0x80000000u) == 0 && state().bmscr_set && state().bmscr_window == 0x80000000u);
    CHECK(setmaxangle(65535, 0) == 0 && state().angle_period_a == 65535.0f && state().angle_period_b == 256.0f);

    if (failures) return 1;
    std::printf("ZGP startup state: PASS\n");
    return 0;
}
