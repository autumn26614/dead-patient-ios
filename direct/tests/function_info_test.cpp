#include "direct_runtime.h"
#include <cassert>
#include <iostream>

int main() {
    auto function = DirectFunctionInfo(-1, 2, 12, 3, 48, 24, 7, 0x123);
    assert(function.index == -1 && function.subid == 2);
    assert(function.prmindex == 12 && function.prmmax == 3);
    assert(function.nameidx == 48 && function.size == 24 && function.otindex == 7);
    assert(function.funcflag == 0x123);
#ifdef PTR64BIT
    assert(function.proc == nullptr);
#endif
    STRUCTDAT functions[2] = {function, function};
    assert(sizeof(functions) / sizeof(STRUCTDAT) == 2);
    std::cout << "Native function table ABI and flags checks passed\n";
}
