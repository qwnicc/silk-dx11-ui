#include "silk/render/renderer.h"

#include "silk/render/draw_list.h"
#include "silk/render/shaders.h"

#include <d3d11.h>
#include <d3dcompiler.h>

#include <cstring>
#include <vector>

#pragma warning(push, 0)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>
#pragma warning(pop)

using Microsoft::WRL::ComPtr;

namespace silk {

namespace {

struct FrameCB {
    float viewSize[2];
    float screenSize[2];
    float dpi;
    float time;
    float pad[2];
};

struct BlurCB {
    float texel[2];
    float offset;
    float pad;
};

ComPtr<ID3DBlob> Compile(const char* src, const char* entry, const char* target) {
    ComPtr<ID3DBlob> blob, err;
    const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS;
    const HRESULT hr = D3DCompile(src, std::strlen(src), "silk", nullptr, nullptr, entry, target, flags, 0,
                                  &blob, &err);
    if (FAILED(hr)) {
        if (err) OutputDebugStringA(static_cast<const char*>(err->GetBufferPointer()));
        return nullptr;
    }
    return blob;
}

} // namespace

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

bool Renderer::Init(ID3D11Device* device, ID3D11DeviceContext* ctx) {
    device_ = device;
    ctx_ = ctx;
    if (!CompileShaders()) return false;

    D3D11_BLEND_DESC bd{};
    auto& rt = bd.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D11_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device_->CreateBlendState(&bd, &blendAlpha_))) return false;
    rt.BlendEnable = FALSE;
    if (FAILED(device_->CreateBlendState(&bd, &blendOpaque_))) return false;

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.ScissorEnable = TRUE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(device_->CreateRasterizerState(&rd, &raster_))) return false;

    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = FALSE;
    dd.StencilEnable = FALSE;
    if (FAILED(device_->CreateDepthStencilState(&dd, &depth_))) return false;

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&sd, &sampler_))) return false;

    D3D11_BUFFER_DESC cb{};
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cb.ByteWidth = sizeof(FrameCB);
    if (FAILED(device_->CreateBuffer(&cb, nullptr, &cbFrame_))) return false;
    cb.ByteWidth = sizeof(BlurCB);
    return SUCCEEDED(device_->CreateBuffer(&cb, nullptr, &cbBlur_));
}

bool Renderer::CompileShaders() {
    auto vsBlob = Compile(shaders::kUi, "VSMain", "vs_5_0");
    auto psBlob = Compile(shaders::kUi, "PSMain", "ps_5_0");
    auto vsFull = Compile(shaders::kBlur, "VSFull", "vs_5_0");
    auto psDown = Compile(shaders::kBlur, "PSDown", "ps_5_0");
    auto psUp = Compile(shaders::kBlur, "PSUp", "ps_5_0");
    if (!vsBlob || !psBlob || !vsFull || !psDown || !psUp) return false;

    auto ok = [](HRESULT hr) { return SUCCEEDED(hr); };
    if (!ok(device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_)) ||
        !ok(device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_)) ||
        !ok(device_->CreateVertexShader(vsFull->GetBufferPointer(), vsFull->GetBufferSize(), nullptr, &vsFull_)) ||
        !ok(device_->CreatePixelShader(psDown->GetBufferPointer(), psDown->GetBufferSize(), nullptr, &psDown_)) ||
        !ok(device_->CreatePixelShader(psUp->GetBufferPointer(), psUp->GetBufferSize(), nullptr, &psUp_)))
        return false;

    const D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, x), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, u), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, lx), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, p0), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, p1), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(Vertex, col), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(Vertex, col2), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    return ok(device_->CreateInputLayout(layout, UINT(std::size(layout)), vsBlob->GetBufferPointer(),
                                         vsBlob->GetBufferSize(), &layout_));
}

bool Renderer::EnsureBuffers(size_t vtxCount, size_t idxCount) {
    if (vtxCount > vbCapacity_) {
        vbCapacity_ = std::max<size_t>(vtxCount + vtxCount / 2, 4096);
        D3D11_BUFFER_DESC d{};
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        d.ByteWidth = UINT(vbCapacity_ * sizeof(Vertex));
        vb_.Reset();
        if (FAILED(device_->CreateBuffer(&d, nullptr, &vb_))) { vbCapacity_ = 0; return false; }
    }
    if (idxCount > ibCapacity_) {
        ibCapacity_ = std::max<size_t>(idxCount + idxCount / 2, 8192);
        D3D11_BUFFER_DESC d{};
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_INDEX_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        d.ByteWidth = UINT(ibCapacity_ * sizeof(uint32_t));
        ib_.Reset();
        if (FAILED(device_->CreateBuffer(&d, nullptr, &ib_))) { ibCapacity_ = 0; return false; }
    }
    return true;
}

