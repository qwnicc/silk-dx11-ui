#include "silk/hud/hud.h"

#include "silk/widgets/context.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

namespace silk {

namespace {
constexpr ID kHudSeed = 0x48554400u; // "HUD"
constexpr SpringParams kEnter{300.0f, 24.0f, 1.0f};
constexpr SpringParams kReflow{320.0f, 32.0f, 1.0f};

std::string ClockText() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    return buf;
}
} // namespace

Hud::Hud() {
    elements_ = {
        {"modules", {0.988f, 0.035f}},
        {"keybinds", {0.012f, 0.10f}},
        {"fps", {0.012f, 0.935f}},
        {"clock", {0.085f, 0.935f}},
    };
}

void Hud::SetModule(const std::string& name, bool active) {
    for (Module& m : modules_) {
        if (m.name == name) {
            m.active = active;
            return;
        }
    }
    modules_.push_back({name, active});
}

Hud::Element* Hud::Find(const std::string& id) {
    for (Element& e : elements_)
        if (e.id == id) return &e;
    return nullptr;
}

// Opens the element's own layer (so only the elements themselves catch the
// mouse) and handles dragging while the menu is open.
void Hud::BeginElement(Context& c, Element& e, Vec2 size, bool rightAligned) {
    const Rect screen = c.Screen();
    Vec2 p{e.anchor.x * screen.w, e.anchor.y * screen.h};
    Rect r{rightAligned ? p.x - size.x : p.x, p.y, size.x, size.y};
    r.x = Clamp(r.x, 4.0f, std::max(4.0f, screen.Right() - r.w - 4.0f));
    r.y = Clamp(r.y, 4.0f, std::max(4.0f, screen.Bottom() - r.h - 4.0f));

    const ID id = HashBytes(e.id, kHudSeed);
    Layer& l = c.BeginLayer(id, LayerKind::Hud);
    l.interactive = c.menuOpen;
    l.rect = c.menuOpen ? r : Rect{};

    if (c.menuOpen) {
        const ItemState st = c.ButtonBehavior(r, id);
        Input& in = c.In();
        if (st.pressed) {
            e.dragging = true;
            e.grab = in.MousePos() - p;
        }
        if (e.dragging && st.held) {
            const Vec2 np = in.MousePos() - e.grab;
            e.anchor = {Saturate(np.x / screen.w), Saturate(np.y / screen.h)};
        } else {
            e.dragging = false;
        }
        const Theme& t = c.Style();
        const float h = c.Anim().Float(id, 1, (st.hovered || e.dragging) ? 1.0f : 0.0f, motion::kHover);
        l.dl.RectStroke(r.Expand(4.0f), t.accent.WithAlpha(0.15f + 0.55f * h), t.radius + 2.0f);
    }
    e.rect = r;
}

