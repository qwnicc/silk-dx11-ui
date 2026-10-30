#pragma once
// Context: the shared state behind the immediate-mode API.
//
// Owns the renderer, fonts, input, animation store, theme and layout, plus
// the retained per-widget state (windows, popups, scroll areas, text edits).
// Every window/popup/modal/tooltip is a Layer with its own DrawList; layers
// are z-sorted at the end of the frame and hover is resolved against the
// layer rectangles of the previous frame, so the top-most layer under the
// mouse is the only one whose widgets react.

#include "silk/anim/anim.h"
#include "silk/core/id.h"
#include "silk/core/input.h"
#include "silk/core/time.h"
#include "silk/layout/layout.h"
#include "silk/render/draw_list.h"
#include "silk/render/font.h"
#include "silk/render/renderer.h"
#include "silk/theme/theme.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct HWND__;
struct ID3D11RenderTargetView;

namespace silk {

enum class LayerKind : uint8_t { Window = 0, Hud = 1, Modal = 2, Popup = 3, Toast = 4, Tooltip = 5 };

struct Layer {
    ID id = 0;
    LayerKind kind = LayerKind::Window;
    DrawList dl;
    Rect rect;            // hit area submitted this frame
    Rect hitRect;         // hit area from the previous frame (used for hover)
    int order = 0;        // z within the kind
    uint64_t lastFrame = 0;
    bool interactive = true;
};

struct WindowState {
    Vec2 pos, size;
    bool initialized = false;
    float openT = 0.0f;   // 0 = closed, 1 = open (animated)
    bool dragging = false;
    bool resizing = false;
    Vec2 dragOffset;
};

struct PopupState {
    bool open = false;
    float t = 0.0f;           // open animation
    uint64_t closedFrame = 0; // frame in which it was closed by an outside click
    uint64_t openSeq = 0;
};

struct ScrollState {
    float scroll = 0.0f;
    float velocity = 0.0f;
    float contentHeight = 0.0f;
    float viewHeight = 0.0f;
    float barAlpha = 0.0f;
    double lastActivity = -10.0;
    bool draggingThumb = false;
    float dragGrab = 0.0f;
};

struct TextEditState {
    ID id = 0;
    size_t cursor = 0, anchor = 0;
    float scrollX = 0.0f;
    double blinkStart = 0.0;
    bool selectingWithMouse = false;
};

enum class FontRole : uint8_t { Regular, Medium, Bold, Mono };

struct ItemState {
    bool hovered = false;
    bool held = false;
    bool pressed = false;   // mouse went down on the item this frame
    bool clicked = false;   // released over the item
};

class Context {
public:
    Context();
    ~Context();

    // --- lifecycle ---
    bool Init(ID3D11Device* device, ID3D11DeviceContext* ctx, const std::string& assetDir);
    bool HandleMessage(HWND__* hwnd, unsigned msg, uintptr_t wp, intptr_t lp);
    // fixedDt > 0 overrides the real clock (deterministic captures).
    void NewFrame(int width, int height, float dpi, float fixedDt = -1.0f);
    void Render(ID3D11RenderTargetView* rtv);

    // --- subsystems ---
    Renderer& Gfx() { return renderer_; }
    FontSystem& Fonts() { return fonts_; }
    Input& In() { return input_; }
    const Input& In() const { return input_; }
    AnimStore& Anim() { return anim_; }
    ThemeManager& Themes() { return theme_; }
    const Theme& Style() const { return theme_.Get(); }
    LayoutStack& Layout() { return layout_; }
    const Clock& Time() const { return clock_; }
    float Dt() const { return dt_; }
    double Now() const { return now_; }
    uint64_t FrameIndex() const { return frame_; }
    Rect Screen() const { return screen_; }
    float Dpi() const { return dpi_; }

    // Draw list behind all UI (app background / scene).
    DrawList& Background() { return background_; }
    // Draw list of the layer currently being built.
    DrawList& DL() { return cur_->dl; }
    Layer* CurrentLayer() { return cur_; }

    // --- layers ---
    Layer& BeginLayer(ID id, LayerKind kind);
    void EndLayer();
    void BringToFront(Layer& l);
    bool IsLayerHovered(const Layer& l) const { return hoveredLayer_ == l.id; }
    ID HoveredLayer() const { return hoveredLayer_; }

    // --- IDs ---
    ID GetID(std::string_view label) const { return HashBytes(label, idStack_.back()); }
    ID GetID(int v) const { return HashInt(uint32_t(v), idStack_.back()); }
    void PushID(ID id) { idStack_.push_back(id); }
    void PushID(std::string_view s) { idStack_.push_back(GetID(s)); }
    void PopID() { if (idStack_.size() > 1) idStack_.pop_back(); }

