#pragma once
// Minimal UTF-8 helpers used by text rendering and the text input widget.

#include <cstdint>
#include <string>
#include <string_view>

namespace silk::utf8 {

// Decodes one code point starting at s[i] and advances i. Invalid bytes yield U+FFFD.
inline uint32_t Decode(std::string_view s, size_t& i) {
    const auto b0 = static_cast<uint8_t>(s[i]);
    auto cont = [&](size_t k) -> uint32_t {
        return (i + k < s.size()) ? (static_cast<uint8_t>(s[i + k]) & 0x3Fu) : 0u;
    };
    if (b0 < 0x80) { i += 1; return b0; }
    if ((b0 >> 5) == 0x6 && i + 1 < s.size()) {
        const uint32_t cp = ((b0 & 0x1Fu) << 6) | cont(1);
        i += 2; return cp;
    }
    if ((b0 >> 4) == 0xE && i + 2 < s.size()) {
        const uint32_t cp = ((b0 & 0x0Fu) << 12) | (cont(1) << 6) | cont(2);
        i += 3; return cp;
    }
    if ((b0 >> 3) == 0x1E && i + 3 < s.size()) {
        const uint32_t cp = ((b0 & 0x07u) << 18) | (cont(1) << 12) | (cont(2) << 6) | cont(3);
        i += 4; return cp;
    }
    i += 1;
    return 0xFFFD;
}

inline void Append(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

inline bool IsContinuation(char c) { return (static_cast<uint8_t>(c) & 0xC0) == 0x80; }

// Byte index of the previous / next code point boundary.
inline size_t Prev(std::string_view s, size_t i) {
    if (i == 0) return 0;
    --i;
    while (i > 0 && IsContinuation(s[i])) --i;
    return i;
}
inline size_t Next(std::string_view s, size_t i) {
    if (i >= s.size()) return s.size();
    ++i;
    while (i < s.size() && IsContinuation(s[i])) ++i;
    return i;
}

std::wstring ToWide(std::string_view s);
std::string FromWide(std::wstring_view w);

} // namespace silk::utf8
