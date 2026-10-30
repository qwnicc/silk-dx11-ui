#pragma once
// Theme: colours, radii, spacing and typography in one struct.
//
// ThemeManager interpolates between themes so switching at runtime fades
// every colour smoothly instead of snapping.

#include "silk/core/math.h"

#include <string>

namespace silk {

struct Theme {
    std::string name = "Dark";

    // Surfaces
    Color background;      // app background behind windows
    Color window;          // window body
    Color windowAlt;       // sidebar / headers
    Color popup;           // dropdowns, tooltips, toasts
    Color panel;           // raised content panel next to the sidebar
    Color card;            // cards inside the panel
    Color surface;         // control fill (inputs, secondary buttons)
    Color surfaceHover;
    Color surfaceActive;
    Color border;          // 1px hairlines
    Color borderStrong;

    // Text
    Color text;
    Color textDim;
    Color textMuted;
    Color textOnAccent;

    // Accent (gradient from accent -> accent2) and semantic colours
    Color accent;
    Color accent2;
    Color success, warning, error, info;

    Color shadow;
    Color overlay;         // modal backdrop dim
    float windowOpacity = 0.86f; // body alpha when glass is enabled
    bool glass = true;

    // Metrics (4px grid)
    float radiusSm = 6.0f;
    float radius = 8.0f;
    float radiusLg = 12.0f;
    float radiusXl = 16.0f;
    float spacing = 8.0f;       // gap between items
    float padding = 16.0f;      // window / panel padding
    float controlHeight = 32.0f;
    float rowHeight = 36.0f;    // settings rows (label + control)

    // Typography
    float fontSmall = 12.0f;
    float fontBody = 13.5f;
    float fontHeading = 15.0f;
    float fontTitle = 20.0f;

    static Theme Dark();
    static Theme Light();
    static Theme Midnight();

    // Component-wise interpolation of every colour and metric.
    static Theme Lerp(const Theme& a, const Theme& b, float t);
};

class ThemeManager {
public:
    ThemeManager() : current_(Theme::Dark()), from_(current_), to_(current_) {}

    // Starts a transition to `t` (or applies it instantly).
    void Set(const Theme& t, bool animate = true);
    // Changes only the accent colours (animated).
    void SetAccent(Color a, Color b);
    void Update(float dt);

    const Theme& Get() const { return current_; }
    const Theme& Target() const { return to_; }
    bool Transitioning() const { return t_ < 1.0f; }

private:
    Theme current_, from_, to_;
    float t_ = 1.0f;
    float duration_ = 0.45f;
};

} // namespace silk
