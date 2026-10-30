// Text input (caret, selection, clipboard, scrolling) and Keybind capture.

#include "silk/core/utf8.h"
#include "silk/widgets/widgets_internal.h"

#include <windows.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace silk::ui {

using namespace detail;

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

namespace {

void SetClipboard(std::string_view text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    const std::wstring w = utf8::ToWide(text);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
        if (void* dst = GlobalLock(mem)) {
            memcpy(dst, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

std::string GetClipboard() {
    std::string out;
    if (!OpenClipboard(nullptr)) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* w = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = utf8::FromWide(w);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    // Single-line field: drop line breaks.
    std::erase_if(out, [](char ch) { return ch == '\r' || ch == '\n'; });
    return out;
}

bool IsWordChar(char ch) {
    return (static_cast<uint8_t>(ch) & 0x80) || std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

size_t WordLeft(std::string_view s, size_t i) {
    while (i > 0 && !IsWordChar(s[utf8::Prev(s, i)])) i = utf8::Prev(s, i);
    while (i > 0 && IsWordChar(s[utf8::Prev(s, i)])) i = utf8::Prev(s, i);
    return i;
}

size_t WordRight(std::string_view s, size_t i) {
    while (i < s.size() && !IsWordChar(s[i])) i = utf8::Next(s, i);
    while (i < s.size() && IsWordChar(s[i])) i = utf8::Next(s, i);
    return i;
}

// Core single-line editor drawn into `field`.
bool EditField(ID id, Rect field, std::string* text, std::string_view hint, std::string_view icon, bool* submitted) {
    Context& c = C();
    const Theme& t = T();
    Input& in = c.In();
    TextEditState& es = c.TextEdit();
    Font& font = c.GetFont(FontRole::Regular);
    const float fs = t.fontBody;

    const float iconW = icon.empty() ? 0.0f : 26.0f;
    const Rect area{field.x + 12.0f + iconW, field.y, field.w - 24.0f - iconW, field.h};

    const ItemState st = c.ButtonBehavior(field, id);
    bool focused = c.FocusId() == id;
    if (es.id != id && focused) {
        es = {};
        es.id = id;
    }
    auto hitIndex = [&](float mx) { return font.HitTest(*text, fs, mx - area.x + es.scrollX); };

    if (st.pressed) {
        c.SetFocus(id);
        if (es.id != id) {
            es = {};
            es.id = id;
        }
        focused = true;
        es.cursor = hitIndex(in.MousePos().x);
        if (!in.Shift()) es.anchor = es.cursor;
        if (in.MouseDoubleClicked()) {
            es.anchor = 0;
            es.cursor = text->size();
        }
        es.selectingWithMouse = !in.MouseDoubleClicked();
        es.blinkStart = c.Now();
    }
    if (focused && st.held && es.selectingWithMouse) es.cursor = hitIndex(in.MousePos().x);
    if (!st.held) es.selectingWithMouse = false;

    bool changed = false;
    if (focused) {
        c.RequestKeyboard();
        es.cursor = std::min(es.cursor, text->size());
        es.anchor = std::min(es.anchor, text->size());
        auto hasSel = [&] { return es.cursor != es.anchor; };
        auto selMin = [&] { return std::min(es.cursor, es.anchor); };
        auto selMax = [&] { return std::max(es.cursor, es.anchor); };
        auto deleteSel = [&] {
            if (!hasSel()) return;
            text->erase(selMin(), selMax() - selMin());
            es.cursor = es.anchor = selMin();
            changed = true;
        };
        auto insert = [&](std::string_view s) {
            deleteSel();
            text->insert(es.cursor, s);
            es.cursor += s.size();
            es.anchor = es.cursor;
            changed = true;
        };
        auto move = [&](size_t to) {
            es.cursor = to;
            if (!in.Shift()) es.anchor = to;
        };
        const size_t before = es.cursor + es.anchor * 7919;

        if (!in.Ctrl()) {
            for (char32_t ch : in.Chars()) {
                std::string u;
                utf8::Append(u, uint32_t(ch));
                insert(u);
            }
        }
        const bool ctrl = in.Ctrl();
        if (in.KeyPressed(VK_LEFT)) {
            if (hasSel() && !in.Shift()) move(selMin());
            else move(ctrl ? WordLeft(*text, es.cursor) : utf8::Prev(*text, es.cursor));
        }
        if (in.KeyPressed(VK_RIGHT)) {
            if (hasSel() && !in.Shift()) move(selMax());
            else move(ctrl ? WordRight(*text, es.cursor) : utf8::Next(*text, es.cursor));
        }
        if (in.KeyPressed(VK_HOME)) move(0);
        if (in.KeyPressed(VK_END)) move(text->size());
        if (in.KeyPressed(VK_BACK)) {
            if (hasSel()) deleteSel();
            else if (es.cursor > 0) {
                const size_t from = ctrl ? WordLeft(*text, es.cursor) : utf8::Prev(*text, es.cursor);
                text->erase(from, es.cursor - from);
                es.cursor = es.anchor = from;
                changed = true;
            }
        }
        if (in.KeyPressed(VK_DELETE)) {
            if (hasSel()) deleteSel();
            else if (es.cursor < text->size()) {
                const size_t to = ctrl ? WordRight(*text, es.cursor) : utf8::Next(*text, es.cursor);
                text->erase(es.cursor, to - es.cursor);
                changed = true;
            }
        }
        if (ctrl && in.KeyPressed('A')) { es.anchor = 0; es.cursor = text->size(); }
        if (ctrl && (in.KeyPressed('C') || in.KeyPressed('X')) && hasSel()) {
            SetClipboard(std::string_view(*text).substr(selMin(), selMax() - selMin()));
            if (in.KeyPressed('X')) deleteSel();
        }
        if (ctrl && in.KeyPressed('V')) insert(GetClipboard());
        if (in.KeyPressed(VK_RETURN) && submitted) *submitted = true;
        if (in.KeyPressed(VK_ESCAPE)) c.SetFocus(0);
        if (changed || before != es.cursor + es.anchor * 7919) es.blinkStart = c.Now();
    }

    // --- visuals ---
    const float hover = Hover(id, st.hovered);
    const float focus = Focus(id, focused);
    DrawList& dl = DL();
    dl.PushAlpha(EnabledAlpha(id));
    dl.RectFilled(field, Mix(t.surface, t.surfaceHover, std::max(hover * 0.8f, focus * 0.5f)), t.radius);
    dl.RectStroke(field, Mix(Mix(t.border, t.borderStrong, hover), t.accent.WithAlpha(0.0f), focus), t.radius);
    FocusRing(dl, field, t.radius, focus);
    if (!icon.empty())
        c.IconAt(dl, icon, {field.x + 8.0f, field.y, 24.0f, field.h}, 15.0f, Mix(t.textMuted, t.accent, focus));

    // Keep the caret visible by scrolling horizontally.
    const float caretX = es.id == id ? font.OffsetOf(*text, fs, es.cursor) : 0.0f;
    if (es.id == id) {
        if (caretX - es.scrollX > area.w - 2.0f) es.scrollX = caretX - area.w + 2.0f;
        if (caretX - es.scrollX < 0.0f) es.scrollX = caretX;
        es.scrollX = std::max(0.0f, std::min(es.scrollX, std::max(0.0f, font.Measure(*text, fs).x - area.w + 2.0f)));
    }
    const float scroll = es.id == id ? es.scrollX : 0.0f;

    dl.PushClip(area.Expand(2.0f, 0.0f));
    const float cy = field.Center().y;
    if (text->empty()) {
        c.TextAt(dl, FontRole::Regular, fs, {area.x, cy}, t.textMuted, hint);
    } else {
        if (focused && es.cursor != es.anchor) {
            const float x0 = font.OffsetOf(*text, fs, std::min(es.cursor, es.anchor)) - scroll;
            const float x1 = font.OffsetOf(*text, fs, std::max(es.cursor, es.anchor)) - scroll;
            dl.RectFilled({area.x + x0, cy - 10.0f, x1 - x0, 20.0f}, t.accent.WithAlpha(0.32f), 3.0f);
        }
        c.TextAt(dl, FontRole::Regular, fs, {area.x - scroll, cy}, t.text, *text);
    }
    if (focused) {
        // Caret glides to its new position and blinks with a soft fade.
        const float vis = A().Float(id, kCaret, caretX - scroll, Motion::Smooth(45.0f));
        const float phase = float(std::fmod(c.Now() - es.blinkStart, 1.06));
        const float blink = phase < 0.53f ? 1.0f : Saturate(1.0f - (phase - 0.53f) * 8.0f) + Saturate((phase - 0.98f) * 12.0f);
        dl.RectFilled({area.x + vis - 0.75f, cy - 9.0f, 1.5f, 18.0f}, t.accent.MulAlpha(Saturate(blink) * focus), 0.75f);
    } else {
        A().SetFloat(id, kCaret, caretX - scroll);
    }
    dl.PopClip();
    dl.PopAlpha();
    return changed;
}

} // namespace

bool InputField(std::string_view idLabel, std::string* text, std::string_view hint, std::string_view icon,
                bool* submitted) {
    const ID id = C().GetID(idLabel);
    const Rect field = NextRect({0, T().controlHeight});
    return EditField(id, field, text, hint, icon, submitted);
}

bool InputText(std::string_view label, std::string* text, std::string_view hint, bool* submitted) {
    const Theme& t = T();
    const ID id = C().GetID(label);
    const Rect row = NextRect({0, t.rowHeight});
    const float fw = std::min(260.0f, std::max(160.0f, row.w * 0.55f));
    const Rect field{row.Right() - fw, row.Center().y - t.controlHeight * 0.5f, fw, t.controlHeight};
    DL().PushAlpha(EnabledAlpha(id));
    RowLabel(row, label, t.textDim);
    DL().PopAlpha();
    return EditField(id, field, text, hint, {}, submitted);
}

// ---------------------------------------------------------------------------
// Keybind
// ---------------------------------------------------------------------------

std::string KeyName(int vk) {
    switch (vk) {
    case 0: return "None";
    case VK_LBUTTON: return "Mouse 1";
    case VK_RBUTTON: return "Mouse 2";
    case VK_MBUTTON: return "Mouse 3";
    case VK_XBUTTON1: return "Mouse 4";
    case VK_XBUTTON2: return "Mouse 5";
    default: break;
    }
    UINT sc = MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC);
    switch (vk) { // keys that need the "extended" bit for a correct name
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_PRIOR: case VK_NEXT: case VK_END:
    case VK_HOME: case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK: case VK_RCONTROL:
    case VK_RMENU:
        sc |= 0x100;
        break;
    default: break;
    }
    wchar_t buf[64]{};
    if (sc && GetKeyNameTextW(LONG(sc << 16), buf, 64) > 0) return utf8::FromWide(buf);
    char tmp[16];
    std::snprintf(tmp, sizeof(tmp), "Key 0x%02X", vk);
    return tmp;
}

bool Keybind(std::string_view label, int* vk) {
    Context& c = C();
    const Theme& t = T();
    const ID id = c.GetID(label);
    const Rect row = NextRect({0, t.rowHeight});
    static uint64_t s_waitStart = 0;

    ID& waiting = c.KeybindWaiting();
    const bool isWaiting = waiting == id;
    const std::string name = isWaiting ? "Press a key…" : KeyName(*vk);
    const float fw = std::max(96.0f, c.TextWidth(FontRole::Medium, t.fontSmall, name) + 28.0f);
    // Field width animates smoothly when the key name changes length.
    const float w = A().Float(id, kWidth, fw, Motion::Springy({420.0f, 34.0f, 1.0f}));
    const Rect field{row.Right() - w, row.Center().y - 13.0f, w, 26.0f};
    const ItemState st = c.ButtonBehavior(field, id);

    bool changed = false;
    if (st.clicked && !isWaiting) {
        waiting = id;
        s_waitStart = c.FrameIndex();
    } else if (isWaiting && c.FrameIndex() > s_waitStart + 1) {
        c.RequestKeyboard();
        const int key = c.In().LastPressedKey();
        if (key) {
            *vk = key == VK_ESCAPE ? 0 : key;
            waiting = 0;
            changed = true;
        }
    }

    const float hover = Hover(id, st.hovered);
    const float wait = Focus(id, waiting == id);
    const float pulse = 0.55f + 0.45f * std::sin(float(c.Now()) * 6.0f);
    DrawList& dl = DL();
    dl.PushAlpha(EnabledAlpha(id));
    RowLabel(row, label, t.textDim);
    dl.RectFilled(field, Mix(t.surface, t.surfaceHover, hover), t.radiusSm);
    dl.RectStroke(field, Mix(Mix(t.border, t.borderStrong, hover), t.accent.WithAlpha(0.9f * pulse), wait), t.radiusSm);
    if (wait > 0.01f) dl.Shadow(field, t.accent.WithAlpha(0.3f * wait * pulse), t.radiusSm, 12);
    dl.PushClip(field);
    c.TextCentered(dl, FontRole::Medium, t.fontSmall, field, Mix(*vk ? t.text : t.textMuted, t.accent, wait), name);
    dl.PopClip();
    dl.PopAlpha();
    return changed;
}

} // namespace silk::ui
