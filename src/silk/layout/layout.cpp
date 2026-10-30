#include "silk/layout/layout.h"

namespace silk {

void LayoutStack::Push(ID id, Rect outer, const LayoutStyle& style) {
    LayoutFrame f;
    f.id = id;
    f.style = style;
    const Padding& p = style.padding;
    f.rect = {outer.x + p.left, outer.y + p.top, std::max(0.0f, outer.w - p.left - p.right),
              std::max(0.0f, outer.h - p.top - p.bottom)};
    f.cursor = f.rect.Pos();
    f.maxPos = f.rect.Pos();

    // Main-axis justification uses last frame's measured content width.
    if (style.axis == Axis::Row && style.justify != Align::Start && style.columns == 0) {
        const float used = Measured(id, {f.rect.w, 0}).x;
        const float freeSpace = std::max(0.0f, f.rect.w - used);
        f.justifyOffset = style.justify == Align::Center ? freeSpace * 0.5f : freeSpace;
        f.cursor.x += f.justifyOffset;
    }
    frames_.push_back(f);
}

LayoutFrame LayoutStack::Pop() {
    LayoutFrame f = frames_.back();
    frames_.pop_back();
    const Vec2 content = f.maxPos - f.rect.Pos() - Vec2(f.style.axis == Axis::Row ? f.justifyOffset : 0.0f, 0.0f);
    if (f.id) measured_[f.id] = content;
    return f;
}

Rect LayoutStack::Next(Vec2 size) {
    LayoutFrame& f = frames_.back();
    const LayoutStyle& s = f.style;
    Rect r;
    if (s.axis == Axis::Column) {
        const float w = size.x > 0.0f ? size.x : f.rect.w;
        const float h = size.y > 0.0f ? size.y : std::max(0.0f, f.rect.Bottom() - f.cursor.y);
        float x = f.rect.x;
        if (size.x > 0.0f) {
            if (s.align == Align::Center) x += (f.rect.w - w) * 0.5f;
            else if (s.align == Align::End) x += f.rect.w - w;
        }
        r = {x, f.cursor.y, w, h};
        f.cursor.y += h + s.gap;
    } else {
        float w;
        if (size.x > 0.0f) w = size.x;
        else if (s.columns > 0) w = (f.rect.w - s.gap * float(s.columns - 1)) / float(s.columns);
        else w = std::max(0.0f, f.rect.Right() - f.cursor.x);
        const float h = size.y > 0.0f ? size.y : f.rect.h;
        float y = f.rect.y;
        if (s.align == Align::Center) y += (f.rect.h - h) * 0.5f;
        else if (s.align == Align::End) y += f.rect.h - h;
        r = {f.cursor.x, y, w, h};
        f.cursor.x += w + s.gap;
    }
    f.maxPos.x = std::max(f.maxPos.x, r.Right());
    f.maxPos.y = std::max(f.maxPos.y, r.Bottom());
    ++f.items;
    return r;
}

Vec2 LayoutStack::Available() const {
    const LayoutFrame& f = frames_.back();
    return {std::max(0.0f, f.rect.Right() - f.cursor.x), std::max(0.0f, f.rect.Bottom() - f.cursor.y)};
}

void LayoutStack::Space(float amount) {
    LayoutFrame& f = frames_.back();
    if (f.style.axis == Axis::Column) f.cursor.y += amount;
    else f.cursor.x += amount;
}

Vec2 LayoutStack::Measured(ID id, Vec2 fallback) const {
    const auto it = measured_.find(id);
    return it == measured_.end() ? fallback : it->second;
}

} // namespace silk
