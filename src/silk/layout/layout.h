#pragma once
// Flex-like layout for the immediate-mode API.
//
// A stack of frames, each either a column or a row with gap, padding,
// cross-axis alignment and main-axis justification. Widgets ask the current
// frame for their rectangle via Next(). Sizes that are only known after the
// content was emitted (row widths for justify, row heights for centring) are
// taken from the previous frame, keyed by the frame's ID — the "retained"
// half of the hybrid design.

#include "silk/core/id.h"
#include "silk/core/math.h"

#include <unordered_map>
#include <vector>

namespace silk {

enum class Axis : uint8_t { Column, Row };
enum class Align : uint8_t { Start, Center, End, Stretch };

struct Padding {
    float left = 0, top = 0, right = 0, bottom = 0;
    constexpr Padding() = default;
    constexpr Padding(float all) : left(all), top(all), right(all), bottom(all) {}
    constexpr Padding(float h, float v) : left(h), top(v), right(h), bottom(v) {}
    constexpr Padding(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}
};

struct LayoutStyle {
    Axis axis = Axis::Column;
    float gap = 8.0f;
    Padding padding;
    Align align = Align::Stretch;   // cross axis
    Align justify = Align::Start;   // main axis (rows: Start/Center/End)
    int columns = 0;                // rows: >0 splits width into equal columns
    float height = 0.0f;            // rows: fixed height (0 = previous frame's content height)
};

struct LayoutFrame {
    ID id = 0;
    Rect rect;            // inner rect (after padding)
    LayoutStyle style;
    Vec2 cursor;
    Vec2 maxPos;          // furthest extent reached by items
    int items = 0;
    float justifyOffset = 0.0f;
};

class LayoutStack {
public:
    void Reset() { frames_.clear(); }
    void Push(ID id, Rect outer, const LayoutStyle& style);
    LayoutFrame Pop();
    bool Empty() const { return frames_.empty(); }
    LayoutFrame& Top() { return frames_.back(); }
    const LayoutFrame& Top() const { return frames_.back(); }

    // Allocates the next item. Non-positive sizes mean "fill the available
    // space" along that axis.
    Rect Next(Vec2 size);
    // Space left in the current frame along both axes.
    Vec2 Available() const;
    // Adds empty space along the main axis.
    void Space(float amount);

    // Content size of frame `id` measured during the previous frame.
    Vec2 Measured(ID id, Vec2 fallback = {}) const;

private:
    std::vector<LayoutFrame> frames_;
    std::unordered_map<ID, Vec2> measured_;
};

} // namespace silk
