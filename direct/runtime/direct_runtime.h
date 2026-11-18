#pragma once

#include <cstdint>
#include "hsp3r.h"

// HSP's embedded runtime represents labels as task IDs encoded in pointer slots.
// Register the same IRQ option and label as the original onexit/onerror/onkey call.
inline void DirectRegisterIrq(int irq, int option, int task_id) {
    code_setirq(irq, option, -1,
        reinterpret_cast<unsigned short *>(static_cast<std::uintptr_t>(task_id)));
}

// The on-disk/x86 function table overlays proc and funcflag. The arm64 runtime
// stores them separately, so a pointer-cast initializer would lose function flags.
inline STRUCTDAT DirectFunctionInfo(short index, short subid, int parameter_index,
        int parameters, int name_index, int size, int task_index, int flags) {
    STRUCTDAT result{};
    result.index = index;
    result.subid = subid;
    result.prmindex = parameter_index;
    result.prmmax = parameters;
    result.nameidx = name_index;
    result.size = size;
    result.otindex = task_index;
#ifdef PTR64BIT
    result.proc = nullptr;
#endif
    result.funcflag = flags;
    return result;
}
