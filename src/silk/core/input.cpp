#include "silk/core/input.h"

#include <windows.h>
#include <windowsx.h>

namespace silk {

namespace {
double NowSeconds() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(freq.QuadPart);
}
} // namespace

void Input::OnButton(int b, bool down) {
    if (down) {
        if (!down_[b]) {
            pressed_[b] = true;
            const double t = NowSeconds();
            const double interval = double(GetDoubleClickTime()) / 1000.0;
            if (t - lastClickTime_[b] < interval && (mouse_ - lastClickPos_[b]).Length() < 6.0f) {
                doubleClicked_[b] = true;
                lastClickTime_[b] = 0.0;
            } else {
                lastClickTime_[b] = t;
            }
            lastClickPos_[b] = mouse_;
            // Mouse buttons map to VK_LBUTTON / VK_RBUTTON / VK_MBUTTON for keybinds.
            static constexpr int kVk[3] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};
            lastKey_ = kVk[b];
        }
    } else if (down_[b]) {
        released_[b] = true;
    }
    down_[b] = down;
}

void Input::OnKey(int vk, bool down) {
    if (vk < 0 || vk >= 256) return;
    if (down) {
        keyPressed_[vk] = true;
        if (!keyDown_[vk]) lastKey_ = vk;
    }
    keyDown_[vk] = down;
}

bool Input::HandleMessage(HWND__* hwnd, unsigned msg, uintptr_t wparam, intptr_t lparam) {
    const HWND wnd = reinterpret_cast<HWND>(hwnd);
    const WPARAM wp = WPARAM(wparam);
    const LPARAM lp = LPARAM(lparam);
    switch (msg) {
    case WM_MOUSEMOVE: {
        mouse_ = Vec2(float(GET_X_LPARAM(lp)), float(GET_Y_LPARAM(lp))) / dpi_;
        if (!mouseInside_) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, wnd, 0};
            TrackMouseEvent(&tme);
            mouseInside_ = true;
        }
        return wantCaptureMouse;
    }
    case WM_MOUSELEAVE:
        mouseInside_ = false;
        if (!down_[0] && !down_[1] && !down_[2]) mouse_ = {-1e4f, -1e4f};
        return false;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: {
        const int b = (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) ? 0
                    : (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) ? 1 : 2;
        if (!down_[0] && !down_[1] && !down_[2] && GetCapture() == nullptr) SetCapture(wnd);
        OnButton(b, true);
        return wantCaptureMouse;
    }
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: {
        const int b = msg == WM_LBUTTONUP ? 0 : (msg == WM_RBUTTONUP ? 1 : 2);
        OnButton(b, false);
        if (!down_[0] && !down_[1] && !down_[2] && GetCapture() == wnd) ReleaseCapture();
        return wantCaptureMouse;
    }
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: {
        // Side buttons only matter for keybinds.
        const int vk = GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
        OnKey(vk, true);
        return true;
    }
    case WM_XBUTTONUP:
        OnKey(GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2, false);
        return true;
    case WM_MOUSEWHEEL:
        wheel_ += float(GET_WHEEL_DELTA_WPARAM(wp)) / float(WHEEL_DELTA);
        return wantCaptureMouse;
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        OnKey(int(wp), true);
        return wantCaptureKeyboard;
    case WM_KEYUP: case WM_SYSKEYUP:
        OnKey(int(wp), false);
        return wantCaptureKeyboard;
    case WM_CHAR: {
        const auto c = char16_t(wp);
        if (c >= 0xD800 && c < 0xDC00) {
            pendingHighSurrogate_ = c;
        } else if (c >= 0xDC00 && c < 0xE000) {
            if (pendingHighSurrogate_)
                chars_.push_back(char32_t(0x10000 + ((pendingHighSurrogate_ - 0xD800) << 10) + (c - 0xDC00)));
            pendingHighSurrogate_ = 0;
        } else if (c >= 32 && c != 127) {
            chars_.push_back(char32_t(c));
        }
        return wantCaptureKeyboard;
    }
    case WM_KILLFOCUS:
        // Release everything so keys don't get stuck when alt-tabbing.
        for (bool& k : keyDown_) k = false;
        for (int b = 0; b < 3; ++b) if (down_[b]) OnButton(b, false);
        return false;
    default:
        return false;
    }
}

void Input::EndFrame() {
    prevMouse_ = mouse_;
    for (int b = 0; b < 3; ++b) pressed_[b] = released_[b] = doubleClicked_[b] = false;
    for (bool& k : keyPressed_) k = false;
    wheel_ = 0.0f;
    lastKey_ = 0;
    chars_.clear();
}

} // namespace silk
