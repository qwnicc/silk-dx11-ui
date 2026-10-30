#include "silk/render/font.h"

#include "silk/core/utf8.h"
#include "silk/render/lucide_icons.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_TRUETYPE_TABLES_H

#include <d3d11.h>
#include <windows.h>

#include <cstring>
#include <filesystem>

namespace silk {

// ---------------------------------------------------------------------------
// GlyphAtlas
// ---------------------------------------------------------------------------

GlyphAtlas::GlyphAtlas(int width, int height)
    : width_(width), height_(height), pixels_(size_t(width) * size_t(height), 0) {}

GlyphAtlas::~GlyphAtlas() {
    if (srv_) srv_->Release();
    if (tex_) tex_->Release();
}

bool GlyphAtlas::CreateTexture(ID3D11Device* device) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(width_);
    td.Height = UINT(height_);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{pixels_.data(), UINT(width_), 0};
    if (FAILED(device->CreateTexture2D(&td, &init, &tex_))) return false;
    return SUCCEEDED(device->CreateShaderResourceView(tex_, nullptr, &srv_));
}

bool GlyphAtlas::Pack(int w, int h, int& outX, int& outY) {
    constexpr int kPad = 1; // keeps bilinear filtering from bleeding between glyphs
    if (shelfX_ + w + kPad > width_) {
        shelfY_ += shelfH_ + kPad;
        shelfX_ = 1;
        shelfH_ = 0;
    }
    if (shelfY_ + h + kPad > height_ || w + 2 > width_) return false;
    outX = shelfX_;
    outY = shelfY_;
    shelfX_ += w + kPad;
    shelfH_ = std::max(shelfH_, h);
    return true;
}

void GlyphAtlas::Blit(int x, int y, int w, int h, const uint8_t* src, int srcPitch) {
    for (int row = 0; row < h; ++row)
        std::memcpy(&pixels_[size_t(y + row) * size_t(width_) + size_t(x)], src + ptrdiff_t(row) * srcPitch, size_t(w));
    if (dirtyY0_ == dirtyY1_) { dirtyY0_ = y; dirtyY1_ = y + h; }
    else { dirtyY0_ = std::min(dirtyY0_, y); dirtyY1_ = std::max(dirtyY1_, y + h); }
}

void GlyphAtlas::Upload(ID3D11DeviceContext* ctx) {
    if (!tex_ || dirtyY0_ == dirtyY1_) return;
    D3D11_BOX box{0, UINT(dirtyY0_), 0, UINT(width_), UINT(dirtyY1_), 1};
    ctx->UpdateSubresource(tex_, 0, &box, &pixels_[size_t(dirtyY0_) * size_t(width_)], UINT(width_), 0);
    dirtyY0_ = dirtyY1_ = 0;
}

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------

Font::~Font() {
    if (face_) FT_Done_Face(face_);
}

bool Font::Load(FT_LibraryRec_* lib, const std::string& path, GlyphAtlas* atlas, float baseSize, int spread) {
    if (FT_New_Face(lib, path.c_str(), 0, &face_) != 0) {
        face_ = nullptr;
        return false;
    }
    atlas_ = atlas;
    baseSize_ = baseSize;
    spread_ = spread;
    FT_Set_Pixel_Sizes(face_, 0, FT_UInt(baseSize));

    const float upem = float(face_->units_per_EM);
    ascent_ = float(face_->ascender) / upem;
    descent_ = float(-face_->descender) / upem;
    lineGap_ = std::max(0.0f, float(face_->height) / upem - ascent_ - descent_);
    if (auto* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face_, FT_SFNT_OS2)); os2 && os2->sCapHeight > 0)
        capHeight_ = float(os2->sCapHeight) / upem;
    else
        capHeight_ = ascent_ * 0.72f;
    hasKerning_ = FT_HAS_KERNING(face_);
    return true;
}

bool Font::HasGlyph(uint32_t codepoint) const {
    return face_ && FT_Get_Char_Index(face_, codepoint) != 0;
}

const Glyph& Font::GetGlyph(uint32_t cp) {
    if (auto it = glyphs_.find(cp); it != glyphs_.end()) return it->second;

    Glyph g;
    const FT_UInt gi = FT_Get_Char_Index(face_, cp);
    if (FT_Load_Glyph(face_, gi, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) == 0) {
        FT_GlyphSlot slot = face_->glyph;
        g.advance = float(slot->advance.x) / 64.0f;
        const bool empty = slot->outline.n_points == 0;
        if (!empty && FT_Render_Glyph(slot, FT_RENDER_MODE_SDF) == 0) {
            const FT_Bitmap& bm = slot->bitmap;
            const int w = int(bm.width), h = int(bm.rows);
            int ax = 0, ay = 0;
            if (w > 0 && h > 0 && atlas_->Pack(w, h, ax, ay)) {
                atlas_->Blit(ax, ay, w, h, bm.buffer, bm.pitch);
                g.visible = true;
                g.x0 = float(slot->bitmap_left);
                g.y0 = float(-slot->bitmap_top);
                g.x1 = g.x0 + float(w);
                g.y1 = g.y0 + float(h);
                const float iw = 1.0f / float(atlas_->Width()), ih = 1.0f / float(atlas_->Height());
                g.u0 = float(ax) * iw;
                g.v0 = float(ay) * ih;
                g.u1 = float(ax + w) * iw;
                g.v1 = float(ay + h) * ih;
            }
        }
    }
    return glyphs_.emplace(cp, g).first->second;
}

