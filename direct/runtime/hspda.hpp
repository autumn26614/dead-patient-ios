#pragma once
// hspda.dll's sortnote and sortget (the game's copy, SHA256 1033e651...),
// transcribed from the original x86 code and compared with it in
// direct/tests/hspda_test.cpp. The game uses only these two exports
// (model3dread sorts the character-model list and reads back each line's
// original position).
//
// sortnote var, order (0x10001c80):
//   - the variable's text (up to its first NUL) is split into lines
//     (0x100017d0 counts, 0x10001770 splits): a line ends at LF, CR or
//     CR LF, or at the end; a final line break adds no empty line;
//   - no lines: returns -1 and changes nothing (the previous index table
//     stays);
//   - otherwise the table is replaced by one entry per line (text, original
//     line number) and bubble-sorted (0x10001620): passes i = 1 .. n-1, each
//     comparing entries j and j-1 for j = n-1 down to i, swapping when
//     strcmp of unsigned bytes is < 0 (order 0) or > 0 (any other order);
//     equal lines keep their order;
//   - the lines are joined, each followed by CR LF (0x10001840), and stored
//     back into the variable as a string. Returns 0.
// sortget var, n (0x10001d40): before any sort, returns -1 and leaves the
//   variable; otherwise stores the original line number of sorted entry n,
//   or 0 when n is out of range, and returns 0.
#include <cstdint>
#include <string>
#include <vector>

namespace cp2 {
namespace hspda {

class Sorter {
public:
    // text: the variable's bytes up to its first NUL. On success `out` is the
    // joined result and 0 is returned.
    int sortnote(const std::string& text, int order, std::string& out) {
        std::vector<std::string> lines;
        std::vector<std::int32_t> numbers;
        std::size_t p = 0;
        const std::size_t size = text.size();
        while (p < size) {
            const std::size_t begin = p;
            std::size_t end = p;
            while (p < size) {
                const char c = text[p++];
                if (c == '\n') { end = p - 1; break; }
                if (c == '\r') {
                    end = p - 1;
                    if (p < size && text[p] == '\n') ++p;
                    break;
                }
                end = p;
            }
            numbers.push_back(static_cast<std::int32_t>(lines.size()));
            lines.push_back(text.substr(begin, end - begin));
        }
        const std::size_t n = lines.size();
        if (n == 0) return -1;
        auto compare = [](const std::string& a, const std::string& b) {
            const std::size_t k = a.size() < b.size() ? a.size() : b.size();
            for (std::size_t i = 0; i < k; ++i) {
                const unsigned char x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
                if (x != y) return x < y ? -1 : 1;
            }
            return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
        };
        for (std::size_t pass = 1; pass < n; ++pass) {
            for (std::size_t j = n - 1; j >= pass; --j) {
                const int c = compare(lines[j], lines[j - 1]);
                if (order == 0 ? c < 0 : c > 0) {
                    std::swap(lines[j], lines[j - 1]);
                    std::swap(numbers[j], numbers[j - 1]);
                }
            }
        }
        out.clear();
        for (const std::string& line : lines) out += line + "\r\n";
        table_ = numbers;
        sorted_ = true;
        return 0;
    }

    // Returns -1 before any sort (the variable is left alone); otherwise
    // sets `value` and returns 0.
    int sortget(std::int32_t n, std::int32_t& value) const {
        if (!sorted_) return -1;
        value = n >= 0 && static_cast<std::size_t>(n) < table_.size() ? table_[static_cast<std::size_t>(n)] : 0;
        return 0;
    }

private:
    std::vector<std::int32_t> table_;
    bool sorted_ = false;
};

}  // namespace hspda
}  // namespace cp2
