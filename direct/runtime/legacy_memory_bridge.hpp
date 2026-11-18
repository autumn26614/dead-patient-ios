#pragma once

#include "legacy_memory.hpp"
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>

namespace cp2 {

// One address space for all runtime translation units, including HSP's shared
// allocator. Function-local storage in this inline function is shared by the
// C++ ODR, rather than creating a separate address map per source file.
inline LegacyAddressSpace& legacy_memory() {
    // The runtime may free HSP buffers in static-object destructors. Keep this
    // process-lifetime map alive through that teardown regardless of TU order.
    static LegacyAddressSpace* memory = new LegacyAddressSpace;
    return *memory;
}

inline std::uint32_t legacy_register(void* allocation, std::size_t size, void* pointer) {
    auto base = reinterpret_cast<std::uintptr_t>(allocation);
    auto current = reinterpret_cast<std::uintptr_t>(pointer);
    if (current < base || current - base >= size)
        throw std::out_of_range("Legacy variable pointer is outside its storage");
    auto address = legacy_memory().register_region(allocation, size);
    return address + static_cast<std::uint32_t>(current - base);
}

inline void* legacy_resolve(std::uint32_t address, std::size_t bytes) {
    return legacy_memory().resolve(address, bytes);
}

inline void legacy_invalidate(void* allocation) {
    legacy_memory().invalidate(allocation);
}

struct LegacyClone {
    std::uint32_t address;
    std::size_t size;
};

inline std::map<const void*, LegacyClone>& legacy_clones() {
    static auto* clones = new std::map<const void*, LegacyClone>;
    return *clones;
}

inline bool legacy_has_clone(const void* variable) {
    return legacy_clones().count(variable) != 0;
}

inline void legacy_forget_clone(const void* variable) {
    legacy_clones().erase(variable);
}

inline void legacy_bind_clone(const void* variable, std::uint32_t address, std::size_t size) {
    if (!variable || !size) throw std::invalid_argument("Empty legacy clone");
    legacy_resolve(address, size);
    legacy_clones()[variable] = LegacyClone{address, size};
}

inline void* legacy_clone_pointer(const void* variable, std::size_t offset, std::size_t bytes) {
    auto found = legacy_clones().find(variable);
    if (found == legacy_clones().end()) throw std::out_of_range("Unknown legacy clone");
    const LegacyClone& clone = found->second;
    if (offset > clone.size || bytes > clone.size - offset)
        throw std::out_of_range("Legacy clone access exceeds its declared size");
    return legacy_resolve(clone.address + static_cast<std::uint32_t>(offset), bytes);
}

} // namespace cp2
