#include "silk/render/draw_list.h"

#include "silk/core/utf8.h"
#include "silk/render/font.h"

namespace silk {

void DrawList::Clear(Rect screen) {
    vtx_.clear();
    idx_.clear();
    cmds_.clear();
    clipStack_.assign(1, screen);
    alphaStack_.assign(1, 1.0f);
    xfStack_.assign(1, Transform{{0, 0}, 1.0f, {0, 0}});
    cmds_.push_back(DrawCmd{DrawCmd::Kind::Draw, 0, 0, screen, nullptr});
}

// ---------------------------------------------------------------------------
// State stacks
// ---------------------------------------------------------------------------

void DrawList::PushClip(Rect r, bool intersect) {
    Rect xr = XfRect(r);
    if (intersect) xr = xr.Intersect(clipStack_.back());
    clipStack_.push_back(xr);
}

void DrawList::PopClip() {
    if (clipStack_.size() > 1) clipStack_.pop_back();
}

void DrawList::PushAlpha(float a) { alphaStack_.push_back(alphaStack_.back() * Saturate(a)); }

void DrawList::PopAlpha() {
    if (alphaStack_.size() > 1) alphaStack_.pop_back();
}

// Transforms are stored pre-composed as p' = p * scale + offset.
void DrawList::PushTransform(Vec2 pivot, float scale, Vec2 offset) {
    const Transform& top = xfStack_.back();
    const Vec2 localOffset = pivot * (1.0f - scale) + offset;
    xfStack_.push_back(Transform{{}, top.scale * scale, localOffset * top.scale + top.offset});
}

void DrawList::PopTransform() {
    if (xfStack_.size() > 1) xfStack_.pop_back();
}

Vec2 DrawList::Xf(Vec2 p) const {
    const Transform& t = xfStack_.back();
    return p * t.scale + t.offset;
}

Rect DrawList::XfRect(Rect r) const {
    const Transform& t = xfStack_.back();
    return Rect(Xf(r.Pos()), r.Size() * t.scale);
}

// ---------------------------------------------------------------------------
// Low level emission
// ---------------------------------------------------------------------------

DrawCmd& DrawList::CurrentCmd(ID3D11ShaderResourceView* tex) {
    const Rect& clip = clipStack_.back();
    DrawCmd& c = cmds_.back();
    if (c.kind == DrawCmd::Kind::Draw) {
        if (c.idxCount == 0) {
            c.clip = clip;
            c.tex = tex;
            return c;
        }
        if (c.clip == clip && (tex == nullptr || c.tex == nullptr || c.tex == tex)) {
            if (tex) c.tex = tex;
            return c;
        }
    }
    cmds_.push_back(DrawCmd{DrawCmd::Kind::Draw, uint32_t(idx_.size()), 0, clip, tex});
    return cmds_.back();
}

void DrawList::PushQuad(const Vec2 pos[4], const Vertex& base, const Vec2 local[4], const Vec2 uv[4],
                        const uint32_t cols[4], ID3D11ShaderResourceView* tex) {
    // Cheap reject of geometry fully outside the clip rect.
    const Rect& clip = clipStack_.back();
    float x0 = pos[0].x, x1 = pos[0].x, y0 = pos[0].y, y1 = pos[0].y;
    for (int i = 1; i < 4; ++i) {
        x0 = std::min(x0, pos[i].x); x1 = std::max(x1, pos[i].x);
        y0 = std::min(y0, pos[i].y); y1 = std::max(y1, pos[i].y);
    }
    if (clip.w <= 0 || clip.h <= 0 || !Rect::FromMinMax({x0, y0}, {x1, y1}).Overlaps(clip)) return;

    DrawCmd& cmd = CurrentCmd(tex);
    const auto first = uint32_t(vtx_.size());
    for (int i = 0; i < 4; ++i) {
        Vertex v = base;
        v.x = pos[i].x;
        v.y = pos[i].y;
        v.lx = local[i].x;
        v.ly = local[i].y;
        v.u = uv[i].x;
        v.v = uv[i].y;
        v.col = cols[i];
        vtx_.push_back(v);
    }
    const uint32_t q[6] = {first, first + 1, first + 2, first, first + 2, first + 3};
    idx_.insert(idx_.end(), q, q + 6);
    cmd.idxCount += 6;
}

void DrawList::ShapeRect(Rect r, const CornerColors& cols, const ShapeParams& sp) {
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    const float a = alphaStack_.back();
    const Vec2 c = r.Center();
    const float hx = r.w * 0.5f, hy = r.h * 0.5f;
    const float e = sp.expand;
    const Vec2 local[4] = {{-hx - e, -hy - e}, {hx + e, -hy - e}, {hx + e, hy + e}, {-hx - e, hy + e}};
    Vec2 pos[4], uv[4];
    for (int i = 0; i < 4; ++i) {
        pos[i] = Xf(c + local[i]);
        if (sp.grad == Gradient::Radial)
            uv[i] = local[i] - sp.gradCenter;               // radial: distance from gradient centre
        else
            uv[i] = {(local[i].x + hx) / r.w, (local[i].y + hy) / r.h}; // 0..1 across the rect
    }
    const uint32_t pc[4] = {cols.tl.MulAlpha(a).Pack(), cols.tr.MulAlpha(a).Pack(),
                            cols.br.MulAlpha(a).Pack(), cols.bl.MulAlpha(a).Pack()};
    Vertex base{};
    base.p0[0] = hx;
    base.p0[1] = hy;
    base.p0[2] = std::min(sp.radius, std::min(hx, hy));
    base.p0[3] = sp.stroke;
    base.p1[0] = float(int(sp.type));
    base.p1[1] = sp.softness;
    base.p1[2] = float(int(sp.grad));
    base.p1[3] = sp.type == ShapeType::Glass ? sp.gradParam * a : sp.gradParam;
    base.col2 = sp.second.MulAlpha(a).Pack();
    PushQuad(pos, base, local, uv, pc, nullptr);
}

void DrawList::ShapeRotated(Vec2 center, Vec2 axis, Vec2 half, Color c, const ShapeParams& sp) {
    const float a = alphaStack_.back();
    const Vec2 n{-axis.y, axis.x};
    const float e = sp.expand;
    const Vec2 local[4] = {{-half.x - e, -half.y - e}, {half.x + e, -half.y - e},
                           {half.x + e, half.y + e}, {-half.x - e, half.y + e}};
    Vec2 pos[4];
    const Vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i) pos[i] = Xf(center + axis * local[i].x + n * local[i].y);
    const uint32_t col = c.MulAlpha(a).Pack();
    const uint32_t pc[4] = {col, col, col, col};
    Vertex base{};
    base.p0[0] = half.x;
    base.p0[1] = half.y;
    base.p0[2] = std::min(sp.radius, std::min(half.x, half.y));
    base.p0[3] = sp.stroke;
    base.p1[0] = float(int(sp.type));
    base.p1[1] = sp.softness;
    base.col2 = col;
    PushQuad(pos, base, local, uv, pc, nullptr);
}

