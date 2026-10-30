#pragma once
// FreeType-based SDF fonts.
//
// Glyphs are rasterised lazily with FreeType's SDF renderer at a single base
// size and packed into a dynamic R8 atlas. The pixel shader reconstructs a
// sharp, anti-aliased edge at any display size from the distance field.

#include "silk/core/math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct FT_LibraryRec_;
struct FT_FaceRec_;

namespace silk {

// Single-channel texture atlas with a simple shelf packer.
class GlyphAtlas {
public:
    GlyphAtlas(int width, int height);
    ~GlyphAtlas();
    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    bool CreateTexture(ID3D11Device* device);
    // Reserves a w*h region; returns false when the atlas is full.
    bool Pack(int w, int h, int& outX, int& outY);
    void Blit(int x, int y, int w, int h, const uint8_t* src, int srcPitch);
    // Uploads dirty rows to the GPU.
    void Upload(ID3D11DeviceContext* ctx);

    int Width() const { return width_; }
    int Height() const { return height_; }
    ID3D11ShaderResourceView* Srv() const { return srv_; }

private:
    int width_, height_;
    std::vector<uint8_t> pixels_;
    int shelfX_ = 1, shelfY_ = 1, shelfH_ = 0;
    int dirtyY0_ = 0, dirtyY1_ = 0;
    ID3D11Texture2D* tex_ = nullptr;
    ID3D11ShaderResourceView* srv_ = nullptr;
};

struct Glyph {
    float advance = 0.0f;              // base-size pixels
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // quad relative to pen at baseline (y down)
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    bool visible = false;              // false for whitespace / missing bitmaps
};

class Font {
public:
    ~Font();

    bool Load(FT_LibraryRec_* lib, const std::string& path, GlyphAtlas* atlas, float baseSize, int spread);

    const Glyph& GetGlyph(uint32_t codepoint);
    float Kerning(uint32_t left, uint32_t right);
    bool HasGlyph(uint32_t codepoint) const;

    // Metrics scaled to `size` (pixels per em).
    float Ascent(float size) const { return ascent_ * size; }
    float Descent(float size) const { return descent_ * size; }
    float LineHeight(float size) const { return (ascent_ + descent_ + lineGap_) * size; }
    float CapHeight(float size) const { return capHeight_ * size; }
    float Scale(float size) const { return size / baseSize_; }

    Vec2 Measure(std::string_view text, float size);
    // Byte offset of the code point nearest to x (for text input hit testing).
    size_t HitTest(std::string_view text, float size, float x);
    // x offset of byte position `byte` within `text`.
    float OffsetOf(std::string_view text, float size, size_t byte);

    GlyphAtlas* Atlas() const { return atlas_; }
    float SdfSpread() const { return float(spread_); }

private:
    FT_FaceRec_* face_ = nullptr;
    GlyphAtlas* atlas_ = nullptr;
    float baseSize_ = 32.0f;
    int spread_ = 6;
    float ascent_ = 0.8f, descent_ = 0.2f, lineGap_ = 0.0f, capHeight_ = 0.7f; // in ems
    bool hasKerning_ = false;
    std::unordered_map<uint32_t, Glyph> glyphs_;
    std::unordered_map<uint64_t, float> kerningCache_;
};

// Owns the FreeType library, atlases and the standard font set.
class FontSystem {
public:
    FontSystem();
    ~FontSystem();

    bool Init(ID3D11Device* device, const std::string& assetDir);
    void Upload(ID3D11DeviceContext* ctx);

    Font& Regular() { return *regular_; }
    Font& Medium() { return *medium_; }
    Font& Bold() { return *bold_; }
    Font& Mono() { return *mono_; }
    Font& Icons() { return *icons_; }

    // Lucide icon lookup by name ("settings", "bell", ...). 0 if unknown.
    uint32_t IconCodepoint(std::string_view name) const;

private:
    FT_LibraryRec_* lib_ = nullptr;
    std::unique_ptr<GlyphAtlas> textAtlas_, iconAtlas_;
    std::unique_ptr<Font> regular_, medium_, bold_, mono_, icons_;
    std::unordered_map<std::string_view, uint32_t> iconMap_;
};

} // namespace silk
