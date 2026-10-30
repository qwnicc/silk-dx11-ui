#include "silk/notify/notify.h"

#include "silk/widgets/context.h"

#include <vector>

namespace silk::Notify {

namespace {

struct Toast {
    ID id = 0;
    Type type = Type::Info;
    std::string title, message;
    float duration = 4.0f;
    float elapsed = 0.0f;
    Spring x;          // horizontal offset from the resting position (px)
    Spring y;          // absolute top position
    float alpha = 0.0f;
    float hover = 0.0f;
    bool closing = false;
    bool placed = false;
};

std::vector<Toast> g_toasts;
uint32_t g_counter = 0;

constexpr float kWidth = 320.0f;
constexpr float kHeight = 66.0f;
constexpr float kGap = 10.0f;
constexpr float kMargin = 20.0f;
constexpr SpringParams kEnter{240.0f, 19.0f, 1.0f};   // slightly bouncy slide-in
constexpr SpringParams kStack{300.0f, 30.0f, 1.0f};   // stack re-flow

Color TypeColor(const Theme& t, Type type) {
    switch (type) {
    case Type::Success: return t.success;
    case Type::Warning: return t.warning;
    case Type::Error: return t.error;
    default: return t.info;
    }
}

const char* TypeIcon(Type type) {
    switch (type) {
    case Type::Success: return "circle-check";
    case Type::Warning: return "triangle-alert";
    case Type::Error: return "circle-x";
    default: return "info";
    }
}

} // namespace

void Push(Type type, std::string title, std::string message, float duration) {
    Toast t;
    t.id = HashInt(++g_counter, 0x70A57u);
    t.type = type;
    t.title = std::move(title);
    t.message = std::move(message);
    t.duration = std::max(0.5f, duration);
    t.x.value = kWidth + kMargin + 40.0f;
    g_toasts.push_back(std::move(t));
}

void Clear() { g_toasts.clear(); }
int Count() { return int(g_toasts.size()); }

void Draw() {
    Context& c = Ctx();
    if (g_toasts.empty()) return;
    const Theme& th = c.Style();
    const float dt = c.Dt();
    const Rect screen = c.Screen();

    Layer& layer = c.BeginLayer(0x70A57000u, LayerKind::Toast);
    DrawList& dl = layer.dl;
    Rect bounds{};

    // Newest toast sits at the bottom; older ones stack upwards.
    float bottom = screen.Bottom() - kMargin;
    for (int i = int(g_toasts.size()) - 1; i >= 0; --i) {
        Toast& t = g_toasts[size_t(i)];
        const float targetY = bottom - kHeight;
        if (!t.placed) {
            t.y.value = targetY;
            t.placed = true;
        }
        if (!t.closing) bottom -= kHeight + kGap;
        t.y.Step(targetY, dt, kStack);
        t.x.Step(t.closing ? kWidth + kMargin + 40.0f : 0.0f, dt, t.closing ? SpringParams{200.0f, 26.0f, 1.0f} : kEnter);

        const Rect r{screen.Right() - kMargin - kWidth + t.x.value, t.y.value, kWidth, kHeight};
        const ItemState st = c.ButtonBehavior(r, t.id);
        t.hover += ((st.hovered ? 1.0f : 0.0f) - t.hover) * (1.0f - std::exp(-dt * 14.0f));
        if (st.clicked) t.closing = true;
        if (!st.hovered && !t.closing) t.elapsed += dt;   // timer pauses while hovered
        if (t.elapsed >= t.duration) t.closing = true;
        const float targetAlpha = t.closing ? 0.0f : 1.0f;
        t.alpha += (targetAlpha - t.alpha) * (1.0f - std::exp(-dt * (t.closing ? 10.0f : 12.0f)));

        if (!t.closing) bounds = bounds.w > 0 ? Rect::FromMinMax({std::min(bounds.x, r.x), std::min(bounds.y, r.y)},
                                                                  {std::max(bounds.Right(), r.Right()), std::max(bounds.Bottom(), r.Bottom())})
                                              : r;

        const Color accent = TypeColor(th, t.type);
        dl.PushAlpha(t.alpha);
        dl.PushTransform(r.Center(), 1.0f + 0.015f * t.hover);
        dl.Shadow(r, th.shadow, th.radiusLg, 30.0f, {0, 12});
        if (th.glass) dl.Glass(r, th.radiusLg, th.popup.WithAlpha(0.86f));
        else dl.RectFilled(r, th.popup, th.radiusLg);
        // Soft tint from the type colour on the left edge.
        dl.RectGradient(r, CornerColors::Horizontal(accent.WithAlpha(0.10f), accent.WithAlpha(0.0f)), th.radiusLg);
        dl.RectStroke(r, Lerp(th.borderStrong, accent.WithAlpha(0.45f), t.hover), th.radiusLg);

        const Rect ic{r.x + 14.0f, r.y + 15.0f, 30.0f, 30.0f};
        dl.Shadow(ic, accent.WithAlpha(0.45f), 15.0f, 12.0f);
        dl.RectFilled(ic, accent.WithAlpha(0.16f), 15.0f);
        c.IconAt(dl, TypeIcon(t.type), ic, 16.0f, accent);
        c.TextAt(dl, FontRole::Medium, th.fontBody, {r.x + 56.0f, r.y + 23.0f}, th.text, t.title);
        c.TextAt(dl, FontRole::Regular, th.fontSmall, {r.x + 56.0f, r.y + 42.0f}, th.textDim, t.message);
        c.IconAt(dl, "x", {r.Right() - 30.0f, r.y + 8.0f, 22.0f, 22.0f}, 12.0f, th.textMuted.MulAlpha(t.hover));

        // Lifetime bar.
        const float left = 1.0f - Saturate(t.elapsed / t.duration);
        dl.PushClip({r.x, r.y, r.w, r.h});
        const Rect bar{r.x + 14.0f, r.Bottom() - 6.0f, (r.w - 28.0f) * left, 2.0f};
        dl.RectFilled({r.x + 14.0f, bar.y, r.w - 28.0f, 2.0f}, th.surfaceActive, 1.0f);
        dl.Shadow(bar, accent.WithAlpha(0.5f), 1.0f, 6.0f);
        dl.RectFilled(bar, accent, 1.0f);
        dl.PopClip();
        dl.PopTransform();
        dl.PopAlpha();
    }
    layer.rect = bounds;
    c.EndLayer();

    std::erase_if(g_toasts, [](const Toast& t) { return t.closing && t.alpha < 0.02f && t.x.value > kWidth * 0.8f; });
}

} // namespace silk::Notify