// ---------------------------------------------------------------------------
// Shapes
// ---------------------------------------------------------------------------

void DrawList::RectFilled(Rect r, Color c, float radius) {
    if (c.a <= 0.0f) return;
    ShapeParams sp;
    sp.radius = radius;
    ShapeRect(r, CornerColors::Solid(c), sp);
}

void DrawList::RectGradient(Rect r, const CornerColors& c, float radius) {
    ShapeParams sp;
    sp.radius = radius;
    ShapeRect(r, c, sp);
}

void DrawList::RectStroke(Rect r, Color c, float radius, float thickness) {
    if (c.a <= 0.0f) return;
    RectStrokeGradient(r, CornerColors::Solid(c), radius, thickness);
}

void DrawList::RectStrokeGradient(Rect r, const CornerColors& c, float radius, float thickness) {
    ShapeParams sp;
    sp.radius = radius;
    sp.stroke = thickness;
    ShapeRect(r, c, sp);
}

void DrawList::Shadow(Rect r, Color c, float radius, float blur, Vec2 offset, float spread) {
    if (c.a <= 0.0f || blur <= 0.0f) return;
    ShapeParams sp;
    sp.type = ShapeType::Shadow;
    sp.radius = radius + spread;
    sp.softness = blur;
    sp.expand = blur * 1.5f + 2.0f;
    ShapeRect(r.Translate(offset).Expand(spread), CornerColors::Solid(c), sp);
}

void DrawList::Circle(Vec2 center, float rad, Color c) {
    RectFilled(Rect(center - Vec2(rad, rad), Vec2(rad, rad) * 2.0f), c, rad);
}

void DrawList::CircleStroke(Vec2 center, float rad, Color c, float thickness) {
    RectStroke(Rect(center - Vec2(rad, rad), Vec2(rad, rad) * 2.0f), c, rad, thickness);
}

void DrawList::Line(Vec2 a, Vec2 b, Color c, float thickness) {
    const Vec2 d = b - a;
    const float len = d.Length();
    if (len < 1e-4f || c.a <= 0.0f) return;
    const Vec2 axis = d / len;
    const float ht = thickness * 0.5f;
    ShapeParams sp;
    sp.radius = ht;
    ShapeRotated((a + b) * 0.5f, axis, {len * 0.5f + ht, ht}, c, sp);
}

void DrawList::RadialGradient(Vec2 center, float rad, Color inner, Color outer) {
    RectRadial(Rect(center - Vec2(rad, rad), Vec2(rad, rad) * 2.0f), rad, center, rad, inner, outer);
}

void DrawList::RectRadial(Rect r, float radius, Vec2 center, float gradRadius, Color inner, Color outer) {
    ShapeParams sp;
    sp.radius = radius;
    sp.grad = Gradient::Radial;
    sp.gradParam = gradRadius;
    sp.second = outer;
    sp.gradCenter = center - r.Center();
    ShapeRect(r, CornerColors::Solid(inner), sp);
}

