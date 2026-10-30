// Color picker: swatch in a settings row that opens an HSV + alpha popup.

#include "silk/widgets/widgets_internal.h"

#include <cstdio>
#include <unordered_map>

namespace silk::ui {

using namespace detail;

namespace {

struct HsvCache {
    float h = 0, s = 0, v = 0;
    Color last;     // colour the cache was derived from
};
std::unordered_map<ID, HsvCache> g_hsv;

// Keeps hue/saturation stable when the colour becomes grey or black.
HsvCache& SyncHsv(ID id, const Color& c) {
    HsvCache& h = g_hsv[id];
    if (!(h.last == c)) {
        float nh, ns, nv;
        RgbToHsv(c, nh, ns, nv);
        if (nv > 0.001f && ns > 0.001f) h.h = nh;
        if (nv > 0.001f) h.s = ns;
        h.v = nv;
        h.last = c;
    }
    return h;
}

void Swatch(DrawList& dl, Rect r, Color c, float radius) {
    dl.Checker(r, radius, 5.0f);
    dl.RectGradient(r, CornerColors::Horizontal(c.WithAlpha(1.0f), c), radius);
    dl.RectStroke(r, Color(1, 1, 1, 0.12f), radius);
}

} // namespace

bool ColorEdit(std::string_view label, Color* color, bool alpha) {
    Context& c = C();
    const Theme& t = T();
    const ID id = c.GetID(label);
    const ID popupId = HashBytes("##color", id);
    const Rect row = NextRect({0, t.rowHeight});
    const Rect sw{row.Right() - 44.0f, row.Center().y - 11.0f, 44.0f, 22.0f};
    const ItemState st = c.ButtonBehavior(sw, id);
    PopupState& p = c.Popup(popupId);
    if (st.pressed) {
        if (p.open) c.ClosePopup(popupId);
        else if (!c.PopupJustClosed(popupId)) c.OpenPopup(popupId);
    }
    const float hover = Hover(id, st.hovered);
    const float open = A().Float(id, kChevron, p.open ? 1.0f : 0.0f, motion::kHover);

    DrawList& dl = DL();
    dl.PushAlpha(EnabledAlpha(id));
    RowLabel(row, label, t.textDim);
    char hex[16];
    std::snprintf(hex, sizeof(hex), alpha ? "#%02X%02X%02X%02X" : "#%02X%02X%02X", int(color->r * 255 + 0.5f),
                  int(color->g * 255 + 0.5f), int(color->b * 255 + 0.5f), int(color->a * 255 + 0.5f));
    const float hw = c.TextWidth(FontRole::Mono, t.fontSmall, hex);
    c.TextAt(dl, FontRole::Mono, t.fontSmall, {sw.x - hw - 10.0f, row.Center().y}, t.textMuted, hex);
    dl.PushTransform(sw.Center(), 1.0f + 0.05f * hover);
    dl.Shadow(sw, color->WithAlpha(0.45f * color->a * std::max(hover, open)), 6, 14);
    Swatch(dl, sw, *color, 6.0f);
    FocusRing(dl, sw, 6.0f, open);
    dl.PopTransform();
    dl.PopAlpha();

    bool changed = false;
    Rect pr;
    const Vec2 size{232.0f, alpha ? 236.0f : 210.0f};
    const Rect anchor{sw.Right() - size.x, sw.y, size.x, sw.h};
    if (BeginPopupLayer(popupId, anchor, size, pr)) {
        HsvCache& hsv = SyncHsv(id, *color);
        DrawList& pdl = DL();
        const Rect svR{pr.x + 12, pr.y + 12, pr.w - 50, 150};
        const Rect hueR{svR.Right() + 12, svR.y, 14, svR.h};
        const Rect alphaR{pr.x + 12, svR.Bottom() + 14, pr.w - 24, 12};
        const float infoY = (alpha ? alphaR.Bottom() : svR.Bottom()) + 14.0f;

        // Interaction (each field is its own item so drags keep ownership).
        const ItemState sv = c.ButtonBehavior(svR, HashBytes("sv", id));
        const ItemState hu = c.ButtonBehavior(hueR.Expand(3, 0), HashBytes("hue", id));
        ItemState al;
        if (alpha) al = c.ButtonBehavior(alphaR.Expand(0, 3), HashBytes("alpha", id));
        const Vec2 m = c.In().MousePos();
        if (sv.held) {
            hsv.s = Saturate((m.x - svR.x) / svR.w);
            hsv.v = 1.0f - Saturate((m.y - svR.y) / svR.h);
        }
        if (hu.held) hsv.h = Saturate((m.y - hueR.y) / hueR.h) * 0.9999f;
        float a = color->a;
        if (alpha && al.held) a = Saturate((m.x - alphaR.x) / alphaR.w);
        if (sv.held || hu.held || al.held) {
            const Color nc = HsvToRgb(hsv.h, hsv.s, hsv.v, a);
            if (!(nc == *color)) {
                *color = nc;
                hsv.last = nc;
                changed = true;
            }
        }

        // Fields
        pdl.SatValField(svR, HsvToRgb(hsv.h, 1, 1), t.radiusSm);
        pdl.RectStroke(svR, Color(1, 1, 1, 0.08f), t.radiusSm);
        pdl.HueBar(hueR, 7.0f);
        if (alpha) pdl.AlphaBar(alphaR, *color, 6.0f, 6.0f);

        // Handles
        const Vec2 svp{svR.x + hsv.s * svR.w, svR.y + (1.0f - hsv.v) * svR.h};
        const float svPress = Press(HashBytes("sv", id), sv.held);
        const float hr = 7.0f + 2.0f * svPress;
        pdl.Shadow({svp.x - hr, svp.y - hr, hr * 2, hr * 2}, Color(0, 0, 0, 0.45f), hr, 6.0f);
        pdl.Circle(svp, hr, color->WithAlpha(1.0f));
        pdl.CircleStroke(svp, hr, Color(1, 1, 1, 1), 2.0f);

        const float hy = hueR.y + hsv.h * hueR.h;
        const Rect hk{hueR.x - 2, hy - 4, hueR.w + 4, 8};
        pdl.Shadow(hk, Color(0, 0, 0, 0.45f), 4, 5);
        pdl.RectFilled(hk, HsvToRgb(hsv.h, 1, 1), 4);
        pdl.RectStroke(hk, Color(1, 1, 1, 1), 4, 2.0f);
        if (alpha) {
            const float ax = alphaR.x + color->a * alphaR.w;
            const Rect ak{ax - 4, alphaR.y - 2, 8, alphaR.h + 4};
            pdl.Shadow(ak, Color(0, 0, 0, 0.45f), 4, 5);
            pdl.RectFilled(ak, Color(1, 1, 1, 1), 4);
        }

        // Preview + hex readout
        const Rect prev{pr.x + 12, infoY, 44, 26};
        Swatch(pdl, prev, *color, 6.0f);
        c.TextAt(pdl, FontRole::Mono, t.fontBody, {prev.Right() + 12, prev.Center().y}, t.text, hex);
        char rgb[48];
        std::snprintf(rgb, sizeof(rgb), "%d %d %d", int(color->r * 255 + 0.5f), int(color->g * 255 + 0.5f),
                      int(color->b * 255 + 0.5f));
        const float rw = c.TextWidth(FontRole::Mono, t.fontSmall, rgb);
        c.TextAt(pdl, FontRole::Mono, t.fontSmall, {pr.Right() - 12 - rw, prev.Center().y}, t.textMuted, rgb);
        EndPopupLayer();
    }
    return changed;
}

} // namespace silk::ui
