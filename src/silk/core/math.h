#pragma once
// Basic math types used across Silk: Vec2, Rect, Color and helpers.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace silk {

inline constexpr float kPi = 3.14159265358979323846f;

constexpr float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
constexpr float Saturate(float v) { return Clamp(v, 0.0f, 1.0f); }
constexpr float Lerp(float a, float b, float t) { return a + (b - a) * t; }

struct Vec2 {
    float x = 0.0f, y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator*(Vec2 o) const { return {x * o.x, y * o.y}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    constexpr Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    constexpr bool operator==(const Vec2&) const = default;

    float Length() const { return std::sqrt(x * x + y * y); }
};

inline constexpr Vec2 Lerp(Vec2 a, Vec2 b, float t) { return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t)}; }

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), w(w_), h(h_) {}
    constexpr Rect(Vec2 pos, Vec2 size) : x(pos.x), y(pos.y), w(size.x), h(size.y) {}

    static constexpr Rect FromMinMax(Vec2 a, Vec2 b) { return {a.x, a.y, b.x - a.x, b.y - a.y}; }

    constexpr Vec2 Pos() const { return {x, y}; }
    constexpr Vec2 Size() const { return {w, h}; }
    constexpr Vec2 Min() const { return {x, y}; }
    constexpr Vec2 Max() const { return {x + w, y + h}; }
    constexpr Vec2 Center() const { return {x + w * 0.5f, y + h * 0.5f}; }
    constexpr float Right() const { return x + w; }
    constexpr float Bottom() const { return y + h; }

    constexpr bool Contains(Vec2 p) const { return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h; }
    constexpr bool Overlaps(const Rect& o) const {
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }
    constexpr Rect Expand(float d) const { return {x - d, y - d, w + 2 * d, h + 2 * d}; }
    constexpr Rect Expand(float dx, float dy) const { return {x - dx, y - dy, w + 2 * dx, h + 2 * dy}; }
    constexpr Rect Translate(Vec2 d) const { return {x + d.x, y + d.y, w, h}; }
    Rect Intersect(const Rect& o) const {
        const float x0 = std::max(x, o.x), y0 = std::max(y, o.y);
        const float x1 = std::min(x + w, o.x + o.w), y1 = std::min(y + h, o.y + o.h);
        return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
    }
    constexpr bool operator==(const Rect&) const = default;
};

// Straight-alpha RGBA color in [0,1] (sRGB encoded).
struct Color {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}

    // 0xRRGGBB plus explicit alpha.
    static constexpr Color Hex(uint32_t rgb, float alpha = 1.0f) {
        return {float((rgb >> 16) & 0xFF) / 255.0f, float((rgb >> 8) & 0xFF) / 255.0f,
                float(rgb & 0xFF) / 255.0f, alpha};
    }

    constexpr Color WithAlpha(float alpha) const { return {r, g, b, alpha}; }
    constexpr Color MulAlpha(float k) const { return {r, g, b, a * k}; }
    constexpr bool operator==(const Color&) const = default;

    // Packs into R8G8B8A8_UNORM byte order (little-endian uint32).
    uint32_t Pack() const {
        auto c = [](float v) { return uint32_t(Saturate(v) * 255.0f + 0.5f); };
        return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
    }
};

inline constexpr Color Lerp(const Color& a, const Color& b, float t) {
    return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t)};
}

// HSV <-> RGB, all components in [0,1].
inline Color HsvToRgb(float h, float s, float v, float a = 1.0f) {
    h = h - std::floor(h);
    const float k[3] = {5.0f, 3.0f, 1.0f};
    float out[3];
    for (int i = 0; i < 3; ++i) {
        const float kk = std::fmod(k[i] + h * 6.0f, 6.0f);
        out[i] = v - v * s * std::max(0.0f, std::min({kk, 4.0f - kk, 1.0f}));
    }
    return {out[0], out[1], out[2], a};
}

inline void RgbToHsv(const Color& c, float& h, float& s, float& v) {
    const float mx = std::max({c.r, c.g, c.b});
    const float mn = std::min({c.r, c.g, c.b});
    const float d = mx - mn;
    v = mx;
    s = mx > 0.0f ? d / mx : 0.0f;
    if (d <= 1e-6f) { h = 0.0f; return; }
    if (mx == c.r)      h = std::fmod((c.g - c.b) / d, 6.0f);
    else if (mx == c.g) h = (c.b - c.r) / d + 2.0f;
    else                h = (c.r - c.g) / d + 4.0f;
    h /= 6.0f;
    if (h < 0.0f) h += 1.0f;
}

} // namespace silk