void DrawList::Glass(Rect r, float radius, Color tint, float opacity) {
    // Marker: the renderer flushes everything recorded so far, blurs the
    // render target and binds the result to t1 for the glass shapes below.
    cmds_.push_back(DrawCmd{DrawCmd::Kind::Backdrop, uint32_t(idx_.size()), 0, clipStack_.back(), nullptr});
    ShapeParams sp;
    sp.type = ShapeType::Glass;
    sp.radius = radius;
    sp.gradParam = opacity; // multiplied by the alpha stack inside ShapeRect
    ShapeRect(r, CornerColors::Solid(tint), sp);
}

void DrawList::SatValField(Rect r, Color hue, float radius) {
    ShapeParams sp;
    sp.type = ShapeType::SatVal;
    sp.radius = radius;
    ShapeRect(r, CornerColors::Solid(hue), sp);
}

void DrawList::HueBar(Rect r, float radius) {
    ShapeParams sp;
    sp.type = ShapeType::Hue;
    sp.radius = radius;
    ShapeRect(r, CornerColors::Solid({1, 1, 1, 1}), sp);
}

void DrawList::Checker(Rect r, float radius, float cell) {
    ShapeParams sp;
    sp.type = ShapeType::Checker;
    sp.radius = radius;
    sp.gradParam = cell;
    ShapeRect(r, CornerColors::Solid({1, 1, 1, 1}), sp);
}

void DrawList::AlphaBar(Rect r, Color c, float radius, float cell) {
    ShapeParams sp;
    sp.type = ShapeType::AlphaBar;
    sp.radius = radius;
    sp.gradParam = cell;
    ShapeRect(r, CornerColors::Solid(c.WithAlpha(1.0f)), sp);
}

void DrawList::Image(ID3D11ShaderResourceView* srv, Rect r, Vec2 uv0, Vec2 uv1, Color tint) {
    if (!srv) return;
    const Vec2 pos[4] = {Xf(r.Min()), Xf({r.Right(), r.y}), Xf(r.Max()), Xf({r.x, r.Bottom()})};
    const Vec2 uv[4] = {uv0, {uv1.x, uv0.y}, uv1, {uv0.x, uv1.y}};
    const Vec2 local[4] = {};
    const uint32_t c = tint.MulAlpha(alphaStack_.back()).Pack();
    const uint32_t cols[4] = {c, c, c, c};
    Vertex base{};
    base.p1[0] = float(int(ShapeType::Textured));
    PushQuad(pos, base, local, uv, cols, srv);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

float DrawList::Text(Font& font, float size, Vec2 pos, Color c, std::string_view text) {
    return TextBaseline(font, size, {pos.x, pos.y + font.Ascent(size)}, c, text);
}

float DrawList::TextBaseline(Font& font, float size, Vec2 baseline, Color c, std::string_view text) {
    const float s = font.Scale(size);
    const float a = alphaStack_.back();
    const uint32_t col = c.MulAlpha(a).Pack();
    const uint32_t cols[4] = {col, col, col, col};
    GlyphAtlas* atlas = font.Atlas();

    Vertex base{};
    // unitRange: distance-field range (2*spread) in UV units, per axis.
    base.p0[0] = 2.0f * font.SdfSpread() / float(atlas->Width());
    base.p0[2] = 2.0f * font.SdfSpread() / float(atlas->Height());
    // Slight dilation for small sizes compensates for the thinning that pure
    // coverage AA produces on dark backgrounds.
    base.p0[1] = size < 15.0f ? 0.12f : 0.04f;
    base.p1[0] = float(int(ShapeType::Text));

    float pen = baseline.x;
    const float by = std::round(baseline.y);
    const Vec2 local[4] = {};
    uint32_t prev = 0;
    for (size_t i = 0; i < text.size();) {
        const uint32_t cp = utf8::Decode(text, i);
        pen += font.Kerning(prev, cp) * s;
        const Glyph& g = font.GetGlyph(cp);
        if (g.visible && c.a > 0.0f) {
            const float x0 = pen + g.x0 * s, x1 = pen + g.x1 * s;
            const float y0 = by + g.y0 * s, y1 = by + g.y1 * s;
            const Vec2 pos[4] = {Xf({x0, y0}), Xf({x1, y0}), Xf({x1, y1}), Xf({x0, y1})};
            const Vec2 uv[4] = {{g.u0, g.v0}, {g.u1, g.v0}, {g.u1, g.v1}, {g.u0, g.v1}};
            PushQuad(pos, base, local, uv, cols, atlas->Srv());
        }
        pen += g.advance * s;
        prev = cp;
    }
    return pen - baseline.x;
}

void DrawList::GlyphCentered(Font& font, uint32_t codepoint, Rect box, float size, Color c) {
    if (!codepoint) return;
    const Glyph& g = font.GetGlyph(codepoint);
    const float s = font.Scale(size);
    const Vec2 center = box.Center();
    // Centre the glyph's em box (icon fonts are designed on a square em grid).
    const float penX = center.x - g.advance * s * 0.5f;
    const float baseline = center.y + (font.Ascent(size) - font.Descent(size)) * 0.5f;
    std::string utf;
    utf8::Append(utf, codepoint);
    TextBaseline(font, size, {penX, baseline}, c, utf);
}

} // namespace silk
