#pragma once
// DirectX .x file reader used by the native CSMesh backend.
//
// Supports every encoding shipped with the original game: text ("txt "),
// binary ("bin ") and MSZIP-compressed binary ("bzip"). The result is a tree of
// data objects whose member values are kept as the packed 32-bit stream that
// d3dxof's IDirectXFileData::GetData returns, so higher layers can reproduce
// the original plugin's fixed-stride reads exactly. Template declarations are
// validated syntactically and skipped; members are interpreted by the caller.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace cp2 {
namespace xfile {

// ---------------------------------------------------------------------------
// Raw DEFLATE decoder with history carried across MSZIP blocks.

class Inflater {
public:
    // Appends one raw deflate stream to out. Back-references may reach into
    // bytes already present in out (the MSZIP block dictionary).
    static bool inflate(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                        std::size_t history_limit, std::string& error) {
        Inflater state(data, size, out, out.size() > history_limit ? out.size() - history_limit : 0);
        return state.run(error);
    }

private:
    struct Huffman {
        std::uint16_t counts[16];
        std::uint16_t symbols[320];
    };

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t position_ = 0;
    std::uint32_t bit_buffer_ = 0;
    int bit_count_ = 0;
    std::vector<std::uint8_t>& out_;
    std::size_t window_start_;

    Inflater(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out, std::size_t window_start)
        : data_(data), size_(size), out_(out), window_start_(window_start) {}

    bool bits(int count, std::uint32_t& value) {
        while (bit_count_ < count) {
            if (position_ >= size_) return false;
            bit_buffer_ |= static_cast<std::uint32_t>(data_[position_++]) << bit_count_;
            bit_count_ += 8;
        }
        value = bit_buffer_ & ((count == 32) ? 0xffffffffu : ((1u << count) - 1u));
        bit_buffer_ = count == 32 ? 0 : (bit_buffer_ >> count);
        bit_count_ -= count;
        return true;
    }

    static bool build(Huffman& table, const std::uint8_t* lengths, int count) {
        std::memset(table.counts, 0, sizeof(table.counts));
        for (int i = 0; i < count; ++i) table.counts[lengths[i]]++;
        table.counts[0] = 0;
        int left = 1;
        for (int length = 1; length < 16; ++length) {
            left <<= 1;
            left -= table.counts[length];
            if (left < 0) return false;  // over-subscribed
        }
        std::uint16_t offsets[16];
        offsets[1] = 0;
        for (int length = 1; length < 15; ++length) offsets[length + 1] = offsets[length] + table.counts[length];
        for (int symbol = 0; symbol < count; ++symbol)
            if (lengths[symbol]) table.symbols[offsets[lengths[symbol]]++] = static_cast<std::uint16_t>(symbol);
        return true;
    }