void Hud::DrawModules(Context& c, Element& e) {
    const Theme& t = c.Style();
    const float dt = c.Dt();
    const float fs = t.fontBody;
    const float entryH = 22.0f, padX = 9.0f;

    for (Module& m : modules_) {
        Spring s{m.t, m.vel};
        s.Step(m.active ? 1.0f : 0.0f, dt, kEnter);
        m.t = s.value;
        m.vel = s.velocity;
    }
    std::vector<Module*> list;
    for (Module& m : modules_)
        if (m.active || m.t > 0.01f) list.push_back(&m);
    // Longest first: the classic "staircase" array list.
    std::sort(list.begin(), list.end(), [&](Module* a, Module* b) {
        return c.TextWidth(FontRole::Medium, fs, a->name) > c.TextWidth(FontRole::Medium, fs, b->name);
    });
    float maxW = 60.0f;
    for (Module* m : list) maxW = std::max(maxW, c.TextWidth(FontRole::Medium, fs, m->name) + padX * 2.0f + 3.0f);

    const bool right = e.anchor.x > 0.5f;
    BeginElement(c, e, {maxW, std::max(entryH, float(list.size()) * entryH)}, right);
    const Rect r = e.rect;
    DrawList& dl = c.DL();
    for (size_t i = 0; i < list.size(); ++i) {
        Module& m = *list[i];
        const float targetY = r.y + float(i) * entryH;
        if (!m.placed) { m.y = targetY; m.placed = true; }
        Spring ys{m.y, m.yVel};
        ys.Step(targetY, dt, kReflow);
        m.y = ys.value;
        m.yVel = ys.velocity;

        const float k = Saturate(m.t);
        const float w = c.TextWidth(FontRole::Medium, fs, m.name) + padX * 2.0f + 3.0f;
        const float slide = (1.0f - m.t) * (w + 30.0f) * (right ? 1.0f : -1.0f);
        const float x = (right ? r.Right() - w : r.x) + slide;
        const Rect row{x, m.y, w, entryH};
        const float g = list.size() > 1 ? float(i) / float(list.size() - 1) : 0.0f;
        const Color col = Lerp(t.accent, t.accent2, g);

        dl.PushAlpha(k);
        dl.RectFilled(row, t.window.WithAlpha(0.78f), 4.0f);
        const Rect bar{right ? row.Right() - 2.0f : row.x, row.y + 3.0f, 2.0f, row.h - 6.0f};
        dl.Shadow(bar, col.WithAlpha(0.8f), 1.0f, 8.0f);
        dl.RectFilled(bar, col, 1.0f);
        c.TextAt(dl, FontRole::Medium, fs, {row.x + padX + (right ? 0.0f : 3.0f), row.Center().y}, Lerp(t.text, col, 0.35f),
                 m.name);
        dl.PopAlpha();
    }
    c.EndLayer();
}

void Hud::DrawFps(Context& c, Element& e) {
    const Theme& t = c.Style();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d FPS", int(c.Time().Fps() + 0.5f));
    const float w = 34.0f + c.TextWidth(FontRole::Medium, t.fontBody, "000 FPS");
    BeginElement(c, e, {w, 28.0f}, false);
    const Rect r = e.rect;
    DrawList& dl = c.DL();
    dl.RectFilled(r, t.window.WithAlpha(0.8f), t.radius);
    dl.RectStroke(r, t.border, t.radius);
    c.IconAt(dl, "gauge", {r.x + 6.0f, r.y, 20.0f, r.h}, 14.0f, t.accent);
    c.TextAt(dl, FontRole::Medium, t.fontBody, {r.x + 28.0f, r.Center().y}, t.text, buf);
    c.EndLayer();
}

void Hud::DrawClock(Context& c, Element& e) {
    const Theme& t = c.Style();
    const std::string time = ClockText();
    const float w = 34.0f + c.TextWidth(FontRole::Medium, t.fontBody, "00:00:00");
    BeginElement(c, e, {w, 28.0f}, false);
    const Rect r = e.rect;
    DrawList& dl = c.DL();
    dl.RectFilled(r, t.window.WithAlpha(0.8f), t.radius);
    dl.RectStroke(r, t.border, t.radius);
    c.IconAt(dl, "clock", {r.x + 6.0f, r.y, 20.0f, r.h}, 14.0f, t.accent);
    c.TextAt(dl, FontRole::Medium, t.fontBody, {r.x + 28.0f, r.Center().y}, t.text, time);
    c.EndLayer();
}

