#include "silk/anim/anim.h"

#include <cmath>

namespace silk {

float ApplyEase(Ease e, float t) {
    t = Saturate(t);
    switch (e) {
    case Ease::Linear: return t;
    case Ease::OutCubic: { const float u = 1.0f - t; return 1.0f - u * u * u; }
    case Ease::OutExpo: return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
    case Ease::InOutQuad: return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
    case Ease::InOutCubic: return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
    case Ease::OutBack: {
        constexpr float c1 = 1.70158f, c3 = c1 + 1.0f;
        const float u = t - 1.0f;
        return 1.0f + c3 * u * u * u + c1 * u * u;
    }
    }
    return t;
}

void Spring::Step(float target, float dt, const SpringParams& p) {
    // Semi-implicit Euler at a fixed 1/960 s step keeps the result independent
    // of the frame rate (60 vs 240 FPS give the same curve).
    constexpr float kStep = 1.0f / 960.0f;
    const int steps = std::max(1, int(std::ceil(dt / kStep)));
    const float h = dt / float(steps);
    const float invMass = 1.0f / std::max(p.mass, 1e-3f);
    for (int i = 0; i < steps; ++i) {
        const float force = -p.stiffness * (value - target) - p.damping * velocity;
        velocity += force * invMass * h;
        value += velocity * h;
    }
    if (Settled(target, 1e-4f)) {
        value = target;
        velocity = 0.0f;
    }
}

void AnimStore::NewFrame(float dt) {
    dt_ = dt;
    ++frame_;
    // Drop tracks that haven't been touched for ~3 seconds worth of frames.
    if (frame_ % 120 == 0) {
        std::erase_if(tracks_, [&](const auto& kv) { return frame_ - kv.second.lastFrame > 600; });
    }
}

AnimStore::Track& AnimStore::Get(ID id, uint32_t slot, const float* initial, int n, bool& created) {
    auto [it, inserted] = tracks_.try_emplace(Key(id, slot));
    Track& tr = it->second;
    created = inserted;
    if (inserted) {
        for (int i = 0; i < n; ++i) tr.value[i] = tr.from[i] = tr.target[i] = initial[i];
        tr.t = 1.0f;
    }
    if (tr.lastFrame != frame_) {
        tr.lastFrame = frame_;
        tr.updatedThisFrame = false;
    }
    return tr;
}

void AnimStore::Advance(Track& tr, const float* target, int n, const Motion& m) {
    if (tr.updatedThisFrame) return; // multiple queries per frame advance only once
    tr.updatedThisFrame = true;

    bool changed = false;
    for (int i = 0; i < n; ++i) changed |= std::abs(tr.target[i] - target[i]) > 1e-5f;
    if (changed) {
        for (int i = 0; i < n; ++i) {
            tr.from[i] = tr.value[i];
            tr.target[i] = target[i];
        }
        tr.t = 0.0f;
    }

    switch (m.kind) {
    case Motion::Kind::Tween: {
        if (tr.t >= 1.0f) {
            for (int i = 0; i < n; ++i) tr.value[i] = tr.target[i];
            break;
        }
        tr.t = std::min(1.0f, tr.t + dt_ / std::max(m.duration, 1e-4f));
        const float k = ApplyEase(m.ease, tr.t);
        for (int i = 0; i < n; ++i) tr.value[i] = Lerp(tr.from[i], tr.target[i], k);
        break;
    }
    case Motion::Kind::Spring:
        for (int i = 0; i < n; ++i) {
            Spring s{tr.value[i], tr.velocity[i]};
            s.Step(tr.target[i], dt_, m.spring);
            tr.value[i] = s.value;
            tr.velocity[i] = s.velocity;
        }
        break;
    case Motion::Kind::Smooth: {
        const float k = 1.0f - std::exp(-m.speed * dt_);
        for (int i = 0; i < n; ++i) {
            tr.value[i] += (tr.target[i] - tr.value[i]) * k;
            if (std::abs(tr.value[i] - tr.target[i]) < 1e-4f) tr.value[i] = tr.target[i];
        }
        break;
    }
    }
}

float AnimStore::Float(ID id, uint32_t slot, float target, const Motion& m) {
    bool created;
    Track& tr = Get(id, slot, &target, 1, created);
    if (!created) Advance(tr, &target, 1, m);
    return tr.value[0];
}

Vec2 AnimStore::Vec(ID id, uint32_t slot, Vec2 target, const Motion& m) {
    const float t[2] = {target.x, target.y};
    bool created;
    Track& tr = Get(id, slot, t, 2, created);
    if (!created) Advance(tr, t, 2, m);
    return {tr.value[0], tr.value[1]};
}

Color AnimStore::Col(ID id, uint32_t slot, Color target, const Motion& m) {
    const float t[4] = {target.r, target.g, target.b, target.a};
    bool created;
    Track& tr = Get(id, slot, t, 4, created);
    if (!created) Advance(tr, t, 4, m);
    return {tr.value[0], tr.value[1], tr.value[2], tr.value[3]};
}

void AnimStore::SetFloat(ID id, uint32_t slot, float value) {
    Track& tr = tracks_[Key(id, slot)];
    tr.value[0] = tr.from[0] = tr.target[0] = value;
    tr.velocity[0] = 0.0f;
    tr.t = 1.0f;
    tr.lastFrame = frame_;
}

float AnimStore::Peek(ID id, uint32_t slot, float fallback) const {
    const auto it = tracks_.find(Key(id, slot));
    return it == tracks_.end() ? fallback : it->second.value[0];
}

bool AnimStore::Exists(ID id, uint32_t slot) const { return tracks_.contains(Key(id, slot)); }

} // namespace silk
