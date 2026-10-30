#pragma once
// Animation system.
//
// Values (float / Vec2 / Color) are attached to a widget ID plus a slot and
// animated toward a target every frame. Three motion models are available:
//   * Tween  – fixed duration with an easing curve; restarts from the current
//              value whenever the target changes,
//   * Spring – damped harmonic oscillator (stiffness/damping/mass),
//   * Smooth – critically damped exponential follow.
// All integration uses the real frame delta (springs are sub-stepped at a
// fixed rate), so motion looks identical at 60 or 240 FPS.

#include "silk/core/id.h"
#include "silk/core/math.h"

#include <cstdint>
#include <unordered_map>

namespace silk {

enum class Ease : uint8_t { Linear, OutCubic, OutExpo, InOutQuad, OutBack, InOutCubic };

float ApplyEase(Ease e, float t);

struct SpringParams {
    float stiffness = 260.0f;
    float damping = 24.0f;
    float mass = 1.0f;

    static constexpr SpringParams Snappy() { return {420.0f, 32.0f, 1.0f}; }
    static constexpr SpringParams Gentle() { return {170.0f, 26.0f, 1.0f}; }
    static constexpr SpringParams Bouncy() { return {300.0f, 14.0f, 1.0f}; }
};

// Standalone spring integrator (also used directly by toasts / HUD).
struct Spring {
    float value = 0.0f;
    float velocity = 0.0f;

    void Step(float target, float dt, const SpringParams& p = {});
    bool Settled(float target, float eps = 1e-3f) const {
        return std::abs(value - target) < eps && std::abs(velocity) < eps * 10.0f;
    }
};

struct Motion {
    enum class Kind : uint8_t { Tween, Spring, Smooth };
    Kind kind = Kind::Tween;
    Ease ease = Ease::OutCubic;
    float duration = 0.18f;   // tween seconds
    float speed = 18.0f;      // smooth: convergence rate (1/s)
    SpringParams spring;

    static constexpr Motion Tween(float seconds, Ease e = Ease::OutCubic) {
        Motion m; m.kind = Kind::Tween; m.duration = seconds; m.ease = e; return m;
    }
    static constexpr Motion Springy(SpringParams p = {}) {
        Motion m; m.kind = Kind::Spring; m.spring = p; return m;
    }
    static constexpr Motion Smooth(float speed) {
        Motion m; m.kind = Kind::Smooth; m.speed = speed; return m;
    }
};

// Commonly used motions.
namespace motion {
inline constexpr Motion kHover = Motion::Tween(0.16f, Ease::OutCubic);
inline constexpr Motion kPress = Motion::Springy(SpringParams::Snappy());
inline constexpr Motion kFade = Motion::Tween(0.22f, Ease::OutCubic);
inline constexpr Motion kOpen = Motion::Tween(0.20f, Ease::OutExpo);
inline constexpr Motion kSlide = Motion::Springy({320.0f, 30.0f, 1.0f});
} // namespace motion

class AnimStore {
public:
    // Advances the clock; tracks untouched for a while are garbage collected.
    void NewFrame(float dt);
    float Dt() const { return dt_; }

    float Float(ID id, uint32_t slot, float target, const Motion& m);
    Vec2 Vec(ID id, uint32_t slot, Vec2 target, const Motion& m);
    Color Col(ID id, uint32_t slot, Color target, const Motion& m);

    // Snap a value immediately (no animation).
    void SetFloat(ID id, uint32_t slot, float value);
    // Current value without advancing; `fallback` if the track doesn't exist.
    float Peek(ID id, uint32_t slot, float fallback = 0.0f) const;
    bool Exists(ID id, uint32_t slot) const;

private:
    struct Track {
        float value[4]{}, from[4]{}, target[4]{}, velocity[4]{};
        float t = 1.0f;          // tween progress
        uint64_t lastFrame = 0;
        bool updatedThisFrame = false;
    };

    Track& Get(ID id, uint32_t slot, const float* initial, int n, bool& created);
    void Advance(Track& tr, const float* target, int n, const Motion& m);

    static uint64_t Key(ID id, uint32_t slot) { return (uint64_t(id) << 32) | slot; }

    std::unordered_map<uint64_t, Track> tracks_;
    float dt_ = 1.0f / 60.0f;
    uint64_t frame_ = 0;
};

} // namespace silk
