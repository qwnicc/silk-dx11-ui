// Basic widgets: text, separators, buttons, checkbox, toggle and layout helpers.

#include "silk/widgets/widgets_internal.h"

#include <vector>

namespace silk::ui {

using namespace detail;

// ---------------------------------------------------------------------------
// IDs / state
// ---------------------------------------------------------------------------

void PushID(std::string_view s) { C().PushID(s); }
void PushID(int i) { C().PushID(C().GetID(i)); }
void PopID() { C().PopID(); }
void BeginDisabled(bool disabled) { C().PushDisabled(disabled); }
void EndDisabled() { C().PopDisabled(); }
void Tooltip(std::string_view text) { C().SetTooltip(std::string(text)); }
bool IsItemHovered() { return C().LastItemHovered(); }

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

namespace {
// Parent extents saved by BeginColumn so EndColumn can report the real
// content height to a parent row (instead of the full allocated height).
std::vector<Vec2> g_columnParentMax;

ID AutoLayoutId() {
    LayoutStack& L = C().Layout();
    const LayoutFrame& f = L.Top();
    return HashInt(uint32_t(f.items), f.id ? f.id : 0x9E3779B9u);
}
} // namespace

void BeginRow(const LayoutStyle& styleIn) {
    LayoutStack& L = C().Layout();
    LayoutStyle style = styleIn;
    style.axis = Axis::Row;
    const ID id = AutoLayoutId();
    // height > 0: fixed, < 0: fill remaining space, 0: content height of the previous frame.
    const float h = style.height > 0.0f   ? style.height
                    : style.height < 0.0f ? 0.0f
                                          : std::max(1.0f, L.Measured(id, {0, T().controlHeight}).y +
                                                               style.padding.top + style.padding.bottom);
    const Rect r = L.Next({0, h});
    L.Push(id, r, style);
}

void EndRow() { C().Layout().Pop(); }

void BeginColumns(int count, float gap, float height) {
    LayoutStyle s;
    s.height = height;
    s.axis = Axis::Row;
    s.columns = std::max(1, count);
    s.gap = gap < 0.0f ? T().spacing * 2.0f : gap;
    s.align = Align::Start;
    BeginRow(s);
}

void EndColumns() { EndRow(); }

void BeginColumn(Vec2 size, const LayoutStyle& styleIn) {
    LayoutStack& L = C().Layout();
    const ID id = AutoLayoutId();
    g_columnParentMax.push_back(L.Top().maxPos);
    const Rect r = L.Next(size);
    LayoutStyle style = styleIn;
    style.axis = Axis::Column;
    L.Push(id, r, style);
}

void EndColumn() {
    LayoutStack& L = C().Layout();
    const LayoutFrame f = L.Pop();
    if (L.Empty() || g_columnParentMax.empty()) return;
    const Vec2 saved = g_columnParentMax.back();
    g_columnParentMax.pop_back();
    // Report actual content extent to the parent.
    LayoutFrame& p = L.Top();
    const float contentBottom = f.maxPos.y + f.style.padding.bottom;
    p.maxPos.y = std::max(saved.y, contentBottom);
}

void Spacing(float amount) { C().Layout().Space(amount < 0.0f ? T().spacing : amount); }
Rect NextRect(Vec2 size) { return C().Layout().Next(size); }
Vec2 AvailableSpace() { return C().Layout().Available(); }

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

namespace {
void TextImpl(FontRole role, float size, float height, Color c, std::string_view text) {
    const Rect r = NextRect({C().TextWidth(role, size, text), height});
    C().TextAt(DL(), role, size, {r.x, r.Center().y}, c, text);
}
} // namespace

void Text(std::string_view text) { TextImpl(FontRole::Regular, T().fontBody, 20, T().text, text); }
void TextDim(std::string_view text) { TextImpl(FontRole::Regular, T().fontBody, 20, T().textDim, text); }
void TextColored(Color c, std::string_view text) { TextImpl(FontRole::Regular, T().fontBody, 20, c, text); }
void Heading(std::string_view text) { TextImpl(FontRole::Medium, T().fontHeading, 24, T().text, text); }
void Title(std::string_view text) { TextImpl(FontRole::Medium, T().fontTitle, 30, T().text, text); }
void Caption(std::string_view text) { TextImpl(FontRole::Medium, T().fontSmall - 0.5f, 16, T().textMuted, text); }

void Icon(std::string_view name, float size, Color color) {
    const Rect r = NextRect({size, size});
    C().IconAt(DL(), name, r, size, color.a > 0.0f ? color : T().text);
}

void Separator(std::string_view label) {
    const Theme& t = T();
    if (label.empty()) {
        const Rect r = NextRect({0, 9});
        DL().RectFilled({r.x, std::floor(r.Center().y), r.w, 1}, t.border);
        return;
    }
    const Rect r = NextRect({0, 24});
    const float tw = C().TextWidth(FontRole::Medium, t.fontSmall, label);
    C().TextAt(DL(), FontRole::Medium, t.fontSmall, {r.x, r.Center().y}, t.textMuted, label);
    const float lx = r.x + tw + 10.0f;
    DL().RectGradient({lx, std::floor(r.Center().y), r.Right() - lx, 1},
                      CornerColors::Horizontal(t.borderStrong, t.border.WithAlpha(0.0f)));
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

namespace {
void ButtonVisual(DrawList& dl, Rect r, ButtonVariant v, float hover, float press, float radius) {
    const Theme& t = T();
    switch (v) {
    case ButtonVariant::Primary:
        dl.Shadow(r, t.accent.WithAlpha(0.18f + 0.32f * hover), radius, 16 + 6 * hover, {0, 4});
        dl.RectGradient(r, AccentGradient(0.08f * hover - 0.06f * press), radius);
        // inner top highlight
        dl.RectStrokeGradient(r, CornerColors::Vertical(Color(1, 1, 1, 0.28f), Color(1, 1, 1, 0.04f)), radius);
        break;
    case ButtonVariant::Secondary:
        dl.RectFilled(r, Mix(t.surface, t.surfaceActive, hover * 0.7f + press * 0.3f), radius);
        dl.RectStroke(r, Mix(t.border, t.borderStrong, hover), radius);
        break;
    case ButtonVariant::Ghost:
        dl.RectFilled(r, t.surfaceHover.MulAlpha(hover + press * 0.4f), radius);
        break;
    case ButtonVariant::Danger:
        dl.RectFilled(r, t.error.WithAlpha(0.12f + 0.10f * hover), radius);
        dl.RectStroke(r, t.error.WithAlpha(0.25f + 0.25f * hover), radius);
        break;
    }
}

Color ButtonTextColor(ButtonVariant v, float hover) {
    const Theme& t = T();
    switch (v) {
    case ButtonVariant::Primary: return t.textOnAccent;
    case ButtonVariant::Danger: return t.error;
    case ButtonVariant::Ghost: return Mix(t.textDim, t.text, hover);
    default: return t.text;
    }
}
} // namespace

bool Button(std::string_view label, ButtonVariant variant, Vec2 size) {
    Context& c = C();
    const Theme& t = T();
    const ID id = c.GetID(label);
    const std::string_view text = DisplayText(label);
    const float w = size.x > 0.0f ? size.x : (size.x < 0.0f ? 0.0f : c.TextWidth(FontRole::Medium, t.fontBody, text) + 32.0f);
    const Rect r = NextRect({w, size.y > 0.0f ? size.y : t.controlHeight});
    const ItemState st = c.ButtonBehavior(r, id);

    const float hover = Hover(id, st.hovered || st.held);
    const float press = Press(id, st.held);
    DrawList& dl = DL();
    dl.PushAlpha(EnabledAlpha(id));
    dl.PushTransform(r.Center(), 1.0f - 0.035f * press);
    ButtonVisual(dl, r, variant, hover, press, t.radius);
    c.TextCentered(dl, FontRole::Medium, t.fontBody, r, ButtonTextColor(variant, hover), text);
    dl.PopTransform();
    dl.PopAlpha();
    return st.clicked;
}

bool IconButton(std::string_view icon, std::string_view idLabel, ButtonVariant variant, float size) {
    Context& c = C();
    const Theme& t = T();
    const ID id = c.GetID(idLabel);
    const Rect r = NextRect({size, size});
    const ItemState st = c.ButtonBehavior(r, id);
    const float hover = Hover(id, st.hovered || st.held);
    const float press = Press(id, st.held);
    DrawList& dl = DL();
    dl.PushAlpha(EnabledAlpha(id));
    dl.PushTransform(r.Center(), 1.0f - 0.06f * press);
    ButtonVisual(dl, r, variant, hover, press, t.radius);
    c.IconAt(dl, icon, r, size * 0.5f, ButtonTextColor(variant, hover));
    dl.PopTransform();
    dl.PopAlpha();
    return st.clicked;
}

// ---------------------------------------------------------------------------
// Checkbox
// ---------------------------------------------------------------------------

bool Checkbox(std::string_view label, bool* value) {
    Context& c = C();
    const Theme& t = T();
    const ID id = c.GetID(label);
    const std::string_view text = DisplayText(label);
    const Rect row = NextRect({0, 28});
    const ItemState st = c.ButtonBehavior(row, id);
    if (st.clicked) *value = !*value;

    const float hover = Hover(id, st.hovered);
    const float press = Press(id, st.held);
    const float on = A().Float(id, kValue, *value ? 1.0f : 0.0f, Motion::Tween(0.24f, Ease::OutCubic));

    DrawList& dl = DL();
    dl.PushAlpha(EnabledAlpha(id));
    const Rect box{row.x, std::floor(row.Center().y - 9.0f), 18, 18};
    dl.PushTransform(box.Center(), 1.0f - 0.12f * press);
    dl.Shadow(box, t.accent.WithAlpha((0.25f + 0.25f * hover) * on), 5, 12);
    dl.RectFilled(box, Mix(t.surface, t.surfaceHover, hover), 5);
    if (on > 0.0f) {
        const CornerColors g = AccentGradient(0.06f * hover);
        dl.RectGradient(box, {g.tl.MulAlpha(on), g.tr.MulAlpha(on), g.br.MulAlpha(on), g.bl.MulAlpha(on)}, 5);
    }
    dl.RectStroke(box, Mix(Mix(t.borderStrong, t.textMuted, hover * 0.5f), t.accent.WithAlpha(0.0f), on), 5);

    // Animated checkmark: the path is "drawn" progressively as `on` grows.
    if (on > 0.01f) {
        const Vec2 p0 = box.Pos() + Vec2(4.6f, 9.4f), p1 = box.Pos() + Vec2(7.6f, 12.4f),
                   p2 = box.Pos() + Vec2(13.4f, 5.8f);
        const float l1 = (p1 - p0).Length(), l2 = (p2 - p1).Length();
        const float len = on * (l1 + l2);
        const Color ck = t.textOnAccent.MulAlpha(Saturate(on * 3.0f));
        dl.Line(p0, p0 + (p1 - p0) * Saturate(len / l1), ck, 2.0f);
        if (len > l1) dl.Line(p1, p1 + (p2 - p1) * Saturate((len - l1) / l2), ck, 2.0f);
    }
    dl.PopTransform();
    c.TextAt(dl, FontRole::Regular, t.fontBody, {box.Right() + 10.0f, row.Center().y}, Mix(t.textDim, t.text, 0.6f + 0.4f * hover), text);
    dl.PopAlpha();
    return st.clicked;
}

// ---------------------------------------------------------------------------
// Toggle switch (settings row: label left, switch right)
// ---------------------------------------------------------------------------

bool Toggle(std::string_view label, bool* value) {
    Context& c = C();
    const Theme& t = T();
    const ID id = c.GetID(label);
    const Rect row = NextRect({0, t.rowHeight});
    const ItemState st = c.ButtonBehavior(row, id);
    if (st.clicked) *value = !*value;

    const float hover = Hover(id, st.hovered);
    const float press = Press(id, st.held);
    const float on = A().Float(id, kValue, *value ? 1.0f : 0.0f, Motion::Springy({380.0f, 24.0f, 1.0f}));
    const float onC = Saturate(on);

    DrawList& dl = DL();
    dl.PushAlpha(EnabledAlpha(id));
    RowLabel(row, label, Mix(t.textDim, t.text, 0.6f + 0.4f * hover));

    const Rect track{row.Right() - 36.0f, std::floor(row.Center().y - 10.0f), 36, 20};
    dl.Shadow(track, t.accent.WithAlpha(0.35f * onC), 10, 14);
    dl.RectFilled(track, Mix(t.surfaceActive, t.borderStrong, hover * 0.5f), 10);
    if (onC > 0.0f) {
        const CornerColors g = AccentGradient(0.06f * hover);
        dl.RectGradient(track, {g.tl.MulAlpha(onC), g.tr.MulAlpha(onC), g.br.MulAlpha(onC), g.bl.MulAlpha(onC)}, 10);
    }
    dl.RectStroke(track, t.border, 10);

    // Knob stretches while pressed (iOS-like) and travels with the spring.
    const float stretch = 4.0f * press;
    const float kw = 16.0f + stretch;
    const float travel = track.w - 4.0f - kw;
    const float kx = track.x + 2.0f + travel * on;
    const Rect knob{kx, track.y + 2.0f, kw, 16.0f};
    dl.Shadow(knob, Color(0, 0, 0, 0.35f), 8, 6, {0, 1});
    dl.RectFilled(knob, Color(1, 1, 1, 1), 8);
    dl.PopAlpha();
    return st.clicked;
}

} // namespace silk::ui
