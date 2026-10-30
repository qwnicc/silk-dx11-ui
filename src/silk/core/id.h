#pragma once
// Widget identifiers: 32-bit FNV-1a hashes combined with a parent seed.
//
// Labels may carry a hidden suffix after "##" that participates in the hash
// but is not displayed, e.g. "Save##toolbar" vs "Save##menu".

#include <cstdint>
#include <string_view>

namespace silk {

using ID = uint32_t;

constexpr ID kFnvOffset = 2166136261u;
constexpr ID kFnvPrime = 16777619u;

constexpr ID HashBytes(std::string_view s, ID seed = kFnvOffset) {
    ID h = seed;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= kFnvPrime;
    }
    return h ? h : 1u; // 0 is reserved for "no id"
}

constexpr ID HashInt(uint32_t v, ID seed = kFnvOffset) {
    ID h = seed;
    for (int i = 0; i < 4; ++i) {
        h ^= (v >> (i * 8)) & 0xFF;
        h *= kFnvPrime;
    }
    return h ? h : 1u;
}

// Visible part of a label ("Text##hidden" -> "Text").
constexpr std::string_view DisplayText(std::string_view label) {
    const size_t p = label.find("##");
    return p == std::string_view::npos ? label : label.substr(0, p);
}

} // namespace silk