    bool decode(const Huffman& table, int& symbol) {
        int code = 0, first = 0, index = 0;
        for (int length = 1; length < 16; ++length) {
            std::uint32_t bit;
            if (!bits(1, bit)) return false;
            code |= static_cast<int>(bit);
            const int count = table.counts[length];
            if (code - count < first) {
                symbol = table.symbols[index + (code - first)];
                return true;
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return false;
    }

    bool stored(std::string& error) {
        bit_buffer_ = 0;
        bit_count_ = 0;
        if (size_ - position_ < 4) { error = "truncated stored block"; return false; }
        const unsigned length = data_[position_] | (data_[position_ + 1] << 8);
        const unsigned complement = data_[position_ + 2] | (data_[position_ + 3] << 8);
        position_ += 4;
        if ((length ^ 0xffffu) != complement) { error = "stored block length mismatch"; return false; }
        if (size_ - position_ < length) { error = "truncated stored block"; return false; }
        out_.insert(out_.end(), data_ + position_, data_ + position_ + length);
        position_ += length;
        return true;
    }

    bool codes(const Huffman& lengths, const Huffman& distances, std::string& error) {
        static const std::uint16_t length_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                                      35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const std::uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                                      3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const std::uint16_t distance_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                                        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                                        6145, 8193, 12289, 16385, 24577};
        static const std::uint8_t distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                                        6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol;
            if (!decode(lengths, symbol)) { error = "invalid literal/length code"; return false; }
            if (symbol < 256) {
                out_.push_back(static_cast<std::uint8_t>(symbol));
            } else if (symbol == 256) {
                return true;
            } else {
                symbol -= 257;
                if (symbol >= 29) { error = "invalid length symbol"; return false; }
                std::uint32_t extra;
                if (!bits(length_extra[symbol], extra)) { error = "truncated length"; return false; }
                const std::size_t length = length_base[symbol] + extra;
                int distance_symbol;
                if (!decode(distances, distance_symbol) || distance_symbol >= 30) {
                    error = "invalid distance code";
                    return false;
                }
                if (!bits(distance_extra[distance_symbol], extra)) { error = "truncated distance"; return false; }
                const std::size_t distance = distance_base[distance_symbol] + extra;
                if (distance > out_.size() - window_start_) { error = "distance before dictionary"; return false; }
                const std::size_t from = out_.size() - distance;
                for (std::size_t i = 0; i < length; ++i) out_.push_back(out_[from + i]);
            }
        }
    }

    bool fixed(std::string& error) {
        std::uint8_t lengths[320];
        for (int i = 0; i < 144; ++i) lengths[i] = 8;
        for (int i = 144; i < 256; ++i) lengths[i] = 9;
        for (int i = 256; i < 280; ++i) lengths[i] = 7;
        for (int i = 280; i < 288; ++i) lengths[i] = 8;
        Huffman literal, distance;
        build(literal, lengths, 288);
        for (int i = 0; i < 30; ++i) lengths[i] = 5;
        build(distance, lengths, 30);
        return codes(literal, distance, error);
    }

    bool dynamic(std::string& error) {
        static const std::uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        std::uint32_t literal_count, distance_count, code_count;
        if (!bits(5, literal_count) || !bits(5, distance_count) || !bits(4, code_count)) {
            error = "truncated dynamic header";
            return false;
        }
        literal_count += 257;
        distance_count += 1;
        code_count += 4;
        if (literal_count > 286 || distance_count > 30) { error = "bad dynamic counts"; return false; }
        std::uint8_t lengths[320] = {};
        for (std::uint32_t i = 0; i < code_count; ++i) {
            std::uint32_t value;
            if (!bits(3, value)) { error = "truncated code lengths"; return false; }
            lengths[order[i]] = static_cast<std::uint8_t>(value);
        }
        Huffman code_table;
        if (!build(code_table, lengths, 19)) { error = "bad code length table"; return false; }
        std::uint32_t index = 0;
        while (index < literal_count + distance_count) {
            int symbol;
            if (!decode(code_table, symbol)) { error = "bad code length symbol"; return false; }
            if (symbol < 16) {
                lengths[index++] = static_cast<std::uint8_t>(symbol);
                continue;
            }
            std::uint8_t value = 0;
            std::uint32_t repeat;
            if (symbol == 16) {
                if (index == 0) { error = "repeat without length"; return false; }
                value = lengths[index - 1];
                if (!bits(2, repeat)) { error = "truncated repeat"; return false; }
                repeat += 3;
            } else if (symbol == 17) {
                if (!bits(3, repeat)) { error = "truncated repeat"; return false; }
                repeat += 3;
            } else {
                if (!bits(7, repeat)) { error = "truncated repeat"; return false; }
                repeat += 11;
            }
            if (index + repeat > literal_count + distance_count) { error = "too many lengths"; return false; }
            while (repeat--) lengths[index++] = value;
        }
        if (lengths[256] == 0) { error = "missing end-of-block code"; return false; }
        Huffman literal, distance;
        if (!build(literal, lengths, static_cast<int>(literal_count)) ||
            !build(distance, lengths + literal_count, static_cast<int>(distance_count))) {
            error = "bad dynamic Huffman table";
            return false;
        }
        return codes(literal, distance, error);
    }

