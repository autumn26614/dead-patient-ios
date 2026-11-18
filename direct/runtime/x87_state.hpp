// The x87 precision the original game would be running with. Direct3D 8's
// CreateDevice without D3DCREATE_FPU_PRESERVE (ZGP's createdevm) puts the
// thread's x87 unit in single precision; from then on the game runtime's
// x87 double arithmetic rounds every step to 24 bits. Code that transcribes
// such arithmetic (hsp_polygon.hpp) reads this flag; ZGP's createdevm sets it.
#pragma once

namespace cp2 {

inline bool& x87_single_precision() {
    static bool single = false;
    return single;
}

}  // namespace cp2