void Hud::DrawKeybinds(Context& c, Element& e) {
    const Theme& t = c.Style();
    const float header = 38.0f, rowH = 22.0f;
    const float targetH = header + 12.0f + std::max(1.0f, float(keybinds_.size())) * rowH;
    const ID id = HashBytes("keybinds-h", kHudSeed);
    const float h = c.Anim().Float(id, 0, targetH, Motion::Springy({320.0f, 30.0f, 1.0f}));
    BeginElement(c, e, {210.0f, h}, false);
    const Rect r = e.rect;
    DrawList& dl = c.DL();
    dl.Shadow(r, t.shadow.MulAlpha(0.6f), t.radiusLg, 20.0f, {0, 6});
    if (t.glass) dl.Glass(r, t.radiusLg, t.window.WithAlpha(0.8f));
    else dl.RectFilled(r, t.window, t.radiusLg);
    dl.RectStroke(r, t.border, t.radiusLg);
    c.IconAt(dl, "keyboard", {r.x + 10.0f, r.y + 6.0f, 22.0f, 26.0f}, 15.0f, t.accent);
    c.TextAt(dl, FontRole::Medium, t.fontHeading, {r.x + 36.0f, r.y + 19.0f}, t.text, "Keybinds");
    const Rect ul{r.x + 12.0f, r.y + header - 4.0f, r.w - 24.0f, 2.0f};
    dl.RectGradient(ul, CornerColors::Horizontal(t.accent, t.accent2.WithAlpha(0.0f)), 1.0f);

    dl.PushClip(r);
    float y = r.y + header + 6.0f;
    if (keybinds_.empty()) {
        c.TextAt(dl, FontRole::Regular, t.fontSmall, {r.x + 12.0f, y + rowH * 0.5f}, t.textMuted, "no active binds");
    }
    for (const auto& [name, key] : keybinds_) {
        c.TextAt(dl, FontRole::Regular, t.fontSmall + 0.5f, {r.x + 12.0f, y + rowH * 0.5f}, t.textDim, name);
        const float kw = c.TextWidth(FontRole::Medium, t.fontSmall, key) + 12.0f;
        const Rect kr{r.Right() - 12.0f - kw, y + 2.0f, kw, rowH - 4.0f};
        dl.RectFilled(kr, t.accent.WithAlpha(0.14f), 4.0f);
        c.TextCentered(dl, FontRole::Medium, t.fontSmall, kr, t.accent, key);
        y += rowH;
    }
    dl.PopClip();
    c.EndLayer();
}

void Hud::DrawWatermark(Context& c) {
    WatermarkConfig& w = watermark_;
    const ID id = HashBytes("watermark", kHudSeed);
    const float vis = c.Anim().Float(id, 0, w.visible ? 1.0f : 0.0f, motion::kFade);
    if (vis <= 0.01f) return;
    const Theme& t = c.Style();
    const float fs = t.fontBody;

    // Segments: brand, user, fps, ping, time.
    struct Seg { std::string text; Color color; FontRole role; };
    std::vector<Seg> segs;
    segs.push_back({w.brand, t.accent, FontRole::Bold});
    if (w.showUser) segs.push_back({w.user, t.text, FontRole::Medium});
    char buf[32];
    if (w.showFps) {
        std::snprintf(buf, sizeof(buf), "%d fps", int(c.Time().Fps() + 0.5f));
        segs.push_back({buf, t.text, FontRole::Medium});
    }
    if (w.showPing) {
        std::snprintf(buf, sizeof(buf), "%d ms", ping_);
        segs.push_back({buf, t.text, FontRole::Medium});
    }
    if (w.showTime) segs.push_back({ClockText(), t.text, FontRole::Medium});

    const float padX = 12.0f, sepW = 17.0f, iconW = 20.0f;
    float total = padX * 2.0f + iconW;
    for (size_t i = 0; i < segs.size(); ++i)
        total += c.TextWidth(segs[i].role, fs, segs[i].text) + (i ? sepW : 0.0f);
    // The bar width eases toward the content width when text length changes.
    const float width = c.Anim().Float(id, 1, total, Motion::Springy({360.0f, 32.0f, 1.0f}));
    const float h = 32.0f, m = 12.0f;
    const Rect screen = c.Screen();
    const bool rightSide = w.corner == 1 || w.corner == 3;
    const bool bottomSide = w.corner >= 2;
    const Rect r{rightSide ? screen.Right() - m - width : m, bottomSide ? screen.Bottom() - m - h : m, width, h};

    Layer& l = c.BeginLayer(id, LayerKind::Hud);
    l.interactive = false;
    DrawList& dl = l.dl;
    dl.PushAlpha(vis * w.opacity);
    dl.Shadow(r, t.shadow.MulAlpha(0.7f), t.radius, 18.0f, {0, 6});
    if (t.glass) dl.Glass(r, t.radius, t.window.WithAlpha(0.82f));
    else dl.RectFilled(r, t.window, t.radius);
    dl.RectStroke(r, t.border, t.radius);
    // Accent gradient hairline along the top edge.
    const Rect line{r.x + 8.0f, r.y, r.w - 16.0f, 2.0f};
    dl.Shadow(line, t.accent.WithAlpha(0.6f), 1.0f, 8.0f);
    dl.RectGradient(line, CornerColors::Horizontal(t.accent, t.accent2), 1.0f);

    dl.PushClip(r);
    float x = r.x + padX;
    c.IconAt(dl, w.icon, {x - 2.0f, r.y, iconW, h}, 14.0f, t.accent);
    x += iconW;
    for (size_t i = 0; i < segs.size(); ++i) {
        if (i) {
            dl.RectFilled({x + sepW * 0.5f - 0.5f, r.y + 9.0f, 1.0f, h - 18.0f}, t.borderStrong);
            x += sepW;
        }
        if (i == 0) dl.Shadow({x, r.y + 10.0f, c.TextWidth(segs[i].role, fs, segs[i].text), 12.0f},
                              t.accent.WithAlpha(0.35f), 6.0f, 12.0f);
        const float tw = c.TextWidth(segs[i].role, fs, segs[i].text);
        c.TextAt(dl, segs[i].role, fs, {x, r.Center().y}, segs[i].color, segs[i].text);
        x += tw;
    }
    dl.PopClip();
    dl.PopAlpha();
    c.EndLayer();
}