float Font::Kerning(uint32_t left, uint32_t right) {
    if (!hasKerning_ || !left) return 0.0f;
    const uint64_t key = (uint64_t(left) << 32) | right;
    if (auto it = kerningCache_.find(key); it != kerningCache_.end()) return it->second;
    FT_Vector k{};
    FT_Get_Kerning(face_, FT_Get_Char_Index(face_, left), FT_Get_Char_Index(face_, right), FT_KERNING_UNFITTED, &k);
    const float v = float(k.x) / 64.0f;
    kerningCache_.emplace(key, v);
    return v;
}

Vec2 Font::Measure(std::string_view text, float size) {
    const float s = Scale(size);
    float w = 0.0f;
    uint32_t prev = 0;
    for (size_t i = 0; i < text.size();) {
        const uint32_t cp = utf8::Decode(text, i);
        w += (Kerning(prev, cp) + GetGlyph(cp).advance) * s;
        prev = cp;
    }
    return {w, LineHeight(size)};
}

size_t Font::HitTest(std::string_view text, float size, float x) {
    const float s = Scale(size);
    float pen = 0.0f;
    uint32_t prev = 0;
    for (size_t i = 0; i < text.size();) {
        const size_t start = i;
        const uint32_t cp = utf8::Decode(text, i);
        const float adv = (Kerning(prev, cp) + GetGlyph(cp).advance) * s;
        if (x < pen + adv * 0.5f) return start;
        pen += adv;
        prev = cp;
    }
    return text.size();
}

float Font::OffsetOf(std::string_view text, float size, size_t byte) {
    return Measure(text.substr(0, std::min(byte, text.size())), size).x;
}

// ---------------------------------------------------------------------------
// FontSystem
// ---------------------------------------------------------------------------

FontSystem::FontSystem() = default;

FontSystem::~FontSystem() {
    // Faces must be released before the library.
    regular_.reset(); medium_.reset(); bold_.reset(); mono_.reset(); icons_.reset();
    if (lib_) FT_Done_FreeType(lib_);
}

namespace {
std::string WindowsFontPath(const char* file) {
    char dir[MAX_PATH]{};
    GetWindowsDirectoryA(dir, MAX_PATH);
    return std::string(dir) + "\\Fonts\\" + file;
}

// First existing path from a fallback list.
std::string FirstExisting(std::initializer_list<std::string> paths) {
    for (const auto& p : paths)
        if (std::filesystem::exists(p)) return p;
    return {};
}
} // namespace

bool FontSystem::Init(ID3D11Device* device, const std::string& assetDir) {
    if (FT_Init_FreeType(&lib_) != 0) return false;

    // Distance-field spread in base-size pixels. Larger = more room for glow
    // effects, smaller = more precision per texel.
    constexpr int kSpread = 6;
    FT_Int spread = kSpread;
    FT_Property_Set(lib_, "sdf", "spread", &spread);

    textAtlas_ = std::make_unique<GlyphAtlas>(2048, 2048);
    iconAtlas_ = std::make_unique<GlyphAtlas>(1024, 1024);
    if (!textAtlas_->CreateTexture(device) || !iconAtlas_->CreateTexture(device)) return false;

    // Text is rasterised at 40px: crisp from ~10px up to large titles.
    constexpr float kTextBase = 40.0f;
    auto load = [&](std::unique_ptr<Font>& f, std::initializer_list<std::string> candidates,
                    GlyphAtlas* atlas, float base) {
        f = std::make_unique<Font>();
        const std::string path = FirstExisting(candidates);
        return !path.empty() && f->Load(lib_, path, atlas, base, kSpread);
    };

    const std::string fontDir = assetDir + "/fonts/";
    bool ok = true;
    ok &= load(regular_, {fontDir + "Inter-Regular.ttf", WindowsFontPath("segoeui.ttf"), WindowsFontPath("arial.ttf")},
               textAtlas_.get(), kTextBase);
    ok &= load(medium_, {fontDir + "Inter-SemiBold.ttf", WindowsFontPath("seguisb.ttf"), WindowsFontPath("segoeui.ttf")},
               textAtlas_.get(), kTextBase);
    ok &= load(bold_, {fontDir + "Inter-Bold.ttf", WindowsFontPath("segoeuib.ttf"), WindowsFontPath("arialbd.ttf")},
               textAtlas_.get(), kTextBase);
    ok &= load(mono_, {fontDir + "JetBrainsMono-Regular.ttf", WindowsFontPath("consola.ttf"), WindowsFontPath("cour.ttf")},
               textAtlas_.get(), kTextBase);
    ok &= load(icons_, {fontDir + "lucide.ttf"}, iconAtlas_.get(), 40.0f);
    if (!ok) return false;

    for (const auto& e : icons::kLucide) iconMap_.emplace(e.name, e.codepoint);

    // Pre-warm printable ASCII + Polish diacritics so the first frame has no hitches.
    static constexpr char32_t kExtra[] = U"ąćęłńóśźżĄĆĘŁŃÓŚŹŻ•…–—°·";
    for (Font* f : {regular_.get(), medium_.get(), bold_.get(), mono_.get()}) {
        for (uint32_t c = 32; c < 127; ++c) f->GetGlyph(c);
        for (char32_t c : kExtra) if (c) f->GetGlyph(uint32_t(c));
    }
    return true;
}

void FontSystem::Upload(ID3D11DeviceContext* ctx) {
    textAtlas_->Upload(ctx);
    iconAtlas_->Upload(ctx);
}

uint32_t FontSystem::IconCodepoint(std::string_view name) const {
    const auto it = iconMap_.find(name);
    return it == iconMap_.end() ? 0u : it->second;
}

} // namespace silk
