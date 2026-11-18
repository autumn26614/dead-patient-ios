#include "legacy_memory_bridge.hpp"
#include "win32_compat.hpp"
#include "hspvar_core.h"
#include "hsp3debug.h"
#include "strbuf.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

extern void HspVarInt_Init(HspVarProc*);
extern void HspVarDouble_Init(HspVarProc*);
// This fixture initializes only numeric types, leaving unrelated HSP extension
// type registrations out of the host test binary.
void HspVarStr_Init(HspVarProc*) { throw std::runtime_error("Unexpected string registration"); }
void HspVarStruct_Init(HspVarProc*) { throw std::runtime_error("Unexpected struct registration"); }
extern "C" void HspVarLabel_Init(HspVarProc*) { throw std::runtime_error("Unexpected label registration"); }
int htoi(char* value) { return static_cast<int>(std::strtol(value + (*value == '$'), nullptr, 16)); }

static unsigned checks;
static void require(bool value) {
    ++checks;
    if (!value) throw std::runtime_error("HSP legacy memory integration test failed");
}
template<class Function> static void rejects_pointer(Function function) {
    bool rejected = false;
    try { function(); } catch (const std::out_of_range&) { rejected = true; }
    require(rejected);
}
template<class Function> static void rejects_hsp(Function function) {
    bool rejected = false;
    try { function(); } catch (HSPERROR error) { rejected = error == HSPERR_ILLEGAL_FUNCTION; }
    require(rejected);
}

