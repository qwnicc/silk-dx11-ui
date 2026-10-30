#pragma once
// Mouse/keyboard state fed from the Win32 WndProc.
//
// Events are accumulated between frames; per-frame edges (pressed/released)
// are cleared by EndFrame(). Coordinates are stored in logical pixels
// (physical pixels divided by the DPI scale).

#include "silk/core/math.h"

#include <string>

struct HWND__;

namespace silk {

enum class MouseButton : int { Left = 0, Right = 1, Middle = 2, Count = 3 };

class Input {
public:
    // Feed a Win32 message. Returns true when the UI wants to swallow it
    // (based on capture flags computed during the previous frame).
    bool HandleMessage(HWND__* hwnd, unsigned msg, uintptr_t wparam, intptr_t lparam);

    // Clears per-frame edge state. Call after the UI frame was built.
    void EndFrame();

    void SetDpiScale(float s) { dpi_ = s > 0.0f ? s : 1.0f; }

    // --- mouse ---
    Vec2 MousePos() const { return mouse_; }
    Vec2 MouseDelta() const { return mouse_ - prevMouse_; }
    bool MouseDown(MouseButton b = MouseButton::Left) const { return down_[int(b)]; }
    bool MousePressed(MouseButton b = MouseButton::Left) const { return pressed_[int(b)]; }
    bool MouseReleased(MouseButton b = MouseButton::Left) const { return released_[int(b)]; }
    bool MouseDoubleClicked(MouseButton b = MouseButton::Left) const { return doubleClicked_[int(b)]; }
    float Wheel() const { return wheel_; }
    bool MouseInside() const { return mouseInside_; }

    // --- keyboard (Win32 virtual-key codes) ---
    bool KeyDown(int vk) const { return vk >= 0 && vk < 256 && keyDown_[vk]; }
    bool KeyPressed(int vk) const { return vk >= 0 && vk < 256 && keyPressed_[vk]; } // includes auto-repeat
    bool Ctrl() const { return KeyDown(0x11); }
    bool Shift() const { return KeyDown(0x10); }
    bool Alt() const { return KeyDown(0x12); }
    // Last key/mouse button pressed this frame (for keybind capture), 0 if none.
    int LastPressedKey() const { return lastKey_; }
    const std::u32string& Chars() const { return chars_; }

    // --- capture flags, written by the UI each frame ---
    bool wantCaptureMouse = false;
    bool wantCaptureKeyboard = false;

    // Synthetic input (used by the demo's scripted screenshot mode).
    void InjectMouseMove(Vec2 p) { mouse_ = p; mouseInside_ = true; }
    void InjectMouseButton(MouseButton b, bool down) { OnButton(int(b), down); }
    void InjectChar(char32_t c) { chars_.push_back(c); }
    void InjectKey(int vk, bool down) { OnKey(vk, down); }
    void InjectWheel(float w) { wheel_ += w; }

private:
    void OnButton(int b, bool down);
    void OnKey(int vk, bool down);

    float dpi_ = 1.0f;
    Vec2 mouse_{-1e4f, -1e4f}, prevMouse_{-1e4f, -1e4f};
    bool mouseInside_ = false;
    bool down_[3]{}, pressed_[3]{}, released_[3]{}, doubleClicked_[3]{};
    double lastClickTime_[3]{};
    Vec2 lastClickPos_[3]{};
    float wheel_ = 0.0f;
    bool keyDown_[256]{}, keyPressed_[256]{};
    int lastKey_ = 0;
    std::u32string chars_;
    char16_t pendingHighSurrogate_ = 0;
};

} // namespace silk
