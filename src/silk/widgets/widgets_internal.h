#pragma once
// Helpers shared by the widget implementations (not part of the public API).

#include "silk/widgets/widgets.h"

namespace silk::ui::detail {

// Animation slots per widget ID.
enum Slot : uint32_t {
    kHover = 1,
    kPress,
    kFocus,
    kValue,
    kEnabled,
    kOpen,
    kKnob,
    kTip,
    kIndicator,
    kIndicatorW,
    kScroll,
    kCaret,
    kHighlight,
    kHighlightY,
    kChevron,
    kWidth,
};

inline Context& C() { return Ctx(); }
inline const Theme& T() { return Ctx().Style(); }
inline DrawList& DL() { return Ctx().DL(); }
inline AnimStore& A() { return Ctx().Anim(); }

inline float Hover(ID id, bool on) { return A().Float(id, kHover, on ? 1.0f : 0.0f, motion::kHover); }
inline float Press(ID id, bool on) {
    return A().Float(id, kPress, on ? 1.0f : 0.0f, Motion::Springy(SpringParams::Snappy()));
}
inline float Focus(ID id, bool on) { return A().Float(id, kFocus, on ? 1.0f : 0.0f, motion::kFade); }
// 1 when enabled, fades towards 0.4 when disabled.
inline float EnabledAlpha(ID id) {
    return A().Float(id, kEnabled, C().Disabled() ? 0.4f : 1.0f, motion::kFade);
}

inline Color Mix(const Color& a, const Color& b, float t) { return Lerp(a, b, Saturate(t)); }

// Accent gradient fill used by primary controls.
inline CornerColors AccentGradient(float brighten = 0.0f) {
    const Theme& t = T();
    const Color w{1, 1, 1, 1};
    const Color a = Mix(t.accent, w.WithAlpha(t.accent.a), brighten);
    const Color b = Mix(t.accent2, w.WithAlpha(t.accent2.a), brighten);
    return {a, Mix(a, b, 0.5f), b, Mix(a, b, 0.5f)};
}

// Focus ring: soft accent glow + 1px accent border, faded by `f`.
inline void FocusRing(DrawList& dl, Rect r, float radius, float f) {
    if (f <= 0.01f) return;
    const Theme& t = T();
    dl.Shadow(r, t.accent.WithAlpha(0.35f * f), radius, 10);
    dl.RectStroke(r.Expand(1.0f), t.accent.WithAlpha(0.85f * f), radius + 1.0f, 1.0f);
}

// Draws a settings-row label on the left half of `row`.
inline void RowLabel(Rect row, std::string_view label, Color c) {
    C().TextAt(DL(), FontRole::Regular, T().fontBody, {row.x, row.Center().y}, c, DisplayText(label));
}

// Popup panel background with open/close animation applied by the caller.
void PopupPanel(DrawList& dl, Rect r);

// Shared popup lifetime: returns true while the popup should be drawn and
// pushes its layer + transform. Must be paired with EndPopupLayer().
bool BeginPopupLayer(ID id, Rect anchor, Vec2 size, Rect& outRect);
void EndPopupLayer();

} // namespace silk::ui::detail
