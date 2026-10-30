#include "silk/render/screenshot.h"

#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstring>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace silk {

bool SaveRenderTargetPng(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv,
                         const std::wstring& path) {
    ComPtr<ID3D11Resource> res;
    rtv->GetResource(&res);
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(res.As(&tex))) return false;

    D3D11_TEXTURE2D_DESC td{};
    tex->GetDesc(&td);
    if (td.Format != DXGI_FORMAT_R8G8B8A8_UNORM && td.Format != DXGI_FORMAT_B8G8R8A8_UNORM) return false;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&td, nullptr, &staging))) return false;
    ctx->CopyResource(staging.Get(), tex.Get());

    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
    std::vector<uint8_t> pixels(size_t(td.Width) * td.Height * 4);
    for (UINT y = 0; y < td.Height; ++y) {
        uint8_t* row = pixels.data() + size_t(y) * td.Width * 4;
        std::memcpy(row, static_cast<uint8_t*>(m.pData) + size_t(y) * m.RowPitch, size_t(td.Width) * 4);
        for (UINT x = 0; x < td.Width; ++x) {
            if (td.Format == DXGI_FORMAT_R8G8B8A8_UNORM) std::swap(row[x * 4 + 0], row[x * 4 + 2]); // -> BGRA
            row[x * 4 + 3] = 255; // opaque output
        }
    }
    ctx->Unmap(staging.Get(), 0);

    const bool coInit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    bool ok = false;
    {
        ComPtr<IWICImagingFactory> wic;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapEncoder> enc;
        ComPtr<IWICBitmapFrameEncode> frame;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
            SUCCEEDED(wic->CreateStream(&stream)) &&
            SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
            SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
            SUCCEEDED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
            SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
            SUCCEEDED(frame->SetSize(td.Width, td.Height))) {
            // The PNG encoder natively accepts BGRA; pixels were swizzled above.
            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
            ok = SUCCEEDED(frame->SetPixelFormat(&fmt)) && fmt == GUID_WICPixelFormat32bppBGRA &&
                 SUCCEEDED(frame->WritePixels(td.Height, td.Width * 4, UINT(pixels.size()), pixels.data())) &&
                 SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
        }
    }
    if (coInit) CoUninitialize();
    return ok;
}

} // namespace silk