    bool run(std::string& error) {
        std::uint32_t last = 0;
        do {
            std::uint32_t type;
            if (!bits(1, last) || !bits(2, type)) { error = "truncated block header"; return false; }
            bool ok;
            if (type == 0) ok = stored(error);
            else if (type == 1) ok = fixed(error);
            else if (type == 2) ok = dynamic(error);
            else { error = "invalid block type"; return false; }
            if (!ok) return false;
        } while (!last);
        return true;
    }
};

// ---------------------------------------------------------------------------
// Data model.

struct Value {
    enum Kind { Integer, Float, String };
    Kind kind = Integer;
    std::uint32_t word = 0;  // Integer value, or IEEE-754 binary32 bits for Float
    std::string text;        // String contents (CP932 bytes preserved)

    float as_float() const {
        if (kind == Float) {
            float result;
            std::memcpy(&result, &word, sizeof(result));
            return result;
        }
        return static_cast<float>(word);
    }
};

struct Object;

struct Child {
    std::shared_ptr<Object> object;  // inline data object, or null for a reference
    std::string reference;           // referenced object name when object is null
};

struct Object {
    std::string type;
    std::string name;
    bool has_name = false;
    std::vector<Value> values;
    std::vector<Child> children;
};

struct Document {
    std::string format;  // "txt ", "bin ", "bzip" or "tzip"
    int float_bits = 32;
    std::vector<std::shared_ptr<Object>> objects;

