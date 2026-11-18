#include "legacy_memory.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

static unsigned checks = 0;
static void require(bool value) {
    ++checks;
    if (!value) throw std::runtime_error("Legacy pointer test failed");
}
template<class Exception, class Function> static void rejects(Function function) {
    bool rejected = false;
    try { function(); } catch (const Exception&) { rejected = true; }
    require(rejected);
}

int main() {
    require(sizeof(void*) == 8);
    cp2::LegacyAddressSpace memory;
    std::array<unsigned char, 64> first{};
    std::array<unsigned char, 16> second{};
    auto base = memory.register_region(first.data(), first.size());
    require(base > 0 && base <= 0x7fffffff);
    require(memory.resolve(base, first.size()) == first.data());
    require(memory.register_region(first.data() + 4, 8) == base + 4);
    *static_cast<unsigned char*>(memory.resolve(base + 17, 1)) = 91;
    require(first[17] == 91);
    require(memory.resolve(base + 64, 0) == first.data() + 64);
    rejects<std::out_of_range>([&] { memory.resolve(base + 64, 1); });
    rejects<std::out_of_range>([&] { memory.resolve(base + 63, 2); });
    rejects<std::out_of_range>([&] { memory.resolve(0, 1); });
    require(memory.resolve(0, 0) == nullptr);
    rejects<std::invalid_argument>([&] { memory.register_region(nullptr, 64); });
    rejects<std::invalid_argument>([&] { memory.register_region(first.data(), 0); });
    rejects<std::invalid_argument>([&] { memory.register_region(first.data() + 32, 64); });
    auto other = memory.register_region(second.data(), second.size());
    require(other != base && memory.resolve(other, 16) == second.data());
    require(memory.region_count() == 2);
    memory.invalidate(first.data());
    rejects<std::out_of_range>([&] { memory.resolve(base, 1); });
    require(memory.region_count() == 1);
    auto replacement = memory.register_region(first.data(), first.size());
    require(replacement != base);
    rejects<std::out_of_range>([&] { memory.resolve(base + 17, 1); });
    std::cout << checks << " legacy pointer checks passed\n";
}
