// Containers: Window, Sidebar, Tabs, Child (scroll area), Card, Modal,
// plus page-level decorations (PageHeader, ProgressBar, Spinner).

#include "silk/widgets/widgets_internal.h"

#include <cmath>
#include <cstdio>
#include <optional>
#include <unordered_map>
#include <vector>

namespace silk::ui {

using namespace detail;

namespace {

struct WindowFrame {
    ID id = 0;
    Rect rect;
    float bodyTop = 0.0f;
    uint32_t flags = 0;
    bool visible = false;
    ID sidebarId = 0;
    Rect sidebar;
};
std::vector<WindowFrame> g_windows;
std::optional<Vec2> g_nextPos, g_nextSize;

// Previous-frame targets for indicators that are drawn before the item that
// defines them (sidebar selection).
std::unordered_map<ID, float> g_indicatorTarget;

struct TabContentState { int selected = -1; float dir = 1.0f; };
std::unordered_map<ID, TabContentState> g_tabContent;

std::vector<ID> g_childStack;
std::vector<ID> g_modalStack;
std::vector<Rect> g_childRects;

constexpr float kTitleBarH = 44.0f;

} // namespace

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

void SetNextWindowPos(Vec2 pos) { g_nextPos = pos; }
void SetNextWindowSize(Vec2 size) { g_nextSize = size; }

bool Begin(std::string_view title, bool* open, uint32_t flags) {
    Context& c = C();
    const Theme& t = T();
    const ID id = HashBytes(title);
    WindowState& w = c.Window(id);
    if (!w.initialized) {
        w.pos = g_nextPos.value_or(Vec2{80, 80});
        w.size = g_nextSize.value_or(Vec2{820, 560});
        w.initialized = true;
        A().SetFloat(id, kOpen, 0.0f); // play the opening animation on first show
    }
    g_nextPos.reset();
    g_nextSize.reset();

    const bool isOpen = open ? *open : true;
    w.openT = A().Float(id, kOpen, isOpen ? 1.0f : 0.0f,
                        isOpen ? Motion::Tween(0.34f, Ease::OutExpo) : Motion::Tween(0.16f, Ease::OutCubic));
    WindowFrame wf;
    wf.id = id;
    wf.flags = flags;
    if (!isOpen && w.openT <= 0.002f) {
        g_windows.push_back(wf);
        return false;
    }
    wf.visible = true;

    Layer& l = c.BeginLayer(id, LayerKind::Window);
    l.interactive = isOpen;
    const Rect r{w.pos, w.size};
    l.rect = r;
    wf.rect = r;

    DrawList& dl = l.dl;
    dl.PushAlpha(w.openT);
    dl.PushTransform(r.Center(), 0.96f + 0.04f * w.openT);
    dl.Shadow(r, t.shadow, t.radiusXl, 48.0f, {0, 24});
    if (t.glass && !(flags & WindowFlags_NoGlass)) dl.Glass(r, t.radiusXl, t.window.WithAlpha(t.windowOpacity));
    else dl.RectFilled(r, t.window, t.radiusXl);
    dl.RectStroke(r, t.border, t.radiusXl);

    float bodyTop = r.y;
    if (!(flags & WindowFlags_NoTitleBar)) {
        const Rect bar{r.x, r.y, r.w, kTitleBarH};
        c.TextAt(dl, FontRole::Medium, t.fontHeading, {bar.x + 18.0f, bar.Center().y}, t.text, DisplayText(title));
        dl.RectFilled({bar.x, bar.Bottom() - 1.0f, bar.w, 1.0f}, t.border);
        if (open) {
            const Rect cb{bar.Right() - 38.0f, bar.y + 8.0f, 28.0f, 28.0f};
            const ID cid = HashBytes("#close", id);
            const ItemState st = c.ButtonBehavior(cb, cid);
            const float h = Hover(cid, st.hovered);
            dl.RectFilled(cb, t.error.WithAlpha(0.16f * h), t.radiusSm);
            c.IconAt(dl, "x", cb, 15.0f, Mix(t.textMuted, t.error, h));
            if (st.clicked) *open = false;
        }
        bodyTop = bar.Bottom();
    }
    wf.bodyTop = bodyTop;
    dl.PushClip(r);
    c.PushID(id); // widgets in different windows never collide

    LayoutStyle ls;
    ls.gap = t.spacing;
    ls.padding = Padding(t.padding + 4.0f);
    c.Layout().Push(id, {r.x, bodyTop, r.w, r.Bottom() - bodyTop}, ls);
    g_windows.push_back(wf);
    return true;
}

void End() {
    if (g_windows.empty()) return;
    const WindowFrame wf = g_windows.back();
    g_windows.pop_back();
    if (!wf.visible) return;

    Context& c = C();
    const Theme& t = T();
    c.Layout().Pop();
    c.PopID();
    WindowState& w = c.Window(wf.id);
    DrawList& dl = c.DL();
    Input& in = c.In();

    // Resize grip (bottom-right corner).
    if (!(wf.flags & WindowFlags_NoResize)) {
        const Rect grip{wf.rect.Right() - 18.0f, wf.rect.Bottom() - 18.0f, 18.0f, 18.0f};
        const ID gid = HashBytes("#resize", wf.id);
        const ItemState st = c.ButtonBehavior(grip, gid);
        if (st.pressed) w.dragOffset = wf.rect.Max() - in.MousePos();
        if (st.held) {
            const Vec2 m = in.MousePos() + w.dragOffset;
            w.size = {std::max(420.0f, m.x - w.pos.x), std::max(300.0f, m.y - w.pos.y)};
        }
        const float h = Hover(gid, st.hovered || st.held);
        for (int i = 0; i < 3; ++i)
            dl.Circle({grip.Right() - 6.0f - float(i) * 4.0f, grip.Bottom() - 6.0f}, 1.0f,
                      t.textMuted.MulAlpha(0.3f + 0.7f * h));
    }

    // Drag by any empty area of the window (no widget under the mouse).
    if (!(wf.flags & WindowFlags_NoMove)) {
        const ID did = HashBytes("#move", wf.id);
        Layer* l = c.CurrentLayer();
        if (!w.dragging && in.MousePressed() && l && c.IsLayerHovered(*l) && c.HotId() == 0 &&
            (c.ActiveId() == 0 || c.ActiveId() == did)) {
            w.dragging = true;
            w.dragOffset = in.MousePos() - w.pos;
        }
        if (w.dragging) {
            if (in.MouseDown()) {
                c.SetActive(did);
                const Rect s = c.Screen();
                w.pos = in.MousePos() - w.dragOffset;
                w.pos.x = Clamp(w.pos.x, s.x - w.size.x + 80.0f, s.Right() - 80.0f);
                w.pos.y = Clamp(w.pos.y, s.y, s.Bottom() - 40.0f);
            } else {
                w.dragging = false;
                if (c.ActiveId() == did) c.ClearActive();
            }
        }
    }

    dl.PopClip();
    dl.PopTransform();
    dl.PopAlpha();
    c.EndLayer();
}

// ---------------------------------------------------------------------------
// Sidebar
// ---------------------------------------------------------------------------

void BeginSidebar(float width) {
    if (g_windows.empty()) return;
    Context& c = C();
    const Theme& t = T();
    WindowFrame& wf = g_windows.back();
    const Rect strip{wf.rect.x, wf.bodyTop, width, wf.rect.Bottom() - wf.bodyTop};
    wf.sidebar = strip;
    wf.sidebarId = HashBytes("#sidebar", wf.id);

    // The rest of the window becomes a raised content panel.
    DrawList& dl = DL();
    const Rect panel{strip.Right(), wf.rect.y, wf.rect.Right() - strip.Right(), wf.rect.h};
    dl.Shadow(panel, Color(0, 0, 0, 0.25f), t.radiusXl, 24.0f);
    dl.RectFilled(panel, t.panel.WithAlpha(0.92f), t.radiusXl);
    dl.RectStroke(panel, t.border, t.radiusXl);

    LayoutFrame& f = c.Layout().Top();
    const float pad = t.padding + 4.0f;
    f.rect = {panel.x + pad, wf.bodyTop + pad, panel.w - pad * 2.0f, panel.Bottom() - wf.bodyTop - pad * 2.0f};
    f.cursor = f.rect.Pos();
    f.maxPos = f.rect.Pos();

    // Sliding selection indicator, drawn before the items (target from last frame).
    const auto it = g_indicatorTarget.find(wf.sidebarId);
    if (it != g_indicatorTarget.end()) {
        const float y = A().Float(wf.sidebarId, kIndicator, it->second, motion::kSlide);
        const Rect sel{strip.x + 12.0f, y, strip.w - 24.0f, 34.0f};
        dl.RectGradient(sel, CornerColors::Horizontal(t.accent.WithAlpha(0.20f), t.accent.WithAlpha(0.0f)), t.radius);
        const Rect bar{sel.x, sel.y + 7.0f, 3.0f, sel.h - 14.0f};
        dl.Shadow(bar, t.accent.WithAlpha(0.7f), 1.5f, 8.0f);
        dl.RectGradient(bar, CornerColors::Vertical(t.accent, t.accent2), 1.5f);
    }

    LayoutStyle ls;
    ls.gap = 6.0f;
    ls.padding = Padding(12.0f, 22.0f, 12.0f, 12.0f);
    c.Layout().Push(wf.sidebarId, strip, ls);
}

void SidebarHeader(std::string_view brand, std::string_view subtitle) {
    Context& c = C();
    const Theme& t = T();
    DrawList& dl = DL();
    const Rect r = NextRect({0, subtitle.empty() ? 30.0f : 46.0f});
    const float size = 22.0f;
    const float w = c.TextWidth(FontRole::Bold, size, brand);
    const Rect tr{r.x + 10.0f, r.y, w, 30.0f};
    // Glow behind the brand name.
    dl.Shadow(tr.Expand(-2.0f, -6.0f), t.accent.WithAlpha(0.55f), 8.0f, 22.0f);
    c.TextAt(dl, FontRole::Bold, size, {tr.x, tr.Center().y}, Mix(t.accent, Color(1, 1, 1, 1), 0.12f), brand);
    if (!subtitle.empty())
        c.TextAt(dl, FontRole::Regular, t.fontSmall, {tr.x, r.y + 38.0f}, t.textMuted, subtitle);
    C().Layout().Space(12.0f);
}

void SidebarSection(std::string_view label) {
    const Theme& t = T();
    Spacing(6.0f);
    const Rect r = NextRect({0, 18});
    C().TextAt(DL(), FontRole::Medium, t.fontSmall - 1.0f, {r.x + 10.0f, r.Center().y}, t.textMuted, label);
}

bool SidebarItem(std::string_view icon, std::string_view label, int* selected, int index) {
    Context& c = C();
    const Theme& t = T();
    const ID id = c.GetID(label);
    const Rect r = NextRect({0, 34});
    const ItemState st = c.ButtonBehavior(r, id);
    if (st.clicked) *selected = index;
    const bool active = *selected == index;
    if (active && !g_windows.empty()) g_indicatorTarget[g_windows.back().sidebarId] = r.y;

    const float hover = Hover(id, st.hovered);
    const float on = A().Float(id, kValue, active ? 1.0f : 0.0f, motion::kFade);
    const float press = Press(id, st.held);
    DrawList& dl = DL();
    dl.PushAlpha(EnabledAlpha(id));
    dl.RectFilled(r, t.surface.MulAlpha(hover * (1.0f - on)), t.radius);
    dl.PushTransform(r.Center(), 1.0f - 0.02f * press);
    const Color tc = Mix(Mix(t.textDim, t.text, hover), t.text, on);
    c.IconAt(dl, icon, {r.x + 10.0f, r.y, 20.0f, r.h}, 15.0f, Mix(Mix(t.textMuted, t.textDim, hover), t.accent, on));
    // Active label nudges right a little.
    c.TextAt(dl, on > 0.5f ? FontRole::Medium : FontRole::Regular, t.fontBody, {r.x + 42.0f + 2.0f * on, r.Center().y}, tc,
             DisplayText(label));
    dl.PopTransform();
    dl.PopAlpha();
    return st.clicked;
}

void SidebarUser(std::string_view name, std::string_view subtitle, Color status) {
    Context& c = C();
    const Theme& t = T();
    LayoutFrame& f = c.Layout().Top();
    const Rect r{f.rect.x, f.rect.Bottom() - 48.0f, f.rect.w, 48.0f};
    DrawList& dl = DL();
    dl.RectFilled(r, t.surface, t.radiusLg);
    dl.RectStroke(r, t.border, t.radiusLg);
    const Vec2 av{r.x + 26.0f, r.Center().y};
    dl.RectRadial({av.x - 15, av.y - 15, 30, 30}, 15, av - Vec2(6, 6), 30, t.accent, t.accent2);
    const std::string initial = name.empty() ? "?" : std::string(name.substr(0, 1));
    c.TextCentered(dl, FontRole::Bold, 14.0f, {av.x - 15, av.y - 15, 30, 30}, t.textOnAccent, initial);
    dl.Circle(av + Vec2(10.5f, 10.5f), 5.0f, t.panel);
    dl.Shadow({av.x + 7.0f, av.y + 7.0f, 7.0f, 7.0f}, status.WithAlpha(0.7f), 3.5f, 6.0f);
    dl.Circle(av + Vec2(10.5f, 10.5f), 3.5f, status);
    c.TextAt(dl, FontRole::Medium, t.fontBody, {r.x + 50.0f, r.y + 16.0f}, t.text, name);
    c.TextAt(dl, FontRole::Regular, t.fontSmall, {r.x + 50.0f, r.y + 33.0f}, t.accent, subtitle);
}

void EndSidebar() { C().Layout().Pop(); }

// ---------------------------------------------------------------------------
// Page decorations
// ---------------------------------------------------------------------------

void PageHeader(std::string_view title, std::string_view subtitle) {
    Context& c = C();
    const Theme& t = T();
    const Rect r = NextRect({0, subtitle.empty() ? 30.0f : 46.0f});
    c.TextAt(DL(), FontRole::Bold, t.fontTitle + 2.0f, {r.x, r.y + 13.0f}, t.text, title);
    if (!subtitle.empty()) c.TextAt(DL(), FontRole::Regular, t.fontSmall, {r.x, r.y + 37.0f}, t.textMuted, subtitle);
    Spacing(6.0f);
}

void ProgressBar(std::string_view label, float fraction) {
    Context& c = C();
    const Theme& t = T();
    const ID id = c.GetID(label);
    const Rect r = NextRect({0, 32});
    const float f = A().Float(id, kValue, Saturate(fraction), Motion::Smooth(6.0f));
    DrawList& dl = DL();
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%d%%", int(std::round(f * 100.0f)));
    c.TextAt(dl, FontRole::Regular, t.fontBody, {r.x, r.y + 9.0f}, t.textDim, DisplayText(label));
    const float pw = c.TextWidth(FontRole::Medium, t.fontBody, pct);
    c.TextAt(dl, FontRole::Medium, t.fontBody, {r.Right() - pw, r.y + 9.0f}, t.text, pct);
    const Rect track{r.x, r.y + 24.0f, r.w, 5.0f};
    dl.RectFilled(track, t.surfaceActive, 2.5f);
    if (f > 0.0f) {
        const Rect fill{track.x, track.y, track.w * f, track.h};
        dl.Shadow(fill, t.accent.WithAlpha(0.45f), 2.5f, 10.0f);
        dl.RectGradient(fill, CornerColors::Horizontal(t.accent, t.accent2), 2.5f);
    }
}

void Spinner(std::string_view text) {
    Context& c = C();
    const Theme& t = T();
    const Rect r = NextRect({0, 26});
    DrawList& dl = DL();
    const Vec2 ctr{r.x + 9.0f, r.Center().y};
    const float rad = 7.0f;
    const float base = float(c.Now()) * 5.0f;
    dl.CircleStroke(ctr, rad + 1.0f, t.surfaceActive, 2.0f);
    // Arc approximated by short capsules with a fading tail.
    constexpr int kSeg = 18;
    for (int i = 0; i < kSeg; ++i) {
        const float a0 = base + float(i) / kSeg * 4.4f, a1 = base + float(i + 1) / kSeg * 4.4f;
        const Color col = Mix(t.accent2, t.accent, float(i) / kSeg).MulAlpha(float(i + 1) / kSeg);
        dl.Line(ctr + Vec2(std::cos(a0), std::sin(a0)) * rad, ctr + Vec2(std::cos(a1), std::sin(a1)) * rad, col, 2.0f);
    }
    c.TextAt(dl, FontRole::Regular, t.fontBody, {r.x + 26.0f, r.Center().y}, t.textDim, text);
}

// ---------------------------------------------------------------------------
// Tabs (underline style, sliding gradient indicator)
// ---------------------------------------------------------------------------

bool Tabs(std::string_view idLabel, int* selected, std::span<const char* const> labels) {
    Context& c = C();
    const Theme& t = T();
    const ID id = c.GetID(idLabel);
    const Rect r = NextRect({0, 38});
    DrawList& dl = DL();
    dl.RectFilled({r.x, r.Bottom() - 1.0f, r.w, 1.0f}, t.border);

    bool changed = false;
    float x = r.x;
    Rect selRect{};
    std::vector<Rect> rects;
    for (size_t i = 0; i < labels.size(); ++i) {
        const float w = c.TextWidth(FontRole::Medium, t.fontBody, labels[i]) + 4.0f;
        rects.push_back({x, r.y, w, r.h});
        if (int(i) == *selected) selRect = rects.back();
        x += w + 24.0f;
    }
    // Indicator first so it sits under the text.
    const float ix = A().Float(id, kIndicator, selRect.x, motion::kSlide);
    const float iw = A().Float(id, kIndicatorW, selRect.w, motion::kSlide);
    const Rect ind{ix, r.Bottom() - 2.0f, iw, 2.0f};
    dl.Shadow(ind, t.accent.WithAlpha(0.6f), 1.0f, 8.0f);
    dl.RectGradient(ind, CornerColors::Horizontal(t.accent, t.accent2), 1.0f);

    for (size_t i = 0; i < labels.size(); ++i) {
        const ID tid = HashInt(uint32_t(i), id);
        const ItemState st = c.ButtonBehavior(rects[i], tid);
        if (st.clicked && *selected != int(i)) {
            *selected = int(i);
            changed = true;
        }
        const float hover = Hover(tid, st.hovered);
        const float on = A().Float(tid, kValue, *selected == int(i) ? 1.0f : 0.0f, motion::kFade);
        c.TextAt(dl, FontRole::Medium, t.fontBody, {rects[i].x + 2.0f, r.Center().y - 1.0f},
                 Mix(Mix(t.textMuted, t.textDim, hover), t.text, on), labels[i]);
    }
    return changed;
}

void BeginTabContent(std::string_view idLabel, int selected) {
    Context& c = C();
    const ID id = c.GetID(idLabel);
    TabContentState& s = g_tabContent[id];
    if (s.selected != selected) {
        if (s.selected >= 0) {
            s.dir = selected > s.selected ? 1.0f : -1.0f;
            A().SetFloat(id, kValue, 0.0f);
        }
        s.selected = selected;
    }
    const float k = A().Float(id, kValue, 1.0f, Motion::Tween(0.30f, Ease::OutCubic));
    const Rect r = NextRect({0, 0});
    DL().PushAlpha(k);
    // Slide in from the side of travel; layout is offset so hit-testing matches.
    LayoutStyle ls;
    ls.gap = T().spacing;
    c.Layout().Push(HashInt(uint32_t(selected), id), r.Translate({(1.0f - k) * 22.0f * s.dir, 0.0f}), ls);
}

void EndTabContent() {
    C().Layout().Pop();
    DL().PopAlpha();
}

// ---------------------------------------------------------------------------
// Card
// ---------------------------------------------------------------------------

void BeginCard(std::string_view title, Vec2 size) {
    Context& c = C();
    const Theme& t = T();
    const ID id = c.GetID(title);
    const float header = title.empty() ? 0.0f : 42.0f;
    const float pad = 16.0f;
    float h = size.y;
    if (h == 0.0f) h = c.Layout().Measured(id, {0, 80}).y + header + pad * 2.0f;
    else if (h < 0.0f) h = 0.0f; // fill
    const Rect r = NextRect({size.x, h});
    DrawList& dl = DL();
    dl.RectFilled(r, t.card.WithAlpha(0.9f), t.radiusLg);
    dl.RectStroke(r, t.border, t.radiusLg);
    if (header > 0.0f) {
        const std::string_view text = DisplayText(title);
        const float tw = c.TextWidth(FontRole::Medium, t.fontHeading, text);
        c.TextAt(dl, FontRole::Medium, t.fontHeading, {r.x + pad, r.y + 20.0f}, t.text, text);
        dl.RectFilled({r.x, r.y + header - 1.0f, r.w, 1.0f}, t.border);
        const Rect ul{r.x + pad, r.y + header - 2.0f, tw, 2.0f};
        dl.Shadow(ul, t.accent.WithAlpha(0.5f), 1.0f, 6.0f);
        dl.RectGradient(ul, CornerColors::Horizontal(t.accent, t.accent2), 1.0f);
    }
    LayoutStyle ls;
    ls.gap = 10.0f;
    ls.padding = Padding(pad);
    c.Layout().Push(id, {r.x, r.y + header, r.w, r.h - header}, ls);
}

void EndCard() { C().Layout().Pop(); }

// ---------------------------------------------------------------------------
// Child / scroll area
// ---------------------------------------------------------------------------

void BeginChild(std::string_view idLabel, Vec2 size, bool border) {
    Context& c = C();
    const Theme& t = T();
    const ID id = c.GetID(idLabel);
    const Rect r = NextRect(size);
    ScrollState& s = c.Scroll(id);
    s.viewHeight = r.h;
    const float maxScroll = std::max(0.0f, s.contentHeight - r.h);
    const float dt = c.Dt();

    // Wheel adds momentum; friction decays it; out-of-range is pulled back by a spring.
    Layer* l = c.CurrentLayer();
    const bool hovered = l && c.IsLayerHovered(*l) && r.Contains(c.In().MousePos()) &&
                         DL().CurrentClip().Contains(c.In().MousePos());
    if (hovered && c.In().Wheel() != 0.0f && maxScroll > 0.0f) {
        s.velocity -= c.In().Wheel() * 900.0f;
        s.lastActivity = c.Now();
    }
    if (!s.draggingThumb) {
        if (s.scroll < 0.0f || s.scroll > maxScroll) {
            Spring sp{s.scroll, s.velocity};
            sp.Step(Clamp(s.scroll, 0.0f, maxScroll), dt, {260.0f, 30.0f, 1.0f});
            s.scroll = sp.value;
            s.velocity = sp.velocity;
        } else {
            s.scroll += s.velocity * dt;
            s.velocity *= std::exp(-dt * 7.5f);
            if (std::abs(s.velocity) < 2.0f) s.velocity = 0.0f;
            // Rubber band: slow down hard past the edges.
            if (s.scroll < 0.0f || s.scroll > maxScroll) s.velocity *= 0.35f;
        }
    }
    if (maxScroll <= 0.0f && s.scroll != 0.0f && std::abs(s.scroll) < 0.5f) s.scroll = 0.0f;

    DrawList& dl = DL();
    if (border) {
        dl.RectFilled(r, t.surface.MulAlpha(0.6f), t.radiusLg);
        dl.RectStroke(r, t.border, t.radiusLg);
    }
    dl.PushClip(r);
    LayoutStyle ls;
    ls.gap = t.spacing;
    ls.padding = border ? Padding(12.0f, 12.0f, 18.0f, 12.0f) : Padding(0.0f, 0.0f, 12.0f, 0.0f);
    c.Layout().Push(id, {r.x, r.y - std::round(s.scroll), r.w, 100000.0f}, ls);
    g_childStack.push_back(id);
    g_childRects.push_back(r);
}

void EndChild() {
    if (g_childStack.empty()) return;
    Context& c = C();
    const Theme& t = T();
    const ID id = g_childStack.back();
    const Rect r = g_childRects.back();
    g_childStack.pop_back();
    g_childRects.pop_back();
    const LayoutFrame f = c.Layout().Pop();
    ScrollState& s = c.Scroll(id);
    s.contentHeight = (f.maxPos.y - f.rect.y) + f.style.padding.top + f.style.padding.bottom;
    DrawList& dl = DL();
    dl.PopClip();

    const float maxScroll = std::max(0.0f, s.contentHeight - r.h);
    if (maxScroll <= 0.0f) return;

    // Thin scrollbar: fades in while scrolling / hovering, widens on hover.
    const float thumbH = std::max(28.0f, r.h * r.h / s.contentHeight);
    float k = s.scroll / maxScroll;
    float th = thumbH;
    if (k < 0.0f) { th = thumbH * std::max(0.4f, 1.0f + k * 3.0f); k = 0.0f; }   // squash on overscroll
    if (k > 1.0f) { th = thumbH * std::max(0.4f, 1.0f - (k - 1.0f) * 3.0f); k = 1.0f; }
    const Rect bar{r.Right() - 10.0f, r.y + 4.0f, 8.0f, r.h - 8.0f};
    const Rect thumb{bar.x, bar.y + k * (bar.h - th), bar.w, th};
    const ID tid = HashBytes("#thumb", id);
    const ItemState st = c.ButtonBehavior(bar, tid);
    if (st.pressed) {
        s.draggingThumb = true;
        s.dragGrab = thumb.Contains(c.In().MousePos()) ? c.In().MousePos().y - thumb.y : th * 0.5f;
        s.velocity = 0.0f;
    }
    if (s.draggingThumb) {
        if (st.held) {
            const float nk = Saturate((c.In().MousePos().y - s.dragGrab - bar.y) / std::max(1.0f, bar.h - th));
            s.scroll = nk * maxScroll;
            s.lastActivity = c.Now();
        } else {
            s.draggingThumb = false;
        }
    }
    const bool hovered = r.Contains(c.In().MousePos()) && c.CurrentLayer() && c.IsLayerHovered(*c.CurrentLayer());
    const bool recent = c.Now() - s.lastActivity < 0.8 || std::abs(s.velocity) > 5.0f;
    const float vis = A().Float(id, kScroll, (hovered || recent || s.draggingThumb) ? 1.0f : 0.0f, motion::kFade);
    const float wide = Hover(tid, st.hovered || st.held);
    const float w = 4.0f + 2.0f * wide;
    dl.RectFilled({bar.Right() - w - 2.0f, thumb.y, w, thumb.h}, Mix(t.textMuted, t.textDim, wide).MulAlpha(0.6f * vis),
                  w * 0.5f);
}

// ---------------------------------------------------------------------------
// Modal
// ---------------------------------------------------------------------------

void OpenModal(std::string_view name) {
    const ID id = HashBytes(name, HashBytes("#modal"));
    C().ModalOpen(id) = true;
}

bool BeginModal(std::string_view name, Vec2 size) {
    Context& c = C();
    const Theme& t = T();
    const ID id = HashBytes(name, HashBytes("#modal"));
    bool& open = c.ModalOpen(id);
    const float k = A().Float(id, kOpen, open ? 1.0f : 0.0f,
                              open ? Motion::Tween(0.30f, Ease::OutExpo) : Motion::Tween(0.16f, Ease::OutCubic));
    if (!open && k <= 0.002f) return false;

    Layer& l = c.BeginLayer(id, LayerKind::Modal);
    l.interactive = open;
    const Rect screen = c.Screen();
    l.rect = open ? screen : Rect{};
    DrawList& dl = l.dl;
    dl.RectFilled(screen, t.overlay.MulAlpha(k));

    const float header = 52.0f, pad = 20.0f;
    const float h = size.y > 0.0f ? size.y : c.Layout().Measured(id, {0, 120}).y + header + pad * 2.0f;
    const Rect r{std::round(screen.Center().x - size.x * 0.5f), std::round(screen.Center().y - h * 0.5f), size.x, h};

    // Click on the dimmed backdrop or Escape closes the modal.
    if (open && c.IsLayerHovered(l) && c.In().MousePressed() && !r.Contains(c.In().MousePos())) open = false;
    if (open && c.In().KeyPressed(0x1B) && c.KeybindWaiting() == 0 && c.FocusId() == 0) open = false;

    dl.PushAlpha(k);
    dl.PushTransform(r.Center(), 0.95f + 0.05f * k, {0.0f, (1.0f - k) * 8.0f});
    dl.Shadow(r, t.shadow, t.radiusXl, 60.0f, {0, 30});
    if (t.glass) dl.Glass(r, t.radiusXl, t.popup.WithAlpha(0.9f));
    else dl.RectFilled(r, t.popup, t.radiusXl);
    dl.RectStroke(r, t.borderStrong, t.radiusXl);
    c.TextAt(dl, FontRole::Medium, t.fontHeading + 1.0f, {r.x + pad, r.y + 27.0f}, t.text, DisplayText(name));
    const Rect cb{r.Right() - 40.0f, r.y + 12.0f, 28.0f, 28.0f};
    const ID cid = HashBytes("#close", id);
    const ItemState st = c.ButtonBehavior(cb, cid);
    const float hv = Hover(cid, st.hovered);
    dl.RectFilled(cb, t.surfaceHover.MulAlpha(hv), t.radiusSm);
    c.IconAt(dl, "x", cb, 15.0f, Mix(t.textMuted, t.text, hv));
    if (st.clicked) open = false;
    dl.RectFilled({r.x, r.y + header - 1.0f, r.w, 1.0f}, t.border);
    dl.PushClip(r);

    LayoutStyle ls;
    ls.gap = 10.0f;
    ls.padding = Padding(pad);
    c.Layout().Push(id, {r.x, r.y + header, r.w, r.h - header}, ls);
    g_modalStack.push_back(id);
    return true;
}

void CloseCurrentModal() {
    if (!g_modalStack.empty()) C().ModalOpen(g_modalStack.back()) = false;
}

void EndModal() {
    if (g_modalStack.empty()) return;
    g_modalStack.pop_back();
    Context& c = C();
    c.Layout().Pop();
    DrawList& dl = DL();
    dl.PopClip();
    dl.PopTransform();
    dl.PopAlpha();
    c.EndLayer();
}

} // namespace silk::ui
