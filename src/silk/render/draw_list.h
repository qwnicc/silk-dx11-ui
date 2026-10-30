#pragma once
// DrawList: CPU-side batch of vertices/indices/commands.
//
// Every primitive is a quad whose pixel shader evaluates an analytic SDF
// (rounded rect, capsule, circle, shadow, glass, colour-picker fields, SDF
// text). Shapes do not need a texture, so they merge into whatever command is
// current; a new command is only started when the clip rect or the bound
// texture changes, or when a backdrop-blur capture is requested.

#include "silk/core/math.h"

#include <string_view>
#include <vector>

struct ID3D11ShaderResourceView;

namespace silk {

class Font;

// Shader primitive types (must match shaders.cpp).
enum class ShapeType : int {
    Textured = 0,   // tex0 * color
    Text = 1,       // SDF glyph from tex0
    RoundRect = 2,  // filled or stroked rounded rectangle
    Shadow = 3,     // blurred rounded rectangle (soft shadow / glow)
    Glass = 4,      // rounded rect sampling the blurred backdrop (t1)
    SatVal = 5,     // HSV saturation/value field (colour picker)
    Hue = 6,        // vertical hue bar
    Checker = 7,    // checkerboard (alpha preview)
    AlphaBar = 8,   // checkerboard + horizontal alpha ramp
};

enum class Gradient : int { None = 0, Radial = 1 };

struct Vertex {
    float x, y;          // position (logical px)
    float u, v;          // texture coords / field coords
    float lx, ly;        // position in shape-local space (px, origin = centre)
    float p0[4];         // half width, half height, corner radius, stroke width
    float p1[4];         // type, softness, gradient mode, gradient param / opacity
    uint32_t col;        // primary colour (RGBA8)
    uint32_t col2;       // secondary colour (gradients)
};

struct DrawCmd {
    enum class Kind : uint8_t { Draw, Backdrop };
    Kind kind = Kind::Draw;
    uint32_t idxOffset = 0;
    uint32_t idxCount = 0;
    Rect clip;
    ID3D11ShaderResourceView* tex = nullptr; // nullptr = "any texture"
};

// Corner colours for linear gradients: top-left, top-right, bottom-right, bottom-left.
struct CornerColors {
    Color tl, tr, br, bl;
    static CornerColors Solid(Color c) { return {c, c, c, c}; }
    static CornerColors Horizontal(Color l, Color r) { return {l, r, r, l}; }
    static CornerColors Vertical(Color t, Color b) { return {t, t, b, b}; }
};

class DrawList {
public:
    void Clear(Rect screen);

    // --- state stacks ---
    void PushClip(Rect r, bool intersect = true);
    void PopClip();
    Rect CurrentClip() const { return clipStack_.back(); }
    void PushAlpha(float a);           // multiplies with the current alpha
    void PopAlpha();
    float CurrentAlpha() const { return alphaStack_.back(); }
    // Scales geometry around `pivot` and then translates (used for open/close
    // animations of popups and windows).
    void PushTransform(Vec2 pivot, float scale, Vec2 offset = {});
    void PopTransform();

    // --- shapes ---
    void RectFilled(Rect r, Color c, float radius = 0.0f);
    void RectGradient(Rect r, const CornerColors& c, float radius = 0.0f);
    void RectStroke(Rect r, Color c, float radius = 0.0f, float thickness = 1.0f);
    void RectStrokeGradient(Rect r, const CornerColors& c, float radius, float thickness = 1.0f);
    // Soft drop shadow / glow around `r`. `blur` is the falloff width in px.
    void Shadow(Rect r, Color c, float radius, float blur, Vec2 offset = {}, float spread = 0.0f);
    void Circle(Vec2 center, float rad, Color c);
    void CircleStroke(Vec2 center, float rad, Color c, float thickness = 1.0f);
    void Line(Vec2 a, Vec2 b, Color c, float thickness = 1.0f); // round caps
    void RadialGradient(Vec2 center, float rad, Color inner, Color outer);
    void RectRadial(Rect r, float radius, Vec2 center, float gradRadius, Color inner, Color outer);

    // Frosted glass: blurs everything drawn *before* this call and fills the
    // rounded rect with it, tinted by `tint` (tint.a = tint strength).
    void Glass(Rect r, float radius, Color tint, float opacity = 1.0f);

    // Colour-picker helpers.
    void SatValField(Rect r, Color hue, float radius);
    void HueBar(Rect r, float radius);
    void Checker(Rect r, float radius, float cell = 6.0f);
    void AlphaBar(Rect r, Color c, float radius, float cell = 6.0f);

    void Image(ID3D11ShaderResourceView* srv, Rect r, Vec2 uv0 = {0, 0}, Vec2 uv1 = {1, 1},
               Color tint = {1, 1, 1, 1});

    // --- text ---
    // `pos` is the top-left of the line box. Returns the advance width.
    float Text(Font& font, float size, Vec2 pos, Color c, std::string_view text);
    // Draws text with its baseline at `baseline`.
    float TextBaseline(Font& font, float size, Vec2 baseline, Color c, std::string_view text);
    // Draws one glyph centred inside `box` (used for icons).
    void GlyphCentered(Font& font, uint32_t codepoint, Rect box, float size, Color c);

    // --- raw access for the renderer ---
    const std::vector<Vertex>& Vertices() const { return vtx_; }
    const std::vector<uint32_t>& Indices() const { return idx_; }
    const std::vector<DrawCmd>& Commands() const { return cmds_; }
    bool Empty() const { return idx_.empty() && cmds_.size() <= 1; }

private:
    struct Transform { Vec2 pivot; float scale = 1.0f; Vec2 offset; };
    struct ShapeParams {
        ShapeType type = ShapeType::RoundRect;
        float radius = 0.0f, stroke = 0.0f, softness = 0.0f;
        Gradient grad = Gradient::None;
        float gradParam = 0.0f;
        Color second;
        float expand = 1.5f;        // quad grows by this (AA / blur margin)
        Vec2 gradCenter;            // radial centre in local space
    };

    DrawCmd& CurrentCmd(ID3D11ShaderResourceView* tex);
    Vec2 Xf(Vec2 p) const;
    Rect XfRect(Rect r) const;
    void ShapeRect(Rect r, const CornerColors& cols, const ShapeParams& sp);
    void ShapeRotated(Vec2 center, Vec2 axis, Vec2 halfSize, Color c, const ShapeParams& sp);
    void PushQuad(const Vec2 pos[4], const Vertex& base, const Vec2 local[4], const Vec2 uv[4],
                  const uint32_t cols[4], ID3D11ShaderResourceView* tex);

    std::vector<Vertex> vtx_;
    std::vector<uint32_t> idx_;
    std::vector<DrawCmd> cmds_;
    std::vector<Rect> clipStack_;
    std::vector<float> alphaStack_;
    std::vector<Transform> xfStack_;
};

} // namespace silk
