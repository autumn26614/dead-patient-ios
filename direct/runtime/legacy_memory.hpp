#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>

namespace cp2 {

// HSP's 32-bit integers and Win32 structures cannot hold arm64 pointers. Keep
// byte-addressable virtual ranges so legacy pointer arithmetic remains meaningful.
// The owner must invalidate its region before resizing or freeing native storage.
class LegacyAddressSpace {
public:
    using Address = std::uint32_t;

    Address register_region(void* pointer, std::size_t size) {
        if (!pointer || !size) throw std::invalid_argument("Empty legacy memory region");
        auto native = reinterpret_cast<std::uintptr_t>(pointer);
        if (size > std::numeric_limits<std::uintptr_t>::max() - native)
            throw std::overflow_error("Native memory range overflow");
        for (const auto& entry : regions_) {
            const Region& region = entry.second;
            if (native >= region.native && native - region.native <= region.size &&
                size <= region.size - (native - region.native)) {
                return entry.first + static_cast<Address>(native - region.native);
            }
            if (native < region.native + region.size && native + size > region.native)
                throw std::invalid_argument("Overlapping legacy memory ownership");
        }
        // Upper positive 32-bit values are reserved for native module handles.
        constexpr std::uint64_t limit = 0x6fffffff;
        std::uint64_t reserved = (static_cast<std::uint64_t>(size) + 4095) & ~std::uint64_t(4095);
        if (reserved > limit || next_ > limit - reserved)
            throw std::overflow_error("Legacy virtual address space exhausted");
        Address result = static_cast<Address>(next_);
        regions_.emplace(result, Region{native, size});
        next_ += reserved;
        return result;
    }

    void* resolve(Address address, std::size_t bytes) const {
        if (!address && !bytes) return nullptr;
        auto entry = regions_.upper_bound(address);
        if (entry == regions_.begin()) throw std::out_of_range("Unmapped legacy pointer");
        --entry;
        const Region& region = entry->second;
        std::size_t offset = address - entry->first;
        if (offset > region.size || bytes > region.size - offset)
            throw std::out_of_range("Legacy memory access exceeds its allocation");
        return reinterpret_cast<void*>(region.native + offset);
    }

    void invalidate(void* pointer) {
        auto native = reinterpret_cast<std::uintptr_t>(pointer);
        for (auto entry = regions_.begin(); entry != regions_.end();) {
            if (entry->second.native == native) entry = regions_.erase(entry);
            else ++entry;
        }
    }

    std::size_t region_count() const { return regions_.size(); }

private:
    struct Region { std::uintptr_t native; std::size_t size; };
    std::map<Address, Region> regions_;
    std::uint64_t next_ = 0x10000;
};

} // namespace cp2