    // d3dxof resolves references against named top-level and nested objects.
    std::shared_ptr<Object> find(const std::string& name) const {
        for (const auto& object : objects) {
            if (auto found = find_in(object, name)) return found;
        }
        return nullptr;
    }

private:
    static std::shared_ptr<Object> find_in(const std::shared_ptr<Object>& object, const std::string& name) {
        if (object->has_name && object->name == name) return object;
        for (const Child& child : object->children)
            if (child.object)
                if (auto found = find_in(child.object, name)) return found;
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// Tokens shared by the text and binary encodings.

enum TokenKind {
    T_END = 0,
    T_NAME = 1,
    T_STRING = 2,
    T_INTEGER = 3,
    T_GUID = 5,
    T_INTEGER_LIST = 6,
    T_FLOAT_LIST = 7,
    T_OBRACE = 10,
    T_CBRACE = 11,
    T_OPAREN = 12,
    T_CPAREN = 13,
    T_OBRACKET = 14,
    T_CBRACKET = 15,
    T_OANGLE = 16,
    T_CANGLE = 17,
    T_DOT = 18,
    T_COMMA = 19,
    T_SEMICOLON = 20,
    T_TEMPLATE = 31,
    T_FLOAT = 1000,  // text-only single float literal
};

struct Token {
    int kind = T_END;
    std::string text;
    std::vector<Value> values;  // INTEGER/FLOAT literals and lists
};

class BinaryLexer {
public:
    BinaryLexer(const std::uint8_t* data, std::size_t size, int float_bits)
        : data_(data), size_(size), float_bits_(float_bits) {}

    bool next(Token& token, std::string& error) {
        token = Token();
        if (position_ == size_) return true;  // T_END
        std::uint16_t kind;
        if (!read(&kind, 2)) { error = "truncated token"; return false; }
        token.kind = kind;
        std::uint32_t count;
        switch (kind) {
        case T_NAME:
        case T_STRING:
            if (!read(&count, 4) || size_ - position_ < count) { error = "truncated name/string"; return false; }
            token.text.assign(reinterpret_cast<const char*>(data_ + position_), count);
            position_ += count;
            return true;
        case T_INTEGER: {
            Value value;
            if (!read(&value.word, 4)) { error = "truncated integer"; return false; }
            token.values.push_back(value);
            return true;
        }
        case T_GUID:
            if (size_ - position_ < 16) { error = "truncated GUID"; return false; }
            token.text.assign(reinterpret_cast<const char*>(data_ + position_), 16);
            position_ += 16;
            return true;
        case T_INTEGER_LIST:
            if (!read(&count, 4) || (size_ - position_) / 4 < count) { error = "truncated integer list"; return false; }
            token.values.resize(count);
            for (std::uint32_t i = 0; i < count; ++i) read(&token.values[i].word, 4);
            return true;
        case T_FLOAT_LIST: {
            const std::size_t width = float_bits_ == 64 ? 8 : 4;
            if (!read(&count, 4) || (size_ - position_) / width < count) { error = "truncated float list"; return false; }
            token.values.resize(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                Value& value = token.values[i];
                value.kind = Value::Float;
                if (width == 4) {
                    read(&value.word, 4);
                } else {
                    double wide;
                    read(&wide, 8);
                    const float narrow = static_cast<float>(wide);
                    std::memcpy(&value.word, &narrow, 4);
                }
            }
            return true;
        }
        default:
            if ((kind >= T_OBRACE && kind <= T_SEMICOLON) || kind == T_TEMPLATE || (kind >= 40 && kind <= 52)) return true;
            error = "unknown binary token " + std::to_string(kind);
            return false;
        }
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t position_ = 0;
    int float_bits_;

    bool read(void* out, std::size_t count) {
        if (size_ - position_ < count) return false;
        std::memcpy(out, data_ + position_, count);  // little-endian file and host
        position_ += count;
        return true;
    }
};

class TextLexer {
public:
    TextLexer(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool next(Token& token, std::string& error) {
        token = Token();
        skip_space();
        if (position_ >= size_) return true;
        const char c = static_cast<char>(data_[position_]);
        switch (c) {
        case '{': ++position_; token.kind = T_OBRACE; return true;
        case '}': ++position_; token.kind = T_CBRACE; return true;
        case '(': ++position_; token.kind = T_OPAREN; return true;
        case ')': ++position_; token.kind = T_CPAREN; return true;
        case '[': ++position_; token.kind = T_OBRACKET; return true;
        case ']': ++position_; token.kind = T_CBRACKET; return true;
        case ',': ++position_; token.kind = T_COMMA; return true;
        case ';': ++position_; token.kind = T_SEMICOLON; return true;
        case '<': {
            const std::size_t end = find('>');
            if (end == size_) { error = "unterminated GUID"; return false; }
            token.kind = T_GUID;
            token.text.assign(reinterpret_cast<const char*>(data_ + position_ + 1), end - position_ - 1);
            position_ = end + 1;
            return true;
        }
        case '"': {
            const std::size_t end = find('"', position_ + 1);
            if (end == size_) { error = "unterminated string"; return false; }
            token.kind = T_STRING;
            // A doubled backslash stands for one: the original's loaders store
            // "C:\\Users\\..." with single backslashes (tinori01's texture,
            // the only backslashes in the game's text files). Any other
            // backslash sequence (an escaped quote, a CP932 trail byte 0x5c)
            // is not determined by the game's data and stops here.
            token.text.clear();
            for (std::size_t i = position_ + 1; i < end; ++i) {
                const unsigned char b = data_[i];
                if (b == '\\') {
                    if (i + 1 < end && data_[i + 1] == '\\') {
                        token.text += '\\';
                        ++i;
                        continue;
                    }
                    error = "a string with a backslash sequence other than \\\\ is not implemented";
                    return false;
                }
                token.text += static_cast<char>(b);
            }
            position_ = end + 1;
            return true;
        }
        default: break;
        }
        if (c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9')) return number(token, error);
        if (is_name_byte(data_[position_])) {
            const std::size_t begin = position_;
            while (position_ < size_ && is_name_byte(data_[position_])) ++position_;
            token.text.assign(reinterpret_cast<const char*>(data_ + begin), position_ - begin);
            token.kind = token.text == "template" ? T_TEMPLATE : T_NAME;
            return true;
        }
        error = std::string("unexpected text byte '") + c + "'";
        return false;
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t position_ = 0;

    static bool is_name_byte(std::uint8_t byte) {
        return byte >= 0x80 || (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
               (byte >= '0' && byte <= '9') || byte == '_' || byte == '-' || byte == '#' || byte == '.';
    }

    std::size_t find(char wanted, std::size_t from = std::size_t(-1)) const {
        std::size_t i = from == std::size_t(-1) ? position_ : from;
        while (i < size_ && data_[i] != static_cast<std::uint8_t>(wanted)) ++i;
        return i;
    }

    void skip_space() {
        while (position_ < size_) {
            const std::uint8_t c = data_[position_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0x1a) {
                ++position_;
            } else if (c == '#' || (c == '/' && position_ + 1 < size_ && data_[position_ + 1] == '/')) {
                while (position_ < size_ && data_[position_] != '\n') ++position_;
            } else {
                break;
            }
        }
    }

    bool number(Token& token, std::string& error) {
        const std::size_t begin = position_;
        bool is_float = false;
        if (data_[position_] == '-' || data_[position_] == '+') ++position_;
        while (position_ < size_) {
            const char c = static_cast<char>(data_[position_]);
            if (c >= '0' && c <= '9') { ++position_; continue; }
            if (c == '.' || c == 'e' || c == 'E') {
                is_float = true;
                ++position_;
                if ((c == 'e' || c == 'E') && position_ < size_ && (data_[position_] == '-' || data_[position_] == '+'))
                    ++position_;
                continue;
            }
            break;
        }
        const std::string literal(reinterpret_cast<const char*>(data_ + begin), position_ - begin);
        if (literal.empty() || literal == "-" || literal == "+") { error = "bad number"; return false; }
        Value value;
        if (is_float) {
            value.kind = Value::Float;
            // d3dxof converts text numbers through double precision.
            const float parsed = static_cast<float>(std::strtod(literal.c_str(), nullptr));
            std::memcpy(&value.word, &parsed, 4);
            token.kind = T_FLOAT;
        } else {
            const long long parsed = std::strtoll(literal.c_str(), nullptr, 10);
            value.word = static_cast<std::uint32_t>(parsed);
            token.kind = T_INTEGER;
        }
        token.values.push_back(value);
        return true;
    }
};

// ---------------------------------------------------------------------------
// Parser over the token stream.

template <typename Lexer>
class Parser {
public:
    explicit Parser(Lexer& lexer) : lexer_(lexer) {}

    bool parse(Document& document, std::string& error) {
        if (!advance(error)) return false;
        while (current_.kind != T_END) {
            if (current_.kind == T_TEMPLATE) {
                if (!skip_template(error)) return false;
                continue;
            }
            if (current_.kind != T_NAME) { error = "expected data object at top level"; return false; }
            auto object = std::make_shared<Object>();
            if (!data_object(*object, error)) return false;
            document.objects.push_back(object);
        }
        return true;
    }

private:
    Lexer& lexer_;
    Token current_;

    bool advance(std::string& error) { return lexer_.next(current_, error); }

    bool skip_template(std::string& error) {
        if (!advance(error)) return false;
        if (current_.kind != T_NAME) { error = "template without name"; return false; }
        if (!advance(error)) return false;
        if (current_.kind != T_OBRACE) { error = "template without body"; return false; }
        int depth = 0;
        do {
            if (current_.kind == T_END) { error = "unterminated template"; return false; }
            if (current_.kind == T_OBRACE) ++depth;
            if (current_.kind == T_CBRACE) --depth;
            if (!advance(error)) return false;
        } while (depth > 0);
        return true;
    }

    // Current token is the template name of the object.
    bool data_object(Object& object, std::string& error) {
        object.type = current_.text;
        if (!advance(error)) return false;
        if (current_.kind == T_NAME) {
            object.name = current_.text;
            object.has_name = true;
            if (!advance(error)) return false;
        }
        if (current_.kind == T_GUID && !advance(error)) return false;
        if (current_.kind != T_OBRACE) { error = "expected '{' after " + object.type; return false; }
        if (!advance(error)) return false;
        for (;;) {
            switch (current_.kind) {
            case T_END: error = "unterminated data object " + object.type; return false;
            case T_CBRACE: return advance(error);
            case T_COMMA:
            case T_SEMICOLON:
                if (!advance(error)) return false;
                break;
            case T_INTEGER:
            case T_FLOAT:
            case T_INTEGER_LIST:
            case T_FLOAT_LIST:
                object.values.insert(object.values.end(), current_.values.begin(), current_.values.end());
                if (!advance(error)) return false;
                break;
            case T_STRING: {
                Value value;
                value.kind = Value::String;
                value.text = current_.text;
                object.values.push_back(value);
                if (!advance(error)) return false;
                break;
            }
            case T_OBRACE: {  // reference: { name [guid] } or { guid }
                if (!advance(error)) return false;
                Child child;
                if (current_.kind == T_NAME) {
                    child.reference = current_.text;
                    if (!advance(error)) return false;
                }
                if (current_.kind == T_GUID && !advance(error)) return false;
                if (current_.kind != T_CBRACE) { error = "malformed reference in " + object.type; return false; }
                if (!advance(error)) return false;
                object.children.push_back(child);
                break;
            }
            case T_NAME: {
                Child child;
                child.object = std::make_shared<Object>();
                if (!data_object(*child.object, error)) return false;
                object.children.push_back(child);
                break;
            }
            default:
                error = "unexpected token " + std::to_string(current_.kind) + " in " + object.type;
                return false;
            }
        }
    }
};

inline bool parse(const std::uint8_t* data, std::size_t size, Document& document, std::string& error) {
    if (size < 16 || std::memcmp(data, "xof ", 4) != 0) { error = "not a DirectX file"; return false; }
    document = Document();
    document.format.assign(reinterpret_cast<const char*>(data + 8), 4);
    if (std::memcmp(data + 12, "0064", 4) == 0) document.float_bits = 64;
    else if (std::memcmp(data + 12, "0032", 4) == 0) document.float_bits = 32;
    else { error = "unsupported float size"; return false; }

    std::vector<std::uint8_t> expanded;
    const std::uint8_t* body = data + 16;
    std::size_t body_size = size - 16;
    if (document.format == "bzip" || document.format == "tzip") {
        if (size < 20) { error = "truncated compressed header"; return false; }
        std::uint32_t total;
        std::memcpy(&total, data + 16, 4);
        std::size_t position = 20;
        while (position < size) {
            if (size - position < 6) { error = "truncated MSZIP block header"; return false; }
            const std::uint16_t uncompressed = static_cast<std::uint16_t>(data[position] | (data[position + 1] << 8));
            const std::uint16_t compressed = static_cast<std::uint16_t>(data[position + 2] | (data[position + 3] << 8));
            position += 4;
            if (compressed < 2 || size - position < compressed || data[position] != 'C' || data[position + 1] != 'K') {
                error = "bad MSZIP block";
                return false;
            }
            const std::size_t before = expanded.size();
            if (!Inflater::inflate(data + position + 2, compressed - 2u, expanded, 32768, error)) return false;
            if (expanded.size() - before != uncompressed) { error = "MSZIP block size mismatch"; return false; }
            position += compressed;
        }
        if (total < 16 || expanded.size() != total - 16u) { error = "MSZIP total size mismatch"; return false; }
        body = expanded.data();
        body_size = expanded.size();
    }
    if (document.format == "txt " || document.format == "tzip") {
        TextLexer lexer(body, body_size);
        Parser<TextLexer> parser(lexer);
        return parser.parse(document, error);
    }
    if (document.format == "bin " || document.format == "bzip") {
        BinaryLexer lexer(body, body_size, document.float_bits);
        Parser<BinaryLexer> parser(lexer);
        return parser.parse(document, error);
    }
    error = "unsupported DirectX file format";
    return false;
}

}  // namespace xfile
}  // namespace cp2
