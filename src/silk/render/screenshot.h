#pragma once
// Saves a render target to PNG through WIC (used for automated previews).

#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;

namespace silk {

bool SaveRenderTargetPng(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11RenderTargetView* rtv,
                         const std::wstring& path);

} // namespace silk