    // --- interaction ---
    bool ItemHoverable(const Rect& r, ID id);
    ItemState ButtonBehavior(const Rect& r, ID id);
    void SetActive(ID id) { activeId_ = id; activeAlive_ = true; }
    void ClearActive() { activeId_ = 0; }
    ID ActiveId() const { return activeId_; }
    ID HotId() const { return hotId_; }
    void SetFocus(ID id) { focusId_ = id; }
    ID FocusId() const { return focusId_; }
    void RequestKeyboard() { keyboardCaptured_ = true; }
    bool Disabled() const { return disabledDepth_ > 0; }
    void PushDisabled(bool d) { disabledStack_.push_back(d); if (d) ++disabledDepth_; }
    void PopDisabled() {
        if (disabledStack_.empty()) return;
        if (disabledStack_.back()) --disabledDepth_;
        disabledStack_.pop_back();
    }

    // Last submitted item (for tooltips etc.).
    void SetLastItem(ID id, Rect r, bool hovered) { lastItem_ = id; lastItemRect_ = r; lastItemHovered_ = hovered; }
    ID LastItem() const { return lastItem_; }
    Rect LastItemRect() const { return lastItemRect_; }
    bool LastItemHovered() const { return lastItemHovered_; }

    // --- popups ---
    PopupState& Popup(ID id) { return popups_[id]; }
    void OpenPopup(ID id);
    void ClosePopup(ID id);
    bool PopupJustClosed(ID id) const;
    bool AnyPopupOpen() const { return !popupStack_.empty(); }

    // --- retained widget state ---
    WindowState& Window(ID id) { return windows_[id]; }
    ScrollState& Scroll(ID id) { return scrolls_[id]; }
    TextEditState& TextEdit() { return textEdit_; }
    bool& ModalOpen(ID id) { return modals_[id]; }
    ID& KeybindWaiting() { return keybindWaiting_; }

    // --- tooltip ---
    void SetTooltip(std::string text);

    // --- drawing helpers ---
    Font& GetFont(FontRole r);
    float TextWidth(FontRole r, float size, std::string_view text);
    // Text vertically centred on `centerY` using the cap height (optically centred).
    void TextAt(DrawList& dl, FontRole r, float size, Vec2 leftCenter, Color c, std::string_view text);
    void TextCentered(DrawList& dl, FontRole r, float size, Rect box, Color c, std::string_view text);
    void IconAt(DrawList& dl, std::string_view name, Rect box, float size, Color c);

    // Global UI flags
    bool menuOpen = true;       // HUD elements become draggable while the menu is open

private:
    void UpdateHover();
    void HandlePopupClicks();
    void DrawTooltip();

    Renderer renderer_;
    FontSystem fonts_;
    Input input_;
    Clock clock_;
    AnimStore anim_;
    ThemeManager theme_;
    LayoutStack layout_;
    DrawList background_;

    std::unordered_map<ID, std::unique_ptr<Layer>> layers_;
    std::vector<Layer*> layerStack_;
    Layer* cur_ = nullptr;
    Layer* rootLayer_ = nullptr;
    ID hoveredLayer_ = 0;
    int zCounter_ = 0;

    std::vector<ID> idStack_;
    ID hotId_ = 0, activeId_ = 0, focusId_ = 0;
    bool activeAlive_ = false;
    bool keyboardCaptured_ = false;
    int disabledDepth_ = 0;
    std::vector<bool> disabledStack_;
    ID lastItem_ = 0;
    Rect lastItemRect_;
    bool lastItemHovered_ = false;

    std::unordered_map<ID, PopupState> popups_;
    std::vector<ID> popupStack_;
    uint64_t popupSeq_ = 0;

    std::unordered_map<ID, WindowState> windows_;
    std::unordered_map<ID, ScrollState> scrolls_;
    std::unordered_map<ID, bool> modals_;
    TextEditState textEdit_;
    ID keybindWaiting_ = 0;

    // tooltip
    ID tipItem_ = 0;
    double tipHoverStart_ = 0.0;
    std::string tipText_;
    bool tipRequested_ = false;
    float tipAlpha_ = 0.0f;
    Vec2 tipPos_;

    Rect screen_;
    float dpi_ = 1.0f;
    float dt_ = 1.0f / 60.0f;
    double now_ = 0.0;
    uint64_t frame_ = 0;
};

// Current context used by the free-function API in widgets.h.
Context& Ctx();
void SetCurrentContext(Context* ctx);

} // namespace silk
