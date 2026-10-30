// Popup infrastructure, Slider, Combo and MultiSelect.

#include "silk/widgets/widgets_internal.h"

#include <cstdio>
#include <string>
#include <vector>

namespace silk::ui {

using namespace detail;

// ---------------------------------------------------------------------------
// Popups
// ---------------------------------------------------------------------------

namespace detail {

void PopupPanel(DrawList& dl, Rect r) {
    const Theme& t = T();
    dl.Shadow(r, t.shadow, t.radiusLg, 28, {0, 12});
    if (t.glass) dl.Glass(r, t.radiusLg, t.popup.WithAlpha(0.88f));
    else dl.RectFilled(r, t.popup, t.radiusLg);
    dl.RectStroke(r, t.borderStrong, t.radiusLg);
}

bool BeginPopupLayer(ID id, Rect anchor, Vec2 size, Rect& outRect) {
    Context& c = C();
    PopupState& p = c.Popup(id);
    p.t = A().Float(id, kOpen, p.open ? 1.0f : 0.0f,
                    p.open ? Motion::Tween(0.20f, Ease::OutExpo) : Motion::Tween(0.12f, Ease::OutCubic));
    if (!p.open && p.t <= 0.002f) return false;

    const Rect screen = c.Screen();
    Rect r{anchor.x, anchor.Bottom() + 6.0f, size.x, size.y};
    bool above = false;
    if (r.Bottom() > screen.Bottom() - 8.0f && anchor.y - 6.0f - size.y > 8.0f) {
        r.y = anchor.y - 6.0f - size.y;
        above = true;
    }
    r.x = Clamp(r.x, 8.0f, std::max(8.0f, screen.Right() - size.x - 8.0f));

    Layer& l = c.BeginLayer(id, LayerKind::Popup);
    l.order = int(p.openSeq);
    l.interactive = p.open;   // closing popups are visible but inert
    l.rect = p.open ? r : Rect{};
    DrawList& dl = l.dl;
    // Open: scale 0.95 -> 1 and fade in, growing out of the anchor edge.
    const float s = 0.95f + 0.05f * p.t;
    const Vec2 pivot{r.Center().x, above ? r.Bottom() : r.y};
    dl.PushAlpha(p.t);
    dl.PushTransform(pivot, s, {0.0f, (1.0f - p.t) * (above ? 4.0f : -4.0f)});
    PopupPanel(dl, r);
    dl.PushClip(r);
    LayoutStyle ls;
    ls.gap = 0.0f;
    ls.padding = Padding(4.0f);
    c.Layout().Push(id, r, ls);
    outRect = r;
    return true;
}

void EndPopupLayer() {
    Context& c = C();
    c.Layout().Pop();
    DrawList& dl = c.DL();
    dl.PopClip();
    dl.PopTransform();
    dl.PopAlpha();
    c.EndLayer();
}

} // namespace detail

namespace {

// Shortens `text` with an ellipsis so it fits into `maxW`.
std::string Ellipsize(FontRole role, float size, std::string_view text, float maxW) {
    Context& c = C();
    if (c.TextWidth(role, size, text) <= maxW) return std::string(text);
    std::string s(text);
    while (!s.empty()) {
        s.pop_back();
        while (!s.empty() && (static_cast<uint8_t>(s.back()) & 0xC0) == 0x80) s.pop_back();
        if (c.TextWidth(role, size, s + "…") <= maxW) return s + "…";
    }
    return "…";
}

// The clickable field used by Combo / MultiSelect / Keybind.
void FieldVisual(DrawList& dl, Rect r, float hover, float open) {
    const Theme& t = T();
    dl.RectFilled(r, Mix(t.surface, t.surfaceHover, std::max(hover, open)), t.radius);
    dl.RectStroke(r, Mix(Mix(t.border, t.borderStrong, hover), t.accent.WithAlpha(0.7f), open), t.radius);
    FocusRing(dl, r, t.radius, open * 0.6f);
}

// A dropdown field on the right side of a settings row. Returns its rect.
Rect DropdownField(std::string_view label, ID id, ID popupId, std::string_view value, bool& toggled) {
    Context& c = C();
    const Theme& t = T();
    const Rect row = NextRect({0, t.rowHeight});
    const float fw = std::min(220.0f, std::max(140.0f, row.w * 0.5f));
    const Rect field{row.Right() - fw, row.Center().y - t.controlHeight * 0.5f, fw, t.controlHeight};
    const ItemState st = c.ButtonBehavior(field, id);
    PopupState& p = c.Popup(popupId);
    toggled = false;
    // Open on press (not release) so an outside-click close in the same frame
    // can be distinguished from a re-open.
    if (st.pressed) {
        if (p.open) c.ClosePopup(popupId);
        else if (!c.PopupJustClosed(popupId)) c.OpenPopup(popupId);
        toggled = true;
    }
    const float hover = Hover(id, st.hovered);
    const float open = A().Float(id, kChevron, p.open ? 1.0f : 0.0f, motion::kHover);
    DrawList& dl = DL();
    dl.PushAlpha(EnabledAlpha(id));
    RowLabel(row, label, t.textDim);
    FieldVisual(dl, field, hover, open);
    const std::string shown = Ellipsize(FontRole::Regular, t.fontBody, value, field.w - 44.0f);
    c.TextAt(dl, FontRole::Regular, t.fontBody, {field.x + 12.0f, field.Center().y}, t.text, shown);
    c.IconAt(dl, "chevrons-up-down", {field.Right() - 30.0f, field.y, 24.0f, field.h}, 14.0f,
             Mix(t.textMuted, t.text, std::max(hover, open)));
    dl.PopAlpha();
    return field;
}

// Popup list row with a sliding hover highlight shared by the whole list.
bool PopupItem(ID listId, int index, std::string_view text, bool selected, bool checkbox, float& highlightTarget,
               bool& anyHovered) {
    Context& c = C();
    const Theme& t = T();
    const ID id = HashInt(uint32_t(index), listId);
    const Rect r = NextRect({0, 30});
    const ItemState st = c.ButtonBehavior(r, id);
    if (st.hovered) {
        highlightTarget = r.y;
        anyHovered = true;
    }
    const float hover = Hover(id, st.hovered);
    DrawList& dl = DL();
    float x = r.x + 10.0f;
    if (checkbox) {
        const float on = A().Float(id, kValue, selected ? 1.0f : 0.0f, motion::kHover);
        const Rect box{x, r.Center().y - 7.0f, 14, 14};
        dl.RectStroke(box, Mix(t.borderStrong, t.accent.WithAlpha(0.0f), on), 4);
        if (on > 0.0f) {
            dl.RectFilled(box, t.accent.MulAlpha(on), 4);
            c.IconAt(dl, "check", box, 11.0f, t.textOnAccent.MulAlpha(on));
        }
        x += 24.0f;
    }
    c.TextAt(dl, FontRole::Regular, t.fontBody, {x, r.Center().y}, Mix(t.textDim, t.text, std::max(hover, selected ? 1.0f : 0.0f)), text);
    if (selected && !checkbox) c.IconAt(dl, "check", {r.Right() - 30.0f, r.y, 24.0f, r.h}, 14.0f, t.accent);
    return st.clicked;
}

void DrawHighlight(ID listId, Rect popup, float target, bool any) {
    const Theme& t = T();
    const float y = A().Float(listId, kHighlightY, target, Motion::Springy({600.0f, 40.0f, 1.0f}));
    const float a = A().Float(listId, kHighlight, any ? 1.0f : 0.0f, motion::kHover);
    if (a > 0.01f) DL().RectFilled({popup.x + 4.0f, y, popup.w - 8.0f, 30.0f}, t.surfaceActive.MulAlpha(a), t.radiusSm);
}

} // namespace

// ---------------------------------------------------------------------------
// Combo
// ---------------------------------------------------------------------------

bool Combo(std::string_view label, int* current, std::span<const char* const> items) {
    Context& c = C();
    const ID id = c.GetID(label);
    const ID popupId = HashBytes("##combo", id);
    const char* value = (*current >= 0 && *current < int(items.size())) ? items[size_t(*current)] : "—";
    bool toggled;
    const Rect field = DropdownField(label, id, popupId, value, toggled);

    bool changed = false;
    Rect pr;
    const float h = float(items.size()) * 30.0f + 8.0f;
    if (BeginPopupLayer(popupId, field, {field.w, std::min(h, 320.0f)}, pr)) {
        // The hover highlight is drawn behind the rows using last frame's
        // hovered row; it starts on the selected row the first time.
        float target = A().Peek(popupId, kIndicator, pr.y + 4.0f + float(std::max(0, *current)) * 30.0f);
        DrawHighlight(popupId, pr, target, A().Peek(popupId, kWidth) > 0.5f);
        bool any = false;
        for (int i = 0; i < int(items.size()); ++i) {
            if (PopupItem(popupId, i, items[size_t(i)], i == *current, false, target, any)) {
                *current = i;
                changed = true;
                c.ClosePopup(popupId);
            }
        }
        A().SetFloat(popupId, kIndicator, target);
        A().SetFloat(popupId, kWidth, any ? 1.0f : 0.0f);
        EndPopupLayer();
    }
    return changed;
}

// ---------------------------------------------------------------------------
// MultiSelect
// ---------------------------------------------------------------------------

bool MultiSelect(std::string_view label, bool* selected, std::span<const char* const> items) {
    Context& c = C();
    const ID id = c.GetID(label);
    const ID popupId = HashBytes("##multi", id);

    std::string summary;
    int count = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!selected[i]) continue;
        if (count < 2) summary += (count ? ", " : "") + std::string(items[i]);
        ++count;
    }
    if (count == 0) summary = "None";
    else if (count > 2) summary += " +" + std::to_string(count - 2);

    bool toggled;
    const Rect field = DropdownField(label, id, popupId, summary, toggled);
    bool changed = false;
    Rect pr;
    const float h = float(items.size()) * 30.0f + 8.0f;
    if (BeginPopupLayer(popupId, field, {field.w, std::min(h, 320.0f)}, pr)) {
        float target = A().Peek(popupId, kIndicator, pr.y + 4.0f);
        DrawHighlight(popupId, pr, target, A().Peek(popupId, kWidth) > 0.5f);
        bool any = false;
        for (int i = 0; i < int(items.size()); ++i) {
            if (PopupItem(popupId, i, items[size_t(i)], selected[i], true, target, any)) {
                selected[i] = !selected[i];
                changed = true;
            }
        }
        A().SetFloat(popupId, kIndicator, target);
        A().SetFloat(popupId, kWidth, any ? 1.0f : 0.0f);
        EndPopupLayer();
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Sliders
// ---------------------------------------------------------------------------

namespace {

bool SliderImpl(std::string_view label, double& v, double min, double max, bool isInt, const char* fmt) {
    Context& c = C();
    const Theme& t = T();
    const ID id = c.GetID(label);
    const Rect row = NextRect({0, 46});
    const Rect track{row.x + 7.0f, row.y + 33.0f, row.w - 14.0f, 4.0f};
    const Rect hit{row.x, row.y + 22.0f, row.w, 24.0f};
    const ItemState st = c.ButtonBehavior(hit, id);

    bool changed = false;
    if (st.held || st.pressed) {
        const double f = Saturate((c.In().MousePos().x - track.x) / track.w);
        double nv = min + f * (max - min);
        if (isInt) nv = std::round(nv);
        if (nv != v) {
            v = nv;
            changed = true;
        }
    }
    const float frac = float(max > min ? (v - min) / (max - min) : 0.0);
    // Visual position glides toward the value (snappy while dragging).
    const float vis = A().Float(id, kKnob, frac, Motion::Smooth(st.held ? 40.0f : 18.0f));
    const float hover = Hover(id, st.hovered || st.held);
    const float active = A().Float(id, kPress, st.held ? 1.0f : 0.0f, motion::kFade);

    char buf[64];
    if (isInt) std::snprintf(buf, sizeof(buf), fmt, int(v));
    else std::snprintf(buf, sizeof(buf), fmt, v);

    DrawList& dl = DL();
    dl.PushAlpha(EnabledAlpha(id));
    c.TextAt(dl, FontRole::Regular, t.fontBody, {row.x, row.y + 11.0f}, t.textDim, DisplayText(label));
    const float vw = c.TextWidth(FontRole::Medium, t.fontBody, buf);
    c.TextAt(dl, FontRole::Medium, t.fontBody, {row.Right() - vw, row.y + 11.0f}, Mix(t.text, t.accent, active), buf);

    dl.RectFilled(track, t.surfaceActive, 2.0f);
    const float kx = track.x + vis * track.w;
    if (kx > track.x + 0.5f) {
        const Rect fill{track.x, track.y, kx - track.x, track.h};
        dl.Shadow(fill, t.accent.WithAlpha(0.35f * hover), 2.0f, 10.0f);
        dl.RectGradient(fill, CornerColors::Horizontal(t.accent, Mix(t.accent, t.accent2, vis)), 2.0f);
    }
    const float kr = 7.0f + 1.5f * hover + 1.0f * active;
    const Vec2 kc{kx, track.Center().y};
    dl.Shadow({kc.x - kr, kc.y - kr, kr * 2, kr * 2}, t.accent.WithAlpha(0.25f + 0.35f * hover), kr, 12.0f);
    dl.Shadow({kc.x - kr, kc.y - kr, kr * 2, kr * 2}, Color(0, 0, 0, 0.35f), kr, 4.0f, {0, 1});
    dl.Circle(kc, kr, Color(1, 1, 1, 1));
    dl.Circle(kc, kr * 0.38f * active, t.accent);

    // Value bubble above the knob while dragging.
    const float tip = A().Float(id, kTip, st.held ? 1.0f : 0.0f, Motion::Tween(st.held ? 0.18f : 0.12f, Ease::OutCubic));
    if (tip > 0.01f) {
        const float bw = vw + 18.0f, bh = 24.0f;
        Rect b{kc.x - bw * 0.5f, track.y - 36.0f + (1.0f - tip) * 6.0f, bw, bh};
        dl.PushAlpha(tip);
        dl.PushTransform({b.Center().x, b.Bottom()}, 0.9f + 0.1f * tip);
        dl.Shadow(b, t.shadow, 7.0f, 14.0f, {0, 4});
        dl.RectGradient(b, AccentGradient(), 7.0f);
        c.TextCentered(dl, FontRole::Medium, t.fontSmall, b, t.textOnAccent, buf);
        dl.PopTransform();
        dl.PopAlpha();
    }
    dl.PopAlpha();
    return changed;
}

} // namespace

bool SliderFloat(std::string_view label, float* value, float min, float max, const char* fmt) {
    double v = *value;
    const bool ch = SliderImpl(label, v, min, max, false, fmt);
    if (ch) *value = float(v);
    return ch;
}

bool SliderInt(std::string_view label, int* value, int min, int max, const char* fmt) {
    double v = *value;
    const bool ch = SliderImpl(label, v, min, max, true, fmt);
    if (ch) *value = int(v);
    return ch;
}

} // namespace silk::ui
