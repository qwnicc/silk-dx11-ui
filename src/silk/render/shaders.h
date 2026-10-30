#pragma once
// HLSL source for all Silk shaders, compiled at runtime with D3DCompile.

namespace silk::shaders {

// Main UI shader: one vertex format, the pixel shader branches on the
// primitive type stored per vertex (see ShapeType in draw_list.h).
inline constexpr const char* kUi = R"HLSL(
cbuffer Frame : register(b0) {
    float2 viewSize;     // logical px
    float2 screenSize;   // physical px
    float  dpi;
    float  time;
    float2 pad;
};

Texture2D    tex0     : register(t0);
Texture2D    backdrop : register(t1);
SamplerState samp     : register(s0);

struct VSIn {
    float2 pos   : POSITION;
    float2 uv    : TEXCOORD0;
    float2 local : TEXCOORD1;
    float4 p0    : TEXCOORD2;
    float4 p1    : TEXCOORD3;
    float4 col   : COLOR0;
    float4 col2  : COLOR1;
};

struct PSIn {
    float4 pos   : SV_Position;
    float2 uv    : TEXCOORD0;
    float2 local : TEXCOORD1;
    nointerpolation float4 p0 : TEXCOORD2;
    nointerpolation float4 p1 : TEXCOORD3;
    float4 col   : COLOR0;
    nointerpolation float4 col2 : COLOR1;
};

PSIn VSMain(VSIn i) {
    PSIn o;
    float2 ndc = i.pos / viewSize * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.pos = float4(ndc, 0.0, 1.0);
    o.uv = i.uv; o.local = i.local; o.p0 = i.p0; o.p1 = i.p1; o.col = i.col; o.col2 = i.col2;
    return o;
}

// Signed distance to a rounded box centred at the origin.
float sdRoundRect(float2 p, float2 halfSize, float r) {
    float2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// Smooth approximation of erf() used for gaussian-like shadow falloff.
float erfApprox(float x) {
    float s = sign(x), a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

float3 hue2rgb(float h) {
    float3 k = saturate(abs(frac(h + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
    return k;
}

float checker(float2 p, float cell) {
    float2 c = floor(p / max(cell, 1.0));
    return fmod(abs(c.x + c.y), 2.0) < 0.5 ? 1.0 : 0.78;
}

float4 PSMain(PSIn i) : SV_Target {
    int type = (int)(i.p1.x + 0.5);

    if (type == 0) {                       // textured
        return tex0.Sample(samp, i.uv) * i.col;
    }
    if (type == 1) {                       // SDF text
        float d = tex0.Sample(samp, i.uv).r;
        float2 unitRange = float2(i.p0.x, i.p0.z);
        float2 screenTexSize = 1.0 / max(fwidth(i.uv), 1e-6);
        float pxRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
        float sd = (d - 0.5) * pxRange + i.p0.y;
        float a = saturate(sd + 0.5);
        return float4(i.col.rgb, i.col.a * a);
    }

    // --- SDF shapes ---
    float d = sdRoundRect(i.local, i.p0.xy, i.p0.z);
    float fw = max(length(float2(ddx(d), ddy(d))), 1e-4);

    if (type == 3) {                       // soft shadow / glow
        float sigma = max(i.p1.y * 0.5, 0.5);
        float a = 0.5 - 0.5 * erfApprox(d / (sigma * 1.41421356));
        return float4(i.col.rgb, i.col.a * a);
    }

    float cov = saturate(0.5 - d / fw);
    if (i.p0.w > 0.0) {                    // inner stroke of width p0.w
        cov *= saturate(0.5 + (d + i.p0.w) / fw);
    }

    float4 c = i.col;
    if ((int)(i.p1.z + 0.5) == 1) {        // radial gradient, uv = offset from centre
        float t = saturate(length(i.uv) / max(i.p1.w, 1e-3));
        c = lerp(i.col, i.col2, t);
    }

    if (type == 4) {                       // frosted glass
        float2 suv = i.pos.xy / screenSize;
        float3 bg = backdrop.Sample(samp, suv).rgb;
        // subtle noise breaks up banding in the blurred backdrop
        float n = frac(sin(dot(i.pos.xy, float2(12.9898, 78.233))) * 43758.5453) - 0.5;
        float3 rgb = lerp(bg, c.rgb, c.a) + n * (1.5 / 255.0);
        return float4(rgb, cov * i.p1.w);
    }
    if (type == 5) {                       // saturation / value field
        float2 uv = saturate(i.uv);
        float3 rgb = lerp(float3(1, 1, 1), c.rgb, uv.x) * (1.0 - uv.y);
        return float4(rgb, cov * c.a);
    }
    if (type == 6) {                       // hue bar (vertical)
        return float4(hue2rgb(saturate(i.uv.y)), cov * c.a);
    }
    if (type == 7) {                       // checkerboard
        float v = checker(i.local + i.p0.xy, i.p1.w);
        return float4(v, v, v, cov * c.a);
    }
    if (type == 8) {                       // alpha ramp over checkerboard
        float v = checker(i.local + i.p0.xy, i.p1.w);
        float t = saturate(i.uv.x);
        float3 rgb = lerp(float3(v, v, v), c.rgb, t);
        return float4(rgb, cov);
    }
    return float4(c.rgb, c.a * cov);
}
)HLSL";

// Dual Kawase blur passes, drawn with a fullscreen triangle.
inline constexpr const char* kBlur = R"HLSL(
cbuffer Blur : register(b0) {
    float2 texel;   // 1 / source size
    float  offset;
    float  pad;
};
Texture2D    src  : register(t0);
SamplerState samp : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSFull(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float4 PSDown(VSOut i) : SV_Target {
    float2 h = texel * 0.5 * offset;
    float4 s = src.Sample(samp, i.uv) * 4.0;
    s += src.Sample(samp, i.uv - h);
    s += src.Sample(samp, i.uv + h);
    s += src.Sample(samp, i.uv + float2(h.x, -h.y));
    s += src.Sample(samp, i.uv - float2(h.x, -h.y));
    return s / 8.0;
}

float4 PSUp(VSOut i) : SV_Target {
    float2 h = texel * 0.5 * offset;
    float4 s = src.Sample(samp, i.uv + float2(-h.x * 2.0, 0.0));
    s += src.Sample(samp, i.uv + float2(-h.x, h.y)) * 2.0;
    s += src.Sample(samp, i.uv + float2(0.0, h.y * 2.0));
    s += src.Sample(samp, i.uv + float2(h.x, h.y)) * 2.0;
    s += src.Sample(samp, i.uv + float2(h.x * 2.0, 0.0));
    s += src.Sample(samp, i.uv + float2(h.x, -h.y)) * 2.0;
    s += src.Sample(samp, i.uv + float2(0.0, -h.y * 2.0));
    s += src.Sample(samp, i.uv + float2(-h.x, -h.y)) * 2.0;
    return s / 12.0;
}
)HLSL";

} // namespace silk::shaders
