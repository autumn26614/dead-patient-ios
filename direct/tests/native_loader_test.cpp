#include "native_loader.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    cp2::NativeLoader loader;
    assert(loader.load("cp2-library-that-does-not-exist.dll") == 0);
    assert(loader.load(nullptr) == 0);
    assert(!loader.close(1234));
    auto library = loader.load("/usr/lib/libSystem.B.dylib");
    assert(library >= 256 && library <= 0x7fffffff);
    auto symbol = loader.symbol(library, "strlen");
    assert(symbol != 0 && loader.symbol(library, "strlen") == symbol);
    using Length = std::size_t (*)(const char*);
    auto length = reinterpret_cast<Length>(loader.resolve_symbol(symbol));
    assert(length && length("direct recompilation") == std::strlen("direct recompilation"));
    assert(loader.symbol(library, "cp2_nonexistent_function") == 0);
    assert(loader.close(library));
    assert(!loader.resolve_symbol(symbol));
    assert(!loader.close(library));
    std::cout << "Native loader resolution, execution, and handle lifetime checks passed\n";
}
