#include "silk/theme/theme.h"

#include "silk/anim/anim.h"

namespace silk {

Theme Theme::Dark() {
    Theme t;
    t.name = "Dark";
    t.background = Color::Hex(0x080812);
    t.window = Color::Hex(0x0B0B12);
    t.windowAlt = Color::Hex(0x0D0D15);
    t.popup = Color::Hex(0x17171F);
    t.panel = Color::Hex(0x101017);
    t.card = Color::Hex(0x14141C);
    t.surface = Color(1, 1, 1, 0.045f);
    t.surfaceHover = Color(1, 1, 1, 0.075f);
    t.surfaceActive = Color(1, 1, 1, 0.10f);
    t.border = Color(1, 1, 1, 0.075f);
    t.borderStrong = Color(1, 1, 1, 0.14f);
    t.text = Color::Hex(0xECECF1);
    t.textDim = Color::Hex(0xA3A3B2);
    t.textMuted = Color::Hex(0x6C6C7E);
    t.textOnAccent = Color::Hex(0xFFFFFF);
    t.accent = Color::Hex(0x7F6AF2);
    t.accent2 = Color::Hex(0x5B8DEF);
    t.success = Color::Hex(0x3DD68C);
    t.warning = Color::Hex(0xF5B544);
    t.error = Color::Hex(0xF2555A);
    t.info = Color::Hex(0x52A8FF);
    t.shadow = Color(0, 0, 0, 0.55f);
    t.overlay = Color(0, 0, 0, 0.45f);
    t.windowOpacity = 0.84f;
    return t;
}

Theme Theme::Light() {
    Theme t;
    t.name = "Light";
    t.background = Color::Hex(0xE9EBEF);
    t.window = Color::Hex(0xFBFBFC);
    t.windowAlt = Color::Hex(0xF2F3F5);
    t.popup = Color::Hex(0xFFFFFF);
    t.panel = Color::Hex(0xFFFFFF);
    t.card = Color::Hex(0xF6F7F9);
    t.surface = Color(0, 0, 0, 0.04f);
    t.surfaceHover = Color(0, 0, 0, 0.065f);
    t.surfaceActive = Color(0, 0, 0, 0.09f);
    t.border = Color(0, 0, 0, 0.08f);
    t.borderStrong = Color(0, 0, 0, 0.15f);
    t.text = Color::Hex(0x16171A);
    t.textDim = Color::Hex(0x5D626B);
    t.textMuted = Color::Hex(0x8E939B);
    t.textOnAccent = Color::Hex(0xFFFFFF);
    t.accent = Color::Hex(0x6A55D8);
    t.accent2 = Color::Hex(0x3F7BE8);
    t.success = Color::Hex(0x1FA86A);
    t.warning = Color::Hex(0xD9901A);
    t.error = Color::Hex(0xDC3A40);
    t.info = Color::Hex(0x2F86E8);
    t.shadow = Color(0.08f, 0.09f, 0.15f, 0.20f);
    t.overlay = Color(0.1f, 0.1f, 0.15f, 0.25f);
    t.windowOpacity = 0.86f;
    return t;
}

Theme Theme::Midnight() {
    Theme t = Dark();
    t.name = "Midnight";
    t.background = Color::Hex(0x05070F);
    t.window = Color::Hex(0x0B0F1C);
    t.windowAlt = Color::Hex(0x080B16);
    t.popup = Color::Hex(0x111628);
    t.panel = Color::Hex(0x0C1121);
    t.card = Color::Hex(0x10162A);
    t.surface = Color(0.6f, 0.7f, 1.0f, 0.05f);
    t.surfaceHover = Color(0.6f, 0.7f, 1.0f, 0.085f);
    t.surfaceActive = Color(0.6f, 0.7f, 1.0f, 0.12f);
    t.border = Color(0.6f, 0.7f, 1.0f, 0.09f);
    t.borderStrong = Color(0.6f, 0.7f, 1.0f, 0.16f);
    t.text = Color::Hex(0xE6EBFF);
    t.textDim = Color::Hex(0x97A0BF);
    t.textMuted = Color::Hex(0x5F6788);
    t.accent = Color::Hex(0x22D3EE);
    t.accent2 = Color::Hex(0x6366F1);
    return t;
}

Theme Theme::Lerp(const Theme& a, const Theme& b, float t) {
    Theme r = t < 0.5f ? a : b;
    auto c = [t](const Color& x, const Color& y) { return silk::Lerp(x, y, t); };
    auto f = [t](float x, float y) { return silk::Lerp(x, y, t); };
    r.background = c(a.background, b.background);
    r.window = c(a.window, b.window);
    r.windowAlt = c(a.windowAlt, b.windowAlt);
    r.popup = c(a.popup, b.popup);
    r.panel = c(a.panel, b.panel);
    r.card = c(a.card, b.card);
    r.surface = c(a.surface, b.surface);
    r.surfaceHover = c(a.surfaceHover, b.surfaceHover);
    r.surfaceActive = c(a.surfaceActive, b.surfaceActive);
    r.border = c(a.border, b.border);
    r.borderStrong = c(a.borderStrong, b.borderStrong);
    r.text = c(a.text, b.text);
    r.textDim = c(a.textDim, b.textDim);
    r.textMuted = c(a.textMuted, b.textMuted);
    r.textOnAccent = c(a.textOnAccent, b.textOnAccent);
    r.accent = c(a.accent, b.accent);
    r.accent2 = c(a.accent2, b.accent2);
    r.success = c(a.success, b.success);
    r.warning = c(a.warning, b.warning);
    r.error = c(a.error, b.error);
    r.info = c(a.info, b.info);
    r.shadow = c(a.shadow, b.shadow);
    r.overlay = c(a.overlay, b.overlay);
    r.windowOpacity = f(a.windowOpacity, b.windowOpacity);
    r.radiusSm = f(a.radiusSm, b.radiusSm);
    r.radius = f(a.radius, b.radius);
    r.radiusLg = f(a.radiusLg, b.radiusLg);
    r.radiusXl = f(a.radiusXl, b.radiusXl);
    return r;
}

void ThemeManager::Set(const Theme& t, bool animate) {
    from_ = current_;
    to_ = t;
    t_ = animate ? 0.0f : 1.0f;
    if (!animate) current_ = t;
}

void ThemeManager::SetAccent(Color a, Color b) {
    Theme t = to_;
    t.accent = a;
    t.accent2 = b;
    Set(t, true);
}

void ThemeManager::Update(float dt) {
    if (t_ >= 1.0f) return;
    t_ = std::min(1.0f, t_ + dt / duration_);
    current_ = Theme::Lerp(from_, to_, ApplyEase(Ease::InOutCubic, t_));
    if (t_ >= 1.0f) current_ = to_;
}

} // namespace silk
