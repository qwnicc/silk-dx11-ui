#pragma once
// HUD overlay: drawn above the menu windows and active while the menu is
// closed. Elements (module list, FPS counter, clock, keybind list) can be
// dragged with the mouse while the menu is open; their positions are stored
// as screen fractions so they survive resolution changes and are saved in
// the config.
//
// The watermark lives here too: a segmented bar (logo, name, user, FPS,
// ping, time) whose width animates when its text changes.

#include "silk/config/json.h"
#include "silk/core/math.h"

#include <string>
#include <vector>

namespace silk {

class Context;

struct WatermarkConfig {
    bool visible = true;
    int corner = 0;            // 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
    bool showUser = true;
    bool showFps = true;
    bool showPing = true;
    bool showTime = true;
    float opacity = 1.0f;
    std::string brand = "silk";
    std::string user = "developer";
    std::string icon = "sparkles";
};

class Hud {
public:
    struct Element {
        std::string id;
        Vec2 anchor;           // position as a fraction of the screen
        bool visible = true;
        Rect rect;             // last drawn rect (screen px)
        bool dragging = false;
        Vec2 grab;
    };

    Hud();

    // Modules shown in the active list (call whenever state changes or every frame).
    void SetModule(const std::string& name, bool active);
    // Keybind panel rows: name + key label.
    void SetKeybinds(std::vector<std::pair<std::string, std::string>> rows) { keybinds_ = std::move(rows); }

    Element* Find(const std::string& id);
    WatermarkConfig& Watermark() { return watermark_; }
    void SetPing(int ms) { ping_ = ms; }

    // Draws all HUD elements and the watermark. Call once per frame.
    void Draw(Context& c);

    json::Value Save() const;
    void Load(const json::Value& v);

private:
    struct Module {
        std::string name;
        bool active = false;
        float t = 0.0f;        // enter/exit progress (spring)
        float vel = 0.0f;
        float y = 0.0f, yVel = 0.0f;
        bool placed = false;
    };

    void BeginElement(Context& c, Element& e, Vec2 size, bool rightAligned);
    void DrawModules(Context& c, Element& e);
    void DrawFps(Context& c, Element& e);
    void DrawClock(Context& c, Element& e);
    void DrawKeybinds(Context& c, Element& e);
    void DrawWatermark(Context& c);

    std::vector<Element> elements_;
    std::vector<Module> modules_;
    std::vector<std::pair<std::string, std::string>> keybinds_;
    WatermarkConfig watermark_;
    int ping_ = 0;
    Rect layerBounds_;
};

} // namespace silk
