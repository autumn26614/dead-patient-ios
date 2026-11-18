#pragma once
// Inputs for the hspda sortnote/sortget comparison, shared by the Windows
// reference harness (original hspda.dll) and the native test. Each case is
// sorted with order 0 and order 1; the game's own list
// (model/chara_model.lst, read by model3dread) is appended by the callers.
#include <string>
#include <vector>

namespace hspda_cases {

inline std::vector<std::string> cases() {
    return {
        "",                                     // no lines: -1, table kept
        "b\r\na\r\nc\r\na\r\n",                 // CR LF, duplicates
        "b\na\nc\na",                           // LF, no final break
        "b\ra\rc\r",                            // CR only
        "b\r\n\r\na\r\n\r\n",                   // empty lines
        "a\r\r\nb\n\rc",                        // mixed breaks
        "abc\r\nab\r\nabd\r\nAb\r\n",           // prefixes, case
        "\x82\xc8\x82\xb5\r\nz\r\n\x82\xa0\r\n\x7f\r\n\x80\r\n",  // bytes above 0x7f are larger
        "x\r\nx\r\nx\r\nw\r\ny\r\nx\r\n",       // stability, both orders
        "single",
        "",                                     // no lines after a sort: the table stays
        "\r\n",                                 // one empty line
    };
}

// Indices read back with sortget after each sort: -1 .. lines (out of range
// at both ends).
}  // namespace hspda_cases