int main() {
    sbInit();
    HspVarProc types[HSPVAR_FLAG_MAX]{};
    HspVarInt_Init(&types[HSPVAR_FLAG_INT]);
    HspVarDouble_Init(&types[HSPVAR_FLAG_DOUBLE]);
    hspvarproc = types;
    PVal owner{};
    owner.mode = HSPVAR_MODE_NONE;
    owner.flag = HSPVAR_FLAG_INT;
    HspVarCoreDim(&owner, HSPVAR_FLAG_INT, 8, 0, 0, 0);
    auto* integers = reinterpret_cast<int*>(owner.pt);
    integers[3] = 71;
    auto interior = cp2::legacy_register(owner.pt, owner.size, integers + 3);
    auto base = cp2::legacy_register(owner.pt, owner.size, owner.pt);
    require(interior == base + 12);
    require(cp2::legacy_resolve(interior, 4) == integers + 3);
    rejects_pointer([&] { cp2::legacy_resolve(base + owner.size - 1, 2); });
    PVal clone{};
    clone.mode = HSPVAR_MODE_NONE;
    clone.flag = HSPVAR_FLAG_INT;
    HspVarCoreDupPtr(&clone, HSPVAR_FLAG_INT, cp2::legacy_resolve(interior, 8), 8);
    cp2::legacy_bind_clone(&clone, interior, 8);
    auto* shared = reinterpret_cast<int*>(types[HSPVAR_FLAG_INT].GetPtr(&clone));
    require(*shared == 71);
    *shared = 90;
    require(integers[3] == 90);
    clone.offset = 2;
    rejects_hsp([&] { types[HSPVAR_FLAG_INT].GetPtr(&clone); });
    clone.offset = 0;
    auto* previous = owner.pt;
    HspVarCoreReDim(&owner, 1, 16); // same pooled slot, different variable extent
    require(owner.pt == previous);
    rejects_pointer([&] { cp2::legacy_resolve(base, 4); });
    rejects_hsp([&] { types[HSPVAR_FLAG_INT].GetPtr(&clone); });
    auto resized = cp2::legacy_register(owner.pt, owner.size, owner.pt);
    require(resized != base && cp2::legacy_resolve(resized, owner.size) == owner.pt);
    types[HSPVAR_FLAG_INT].Free(&clone);
    require(!cp2::legacy_has_clone(&clone));
    HspVarCoreReDim(&owner, 1, 128); // internal -> external backing storage
    rejects_pointer([&] { cp2::legacy_resolve(resized, 4); });
    auto expanded = cp2::legacy_register(owner.pt, owner.size, owner.pt);
    types[HSPVAR_FLAG_INT].Free(&owner);
    rejects_pointer([&] { cp2::legacy_resolve(expanded, 4); });

    // Exercise the allocator directly, as used by separately allocated HSP
    // string elements. Reallocation and pooled-slot reuse must both expire IDs.
    char* text = sbAlloc(8);
    auto text_address = cp2::legacy_register(text, sbGetSTRINF(text)->size, text);
    text = sbExpand(text, 1024);
    rejects_pointer([&] { cp2::legacy_resolve(text_address, 1); });
    text_address = cp2::legacy_register(text, sbGetSTRINF(text)->size, text);
    sbFree(text);
    rejects_pointer([&] { cp2::legacy_resolve(text_address, 1); });
    char* pooled = sbAlloc(8);
    auto pooled_address = cp2::legacy_register(pooled, 8, pooled);
    sbFree(pooled);
    char* replacement = sbAlloc(8);
    require(replacement == pooled);
    rejects_pointer([&] { cp2::legacy_resolve(pooled_address, 1); });
    auto replacement_address = cp2::legacy_register(replacement, 8, replacement);
    require(replacement_address != pooled_address);

    PVal double_owner{};
    double_owner.mode = HSPVAR_MODE_NONE;
    double_owner.flag = HSPVAR_FLAG_DOUBLE;
    HspVarCoreDim(&double_owner, HSPVAR_FLAG_DOUBLE, 2, 0, 0, 0);
    auto double_address = cp2::legacy_register(double_owner.pt, double_owner.size, double_owner.pt);
    PVal double_clone{};
    double_clone.mode = HSPVAR_MODE_NONE;
    double_clone.flag = HSPVAR_FLAG_INT;
    HspVarCoreDupPtr(&double_clone, HSPVAR_FLAG_DOUBLE, double_owner.pt, double_owner.size);
    cp2::legacy_bind_clone(&double_clone, double_address, double_owner.size);
    auto* number = reinterpret_cast<double*>(types[HSPVAR_FLAG_DOUBLE].GetPtr(&double_clone));
    *number = 2.75;
    require(*reinterpret_cast<double*>(double_owner.pt) == 2.75);
    types[HSPVAR_FLAG_DOUBLE].Free(&double_owner);
    rejects_hsp([&] { types[HSPVAR_FLAG_DOUBLE].GetPtr(&double_clone); });
    types[HSPVAR_FLAG_DOUBLE].Free(&double_clone);
    require(!cp2::legacy_has_clone(&double_clone));

    // The actual GetClientRect/GetWindowRect bridge pattern: Windows writes a
    // 16-byte RECT through an integer returned by HSP's virtualized varptr.
    PVal rect_buffer{};
    rect_buffer.mode = HSPVAR_MODE_NONE;
    rect_buffer.flag = HSPVAR_FLAG_INT;
    HspVarCoreDim(&rect_buffer, HSPVAR_FLAG_INT, 4, 0, 0, 0);
    auto rect_address = cp2::legacy_register(rect_buffer.pt, rect_buffer.size, rect_buffer.pt);
    cp2::Win32Windows windows;
    require(windows.register_window(0x4444, 1920, 1080, 0, 0, 12, 34));
    cp2::Rect32 rect;
    require(windows.get_client_rect(0x4444, &rect));
    std::memcpy(cp2::legacy_resolve(rect_address, sizeof(rect)), &rect, sizeof(rect));
    auto* coordinates = reinterpret_cast<int*>(rect_buffer.pt);
    require(coordinates[0] == 0 && coordinates[1] == 0 && coordinates[2] == 1920 && coordinates[3] == 1080);
    require(windows.get_window_rect(0x4444, &rect));
    std::memcpy(cp2::legacy_resolve(rect_address, sizeof(rect)), &rect, sizeof(rect));
    require(coordinates[0] == 12 && coordinates[1] == 34 && coordinates[2] == 1932 && coordinates[3] == 1114);
    types[HSPVAR_FLAG_INT].Free(&rect_buffer);
    rejects_pointer([&] { cp2::legacy_resolve(rect_address, sizeof(rect)); });
    sbBye();
    rejects_pointer([&] { cp2::legacy_resolve(replacement_address, 1); });
    require(cp2::legacy_memory().region_count() == 0 && cp2::legacy_clones().empty());
    std::cout << checks << " HSP allocator and legacy pointer checks passed\n";
}
