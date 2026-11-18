// Compares the native hspda sortnote/sortget (direct/runtime/hspda.hpp) with
// the original plugin's results recorded by hspda_windows_reference.cpp:
//   hspda_test model/chara_model.lst direct/tests/fixtures/hspda.reference
#include "hspda.hpp"
#include "hspda_cases.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string hex(const std::string& s) {
    std::string out;
    char b[4];
    for (unsigned char c : s) { std::snprintf(b, sizeof(b), "%02x", c); out += b; }
    return out.empty() ? "-" : out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s chara_model.lst reference\n", argv[0]); return 2; }
    cp2::hspda::Sorter sorter;
    std::vector<std::string> out;
    char line[256];
    std::string stored;
    auto get = [&](int n) {
        std::int32_t value = 12345;
        const int r = sorter.sortget(n, value);
        std::snprintf(line, sizeof(line), "GET %d RET %d VALUE %d%s", n, r, value, r < 0 ? " (unset)" : "");
        out.push_back(line);
    };
    get(0);
    std::vector<std::string> inputs = hspda_cases::cases();
    std::ifstream list(argv[1], std::ios::binary);
    if (!list) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    inputs.push_back(std::string((std::istreambuf_iterator<char>(list)), std::istreambuf_iterator<char>()));
    for (std::size_t k = 0; k < inputs.size(); ++k) {
        for (int order = 0; order < 2; ++order) {
            // The variable's text ends at its first NUL.
            const std::string text = inputs[k].substr(0, inputs[k].find('\0'));
            std::string sorted;
            const int r = sorter.sortnote(text, order, sorted);
            if (r == 0) stored = sorted;
            out.push_back("CASE " + std::to_string(k) + " ORDER " + std::to_string(order) + " RET " +
                          std::to_string(r) + " TEXT " + (r == 0 ? hex(sorted) : std::string("(unset)")));
            int lines = 0;
            for (std::size_t i = 0; i + 1 < stored.size(); ++i)
                if (stored[i] == '\r' && stored[i + 1] == '\n') ++lines;
            for (int n = -1; n <= lines; ++n) get(n);
        }
    }
    std::ifstream reference(argv[2], std::ios::binary);
    std::vector<std::string> expected;
    std::string l;
    while (std::getline(reference, l)) {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
        expected.push_back(l);
    }
    int mismatches = 0;
    for (std::size_t i = 0; i < out.size() || i < expected.size(); ++i) {
        const std::string a = i < expected.size() ? expected[i] : "<end>", b = i < out.size() ? out[i] : "<end>";
        if (a == b) continue;
        if (++mismatches <= 10) std::printf("line %zu\n  original: %.200s\n  native:   %.200s\n", i + 1, a.c_str(), b.c_str());
    }
    std::printf("hspda sortnote/sortget: %zu lines compared, %d mismatches\n", expected.size(), mismatches);
    return mismatches ? 1 : 0;
}
