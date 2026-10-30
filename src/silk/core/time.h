#pragma once
// High-resolution frame clock with smoothed FPS.

#include <cstdint>

namespace silk {

class Clock {
public:
    Clock();

    // Call once per frame; returns the clamped delta time in seconds.
    float Tick();

    float Delta() const { return dt_; }
    double Now() const { return now_; }   // seconds since clock creation
    float Fps() const { return fps_; }    // exponentially smoothed
    uint64_t Frame() const { return frame_; }

private:
    int64_t freq_ = 1, start_ = 0, last_ = 0;
    double now_ = 0.0;
    float dt_ = 1.0f / 60.0f;
    float fps_ = 60.0f;
    uint64_t frame_ = 0;
};

} // namespace silk