void Hud::Draw(Context& c) {
    for (Element& e : elements_) {
        if (!e.visible) continue;
        if (e.id == "modules") DrawModules(c, e);
        else if (e.id == "fps") DrawFps(c, e);
        else if (e.id == "clock") DrawClock(c, e);
        else if (e.id == "keybinds") DrawKeybinds(c, e);
    }
    DrawWatermark(c);
}

json::Value Hud::Save() const {
    json::Value v;
    for (const Element& e : elements_) {
        json::Value& o = v["elements"][e.id];
        o["x"] = e.anchor.x;
        o["y"] = e.anchor.y;
        o["visible"] = e.visible;
    }
    json::Value& w = v["watermark"];
    w["visible"] = watermark_.visible;
    w["corner"] = watermark_.corner;
    w["showUser"] = watermark_.showUser;
    w["showFps"] = watermark_.showFps;
    w["showPing"] = watermark_.showPing;
    w["showTime"] = watermark_.showTime;
    w["opacity"] = watermark_.opacity;
    return v;
}

void Hud::Load(const json::Value& v) {
    const json::Value& els = v.Get("elements");
    for (Element& e : elements_) {
        const json::Value& o = els.Get(e.id);
        if (!o.IsObject()) continue;
        e.anchor = {Saturate(o.Get("x").AsFloat(e.anchor.x)), Saturate(o.Get("y").AsFloat(e.anchor.y))};
        e.visible = o.Get("visible").AsBool(e.visible);
    }
    const json::Value& w = v.Get("watermark");
    if (w.IsObject()) {
        watermark_.visible = w.Get("visible").AsBool(watermark_.visible);
        watermark_.corner = std::clamp(w.Get("corner").AsInt(watermark_.corner), 0, 3);
        watermark_.showUser = w.Get("showUser").AsBool(watermark_.showUser);
        watermark_.showFps = w.Get("showFps").AsBool(watermark_.showFps);
        watermark_.showPing = w.Get("showPing").AsBool(watermark_.showPing);
        watermark_.showTime = w.Get("showTime").AsBool(watermark_.showTime);
        watermark_.opacity = Clamp(w.Get("opacity").AsFloat(watermark_.opacity), 0.1f, 1.0f);
    }
}

} // namespace silk
