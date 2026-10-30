// Implementation of small core utilities (clock, UTF-8 conversion).

#include "silk/core/time.h"
#include "silk/core/utf8.h"

#include <windows.h>

#include <cmath>

namespace silk {

Clock::Clock() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    freq_ = f.QuadPart;
    start_ = last_ = c.QuadPart;
}

float Clock::Tick() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    const double raw = double(c.QuadPart - last_) / double(freq_);
    last_ = c.QuadPart;
    now_ = double(c.QuadPart - start_) / double(freq_);
    // Clamp huge hitches (window drag, breakpoint) so animations don't jump.
    dt_ = float(raw > 0.1 ? 0.1 : (raw < 1e-5 ? 1e-5 : raw));
    const float inst = 1.0f / dt_;
    fps_ = frame_ == 0 ? inst : fps_ + (inst - fps_) * (1.0f - std::exp(-dt_ * 4.0f));
    ++frame_;
    return dt_;
}

namespace utf8 {

std::wstring ToWide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string FromWide(std::wstring_view w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace utf8
} // namespace silk
