#include "silk/widgets/context.h"

#include <d3d11.h>

#include <algorithm>

namespace silk {

namespace {
Context* g_ctx = nullptr;
constexpr ID kRootLayer = 0x524F4F54u;   // "ROOT"
constexpr ID kTooltipLayer = 0x54495053u; // "TIPS"
} // namespace

Context& Ctx() { return *g_ctx; }
void SetCurrentContext(Context* ctx) { g_ctx = ctx; }

Context::Context() {
    idStack_.push_back(kFnvOffset);
    if (!g_ctx) g_ctx = this;
}

Context::~Context() {
    if (g_ctx == this) g_ctx = nullptr;
}

bool Context::Init(ID3D11Device* device, ID3D11DeviceContext* ctx, const std::string& assetDir) {
    return renderer_.Init(device, ctx) && fonts_.Init(device, assetDir);
}

bool Context::HandleMessage(HWND__* hwnd, unsigned msg, uintptr_t wp, intptr_t lp) {
    return input_.HandleMessage(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void Context::NewFrame(int width, int height, float dpi, float fixedDt) {
    const float realDt = clock_.Tick();
    dt_ = fixedDt > 0.0f ? fixedDt : realDt;
    now_ += dt_;
    ++frame_;
    dpi_ = dpi;
    input_.SetDpiScale(dpi);
    screen_ = {0, 0, float(width) / dpi, float(height) / dpi};

    anim_.NewFrame(dt_);
    theme_.Update(dt_);
    layout_.Reset();
    idStack_.assign(1, kFnvOffset);
    background_.Clear(screen_);

    // Drop a stale active item (its widget was not submitted last frame).
    if (activeId_ && !activeAlive_) activeId_ = 0;
    activeAlive_ = false;
    hotId_ = 0;
    keyboardCaptured_ = false;
    tipRequested_ = false;
    disabledDepth_ = 0;
    disabledStack_.clear();

    UpdateHover();
    HandlePopupClicks();
    if (input_.MousePressed() || input_.MousePressed(MouseButton::Right)) {
        // Clicking anywhere drops keyboard focus; the clicked widget re-takes it.
        focusId_ = 0;
    }

    layerStack_.clear();
    rootLayer_ = &BeginLayer(kRootLayer, LayerKind::Window);
    rootLayer_->order = -1000000;
    rootLayer_->interactive = false;
}

void Context::UpdateHover() {
    std::vector<Layer*> visible;
    for (auto& [id, l] : layers_)
        if (l->lastFrame + 1 == frame_ && l->interactive) visible.push_back(l.get());
    std::sort(visible.begin(), visible.end(), [](const Layer* a, const Layer* b) {
        return a->kind != b->kind ? a->kind > b->kind : a->order > b->order;
    });
    hoveredLayer_ = 0;
    const Vec2 m = input_.MousePos();
    for (Layer* l : visible) {
        if (l->hitRect.Contains(m)) {
            hoveredLayer_ = l->id;
            break;
        }
    }
    // While dragging something the owning layer keeps the hover.
    if (input_.MousePressed() && hoveredLayer_) {
        Layer& l = *layers_[hoveredLayer_];
        if (l.kind == LayerKind::Window) BringToFront(l);
    }
}

void Context::HandlePopupClicks() {
    if (popupStack_.empty()) return;
    if (!input_.MousePressed() && !input_.MousePressed(MouseButton::Right)) return;
    // Keep popups up to (and including) the hovered one; close the rest.
    size_t keep = 0;
    for (size_t i = 0; i < popupStack_.size(); ++i)
        if (popupStack_[i] == hoveredLayer_) keep = i + 1;
    while (popupStack_.size() > keep) {
        const ID id = popupStack_.back();
        PopupState& p = popups_[id];
        p.open = false;
        p.closedFrame = frame_;
        popupStack_.pop_back();
    }
}

void Context::DrawTooltip() {
    const bool show = tipRequested_ && (now_ - tipHoverStart_) > 0.45;
    if (!tipRequested_) tipItem_ = 0;
    tipAlpha_ = anim_.Float(kTooltipLayer, 0, show ? 1.0f : 0.0f, Motion::Tween(show ? 0.16f : 0.10f));
    if (tipAlpha_ <= 0.01f || tipText_.empty()) return;
    if (show) tipPos_ = input_.MousePos();

    const Theme& t = Style();
    Layer& l = BeginLayer(kTooltipLayer, LayerKind::Tooltip);
    l.interactive = false;
    DrawList& dl = l.dl;
    const float fs = t.fontSmall;
    const float w = TextWidth(FontRole::Regular, fs, tipText_) + 20.0f;
    const float h = 26.0f;
    Rect r{tipPos_.x + 14.0f, tipPos_.y + 18.0f, w, h};
    r.x = std::min(r.x, screen_.Right() - w - 6.0f);
    r.y = std::min(r.y, screen_.Bottom() - h - 6.0f);
    const float lift = (1.0f - tipAlpha_) * 4.0f;
    dl.PushAlpha(tipAlpha_);
    dl.PushTransform({}, 1.0f, {0.0f, lift});
    dl.Shadow(r, t.shadow, t.radiusSm, 16, {0, 6});
    dl.RectFilled(r, t.popup, t.radiusSm);
    dl.RectStroke(r, t.borderStrong, t.radiusSm);
    TextAt(dl, FontRole::Regular, fs, {r.x + 10, r.Center().y}, t.text, tipText_);
    dl.PopTransform();
    dl.PopAlpha();
    EndLayer();
}

void Context::Render(ID3D11RenderTargetView* rtv) {
    // Close the root layer and anything left open by mistake.
    while (!layerStack_.empty()) EndLayer();
    DrawTooltip();

    std::vector<Layer*> visible;
    for (auto& [id, l] : layers_) {
        if (l->lastFrame == frame_) {
            l->hitRect = l->rect;
            visible.push_back(l.get());
        }
    }
    std::sort(visible.begin(), visible.end(), [](const Layer* a, const Layer* b) {
        return a->kind != b->kind ? a->kind < b->kind : a->order < b->order;
    });

    std::vector<DrawList*> lists;
    lists.reserve(visible.size() + 1);
    lists.push_back(&background_);
    for (Layer* l : visible) lists.push_back(&l->dl);

    // Tell the host which input the UI consumed this frame.
    input_.wantCaptureMouse = hoveredLayer_ != 0 || activeId_ != 0;
    input_.wantCaptureKeyboard = keyboardCaptured_;

    fonts_.Upload(renderer_.Context());
    renderer_.Render(lists, rtv, dpi_, float(now_));
    input_.EndFrame();
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

Layer& Context::BeginLayer(ID id, LayerKind kind) {
    auto& slot = layers_[id];
    if (!slot) {
        slot = std::make_unique<Layer>();
        slot->id = id;
        slot->order = ++zCounter_;
    }
    Layer& l = *slot;
    l.kind = kind;
    if (l.lastFrame != frame_) {
        l.lastFrame = frame_;
        l.dl.Clear(screen_);
        l.rect = {};
    }
    layerStack_.push_back(&l);
    cur_ = &l;
    return l;
}

void Context::EndLayer() {
    if (layerStack_.empty()) return;
    layerStack_.pop_back();
    cur_ = layerStack_.empty() ? rootLayer_ : layerStack_.back();
}

void Context::BringToFront(Layer& l) { l.order = ++zCounter_; }

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

bool Context::ItemHoverable(const Rect& r, ID id) {
    if (disabledDepth_ > 0 || !cur_) return false;
    if (hoveredLayer_ != cur_->id) return false;
    if (activeId_ && activeId_ != id) return false;
    const Vec2 m = input_.MousePos();
    return cur_->dl.CurrentClip().Contains(m) && r.Contains(m);
}

ItemState Context::ButtonBehavior(const Rect& r, ID id) {
    ItemState s;
    s.hovered = ItemHoverable(r, id);
    if (s.hovered) hotId_ = id;
    if (s.hovered && input_.MousePressed()) {
        activeId_ = id;
        s.pressed = true;
    }
    if (activeId_ == id) {
        activeAlive_ = true;
        s.held = input_.MouseDown();
        if (!input_.MouseDown()) {
            // Released (possibly in the same frame as pressed).
            s.clicked = r.Contains(input_.MousePos()) && cur_ && hoveredLayer_ == cur_->id;
            s.held = false;
            activeId_ = 0;
        }
    }
    SetLastItem(id, r, s.hovered);
    return s;
}

void Context::OpenPopup(ID id) {
    PopupState& p = popups_[id];
    if (p.open) return;
    p.open = true;
    p.openSeq = ++popupSeq_;
    popupStack_.push_back(id);
}

void Context::ClosePopup(ID id) {
    PopupState& p = popups_[id];
    p.open = false;
    std::erase(popupStack_, id);
}

bool Context::PopupJustClosed(ID id) const {
    const auto it = popups_.find(id);
    return it != popups_.end() && it->second.closedFrame == frame_;
}

void Context::SetTooltip(std::string text) {
    if (!lastItemHovered_) return;
    if (tipItem_ != lastItem_) {
        tipItem_ = lastItem_;
        tipHoverStart_ = now_;
    }
    tipRequested_ = true;
    tipText_ = std::move(text);
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

Font& Context::GetFont(FontRole r) {
    switch (r) {
    case FontRole::Medium: return fonts_.Medium();
    case FontRole::Bold: return fonts_.Bold();
    case FontRole::Mono: return fonts_.Mono();
    default: return fonts_.Regular();
    }
}

float Context::TextWidth(FontRole r, float size, std::string_view text) {
    return GetFont(r).Measure(text, size).x;
}

void Context::TextAt(DrawList& dl, FontRole r, float size, Vec2 leftCenter, Color c, std::string_view text) {
    Font& f = GetFont(r);
    dl.TextBaseline(f, size, {leftCenter.x, leftCenter.y + f.CapHeight(size) * 0.5f}, c, text);
}

void Context::TextCentered(DrawList& dl, FontRole r, float size, Rect box, Color c, std::string_view text) {
    const float w = TextWidth(r, size, text);
    TextAt(dl, r, size, {box.Center().x - w * 0.5f, box.Center().y}, c, text);
}

void Context::IconAt(DrawList& dl, std::string_view name, Rect box, float size, Color c) {
    dl.GlyphCentered(fonts_.Icons(), fonts_.IconCodepoint(name), box, size, c);
}

} // namespace silk