bool Renderer::EnsureBlurTargets(ID3D11Texture2D* rt) {
    D3D11_TEXTURE2D_DESC rd{};
    rt->GetDesc(&rd);
    if (sceneCopy_.tex && sceneCopy_.w == int(rd.Width) && sceneCopy_.h == int(rd.Height) &&
        sceneFormat_ == int(rd.Format))
        return true;

    auto make = [&](BlurLevel& l, int w, int h, bool rtvNeeded) {
        l = {};
        D3D11_TEXTURE2D_DESC td{};
        td.Width = UINT(w);
        td.Height = UINT(h);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = rd.Format;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | (rtvNeeded ? D3D11_BIND_RENDER_TARGET : 0u);
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &l.tex))) return false;
        if (FAILED(device_->CreateShaderResourceView(l.tex.Get(), nullptr, &l.srv))) return false;
        if (rtvNeeded && FAILED(device_->CreateRenderTargetView(l.tex.Get(), nullptr, &l.rtv))) return false;
        l.w = w;
        l.h = h;
        return true;
    };
    sceneFormat_ = int(rd.Format);
    if (!make(sceneCopy_, int(rd.Width), int(rd.Height), false)) return false;
    for (int i = 1; i < 7; ++i)
        if (!make(levels_[i], std::max(1, int(rd.Width) >> i), std::max(1, int(rd.Height) >> i), true)) return false;
    return true;
}

