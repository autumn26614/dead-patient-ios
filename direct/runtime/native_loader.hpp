#pragma once

#include <cstdint>
#include <dlfcn.h>
#include <map>
#include <limits>

namespace cp2 {

// Return actual native modules through opaque 32-bit handles. Missing Windows
// DLLs fail with zero, allowing the original game's optional-library checks to run.
class NativeLoader {
public:
    using Handle = std::uint32_t;
    ~NativeLoader() {
        for (const auto& entry : libraries_) dlclose(entry.second);
    }

    Handle load(const char* name) {
        if (!name || !*name) return 0;
        void* library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (!library) return 0;
        if (next_ == std::numeric_limits<std::int32_t>::max()) {
            dlclose(library);
            return 0;
        }
        Handle handle = next_++;
        libraries_.emplace(handle, library);
        return handle;
    }

    Handle symbol(Handle library, const char* name) {
        auto owner = libraries_.find(library);
        if (owner == libraries_.end() || !name || !*name) return 0;
        void* pointer = dlsym(owner->second, name);
        if (!pointer) return 0;
        for (const auto& entry : symbols_)
            if (entry.second.owner == library && entry.second.pointer == pointer) return entry.first;
        if (next_ == std::numeric_limits<std::int32_t>::max()) return 0;
        Handle handle = next_++;
        symbols_.emplace(handle, Symbol{library, pointer});
        return handle;
    }

    void* resolve_symbol(Handle symbol) const {
        auto entry = symbols_.find(symbol);
        return entry == symbols_.end() ? nullptr : entry->second.pointer;
    }

    bool close(Handle library) {
        auto entry = libraries_.find(library);
        if (entry == libraries_.end()) return false;
        if (dlclose(entry->second)) return false;
        libraries_.erase(entry);
        for (auto symbol = symbols_.begin(); symbol != symbols_.end();)
            if (symbol->second.owner == library) symbol = symbols_.erase(symbol);
            else ++symbol;
        return true;
    }

private:
    struct Symbol { Handle owner; void* pointer; };
    // The game's llmod helper treats values below 256 as module-table indices.
    Handle next_ = 0x70000000;
    std::map<Handle, void*> libraries_;
    std::map<Handle, Symbol> symbols_;
};

} // namespace cp2
