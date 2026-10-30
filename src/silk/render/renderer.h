#pragma once
// DirectX 11 batch renderer for DrawLists.
//
// All lists of a frame are concatenated into one dynamic vertex/index buffer;
// each DrawCmd becomes one DrawIndexed call (per texture/clip change).
// Backdrop commands trigger a dual-Kawase blur of the render target which is
// then sampled by glass shapes.

#include "silk/core/math.h"

#include <span>
#include <string>
#include <wrl/client.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Buffer;
struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11InputLayout;
struct ID3D11BlendState;
struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ID3D11SamplerState;
struct ID3D11Texture2D;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;

namespace silk {

class DrawList;

struct Texture {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    int width = 0, height = 0;
};

struct RenderStats {
    int drawCalls = 0;
    int vertices = 0;
    int blurPasses = 0;
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool Init(ID3D11Device* device, ID3D11DeviceContext* ctx);

    // Draws the lists in order into `rtv`. `dpi` maps logical px to physical px.
    void Render(std::span<DrawList* const> lists, ID3D11RenderTargetView* rtv, float dpi, float time);

    // Number of Kawase down/up iterations (1..6). More = stronger blur.
    void SetBlurStrength(int iterations) { blurIterations_ = std::clamp(iterations, 1, 6); }
    int BlurStrength() const { return blurIterations_; }

    Texture LoadTexture(const std::string& path);
    Texture CreateTexture(int w, int h, const void* rgba);

    ID3D11Device* Device() const { return device_.Get(); }
    ID3D11DeviceContext* Context() const { return ctx_.Get(); }
    const RenderStats& Stats() const { return stats_; }

private:
    struct BlurLevel {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        int w = 0, h = 0;
    };

    bool CompileShaders();
    bool EnsureBuffers(size_t vtxCount, size_t idxCount);
    bool EnsureBlurTargets(ID3D11Texture2D* rt);
    void SetupUiState(ID3D11RenderTargetView* rtv, int w, int h);
    ID3D11ShaderResourceView* BlurBackdrop(ID3D11RenderTargetView* rtv, int w, int h);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_, vsFull_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_, psDown_, psUp_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> layout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vb_, ib_, cbFrame_, cbBlur_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blendAlpha_, blendOpaque_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    size_t vbCapacity_ = 0, ibCapacity_ = 0;

    BlurLevel sceneCopy_;
    BlurLevel levels_[7];
    int blurIterations_ = 4;
    int sceneFormat_ = 0; // DXGI_FORMAT of the current render target
    RenderStats stats_;
};

} // namespace silk