void Renderer::SetupUiState(ID3D11RenderTargetView* rtv, int w, int h) {
    const UINT stride = sizeof(Vertex), offset = 0;
    ID3D11Buffer* vb = vb_.Get();
    ctx_->IASetInputLayout(layout_.Get());
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx_->IASetIndexBuffer(ib_.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx_->VSSetShader(vs_.Get(), nullptr, 0);
    ctx_->PSSetShader(ps_.Get(), nullptr, 0);
    ID3D11Buffer* cb = cbFrame_.Get();
    ctx_->VSSetConstantBuffers(0, 1, &cb);
    ctx_->PSSetConstantBuffers(0, 1, &cb);
    ID3D11SamplerState* s = sampler_.Get();
    ctx_->PSSetSamplers(0, 1, &s);
    ctx_->GSSetShader(nullptr, nullptr, 0);
    const float blendFactor[4] = {0, 0, 0, 0};
    ctx_->OMSetBlendState(blendAlpha_.Get(), blendFactor, 0xFFFFFFFF);
    ctx_->OMSetDepthStencilState(depth_.Get(), 0);
    ctx_->RSSetState(raster_.Get());
    ctx_->OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT vp{0, 0, float(w), float(h), 0, 1};
    ctx_->RSSetViewports(1, &vp);
}

ID3D11ShaderResourceView* Renderer::BlurBackdrop(ID3D11RenderTargetView* rtv, int w, int h) {
    ComPtr<ID3D11Resource> res;
    rtv->GetResource(&res);
    ComPtr<ID3D11Texture2D> rt;
    if (FAILED(res.As(&rt)) || !EnsureBlurTargets(rt.Get())) return nullptr;
    (void)w; (void)h;

    ctx_->CopyResource(sceneCopy_.tex.Get(), rt.Get());

    ID3D11ShaderResourceView* nullSrv[2] = {nullptr, nullptr};
    ctx_->PSSetShaderResources(0, 2, nullSrv);
    ctx_->IASetInputLayout(nullptr);
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vsFull_.Get(), nullptr, 0);
    const float blendFactor[4] = {0, 0, 0, 0};
    ctx_->OMSetBlendState(blendOpaque_.Get(), blendFactor, 0xFFFFFFFF);
    ID3D11Buffer* cb = cbBlur_.Get();
    ctx_->PSSetConstantBuffers(0, 1, &cb);

    auto pass = [&](ID3D11PixelShader* ps, const BlurLevel& src, const BlurLevel& dst) {
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(ctx_->Map(cbBlur_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            BlurCB b{{1.0f / float(src.w), 1.0f / float(src.h)}, 1.0f, 0.0f};
            std::memcpy(m.pData, &b, sizeof(b));
            ctx_->Unmap(cbBlur_.Get(), 0);
        }
        ctx_->PSSetShaderResources(0, 1, nullSrv);
        ID3D11RenderTargetView* target = dst.rtv.Get();
        ctx_->OMSetRenderTargets(1, &target, nullptr);
        D3D11_VIEWPORT vp{0, 0, float(dst.w), float(dst.h), 0, 1};
        ctx_->RSSetViewports(1, &vp);
        D3D11_RECT sc{0, 0, LONG(dst.w), LONG(dst.h)};
        ctx_->RSSetScissorRects(1, &sc);
        ctx_->PSSetShader(ps, nullptr, 0);
        ID3D11ShaderResourceView* s = src.srv.Get();
        ctx_->PSSetShaderResources(0, 1, &s);
        ctx_->Draw(3, 0);
        ++stats_.blurPasses;
    };

    const int n = blurIterations_;
    pass(psDown_.Get(), sceneCopy_, levels_[1]);
    for (int i = 2; i <= n; ++i) pass(psDown_.Get(), levels_[i - 1], levels_[i]);
    for (int i = n; i >= 2; --i) pass(psUp_.Get(), levels_[i], levels_[i - 1]);
    ctx_->PSSetShaderResources(0, 1, nullSrv);
    return levels_[1].srv.Get();
}

void Renderer::Render(std::span<DrawList* const> lists, ID3D11RenderTargetView* rtv, float dpi, float time) {
    stats_ = {};
    ComPtr<ID3D11Resource> res;
    rtv->GetResource(&res);
    ComPtr<ID3D11Texture2D> rt;
    if (FAILED(res.As(&rt))) return;
    D3D11_TEXTURE2D_DESC rd{};
    rt->GetDesc(&rd);
    const int w = int(rd.Width), h = int(rd.Height);

    size_t totalV = 0, totalI = 0;
    for (const DrawList* dl : lists) {
        totalV += dl->Vertices().size();
        totalI += dl->Indices().size();
    }
    if (totalI == 0 || !EnsureBuffers(totalV, totalI)) return;

    // Upload all lists into the shared buffers.
    std::vector<std::pair<uint32_t, uint32_t>> bases; // (vertex base, index base) per list
    bases.reserve(lists.size());
    D3D11_MAPPED_SUBRESOURCE vm, im;
    if (FAILED(ctx_->Map(vb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &vm))) return;
    if (FAILED(ctx_->Map(ib_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &im))) { ctx_->Unmap(vb_.Get(), 0); return; }
    auto* vdst = static_cast<Vertex*>(vm.pData);
    auto* idst = static_cast<uint32_t*>(im.pData);
    uint32_t vb = 0, ib = 0;
    for (const DrawList* dl : lists) {
        bases.emplace_back(vb, ib);
        const auto& v = dl->Vertices();
        const auto& i = dl->Indices();
        if (!v.empty()) std::memcpy(vdst + vb, v.data(), v.size() * sizeof(Vertex));
        if (!i.empty()) std::memcpy(idst + ib, i.data(), i.size() * sizeof(uint32_t));
        vb += uint32_t(v.size());
        ib += uint32_t(i.size());
    }
    ctx_->Unmap(vb_.Get(), 0);
    ctx_->Unmap(ib_.Get(), 0);
    stats_.vertices = int(totalV);

    D3D11_MAPPED_SUBRESOURCE cm;
    if (SUCCEEDED(ctx_->Map(cbFrame_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &cm))) {
        FrameCB f{{float(w) / dpi, float(h) / dpi}, {float(w), float(h)}, dpi, time, {0, 0}};
        std::memcpy(cm.pData, &f, sizeof(f));
        ctx_->Unmap(cbFrame_.Get(), 0);
    }

    SetupUiState(rtv, w, h);
    ID3D11ShaderResourceView* backdrop = nullptr;
    for (size_t li = 0; li < lists.size(); ++li) {
        const auto [vbase, ibase] = bases[li];
        for (const DrawCmd& cmd : lists[li]->Commands()) {
            if (cmd.kind == DrawCmd::Kind::Backdrop) {
                backdrop = BlurBackdrop(rtv, w, h);
                SetupUiState(rtv, w, h);
                continue;
            }
            if (cmd.idxCount == 0) continue;
            // Clip rects are in logical px; scale to physical px for the scissor.
            const LONG x0 = std::max<LONG>(0, LONG(std::floor(cmd.clip.x * dpi)));
            const LONG y0 = std::max<LONG>(0, LONG(std::floor(cmd.clip.y * dpi)));
            const LONG x1 = std::min<LONG>(w, LONG(std::ceil(cmd.clip.Right() * dpi)));
            const LONG y1 = std::min<LONG>(h, LONG(std::ceil(cmd.clip.Bottom() * dpi)));
            if (x1 <= x0 || y1 <= y0) continue;
            D3D11_RECT sc{x0, y0, x1, y1};
            ctx_->RSSetScissorRects(1, &sc);
            ID3D11ShaderResourceView* srvs[2] = {cmd.tex, backdrop};
            ctx_->PSSetShaderResources(0, 2, srvs);
            ctx_->DrawIndexed(cmd.idxCount, ibase + cmd.idxOffset, INT(vbase));
            ++stats_.drawCalls;
        }
    }
    ID3D11ShaderResourceView* nullSrv[2] = {nullptr, nullptr};
    ctx_->PSSetShaderResources(0, 2, nullSrv);
}

Texture Renderer::CreateTexture(int w, int h, const void* rgba) {
    Texture t;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = UINT(w);
    td.Height = UINT(h);
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{rgba, UINT(w * 4), 0};
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(device_->CreateTexture2D(&td, &init, &tex))) return t;
    if (FAILED(device_->CreateShaderResourceView(tex.Get(), nullptr, &t.srv))) return t;
    t.width = w;
    t.height = h;
    return t;
}

Texture Renderer::LoadTexture(const std::string& path) {
    int w = 0, h = 0, n = 0;
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) return {};
    Texture t = CreateTexture(w, h, data);
    stbi_image_free(data);
    return t;
}

} // namespace silk
