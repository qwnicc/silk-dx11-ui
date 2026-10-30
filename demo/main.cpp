// Silk demo: Win32 window + DirectX 11 swap chain hosting the demo app.
//
// Command line (all optional):
//   --shot <file.png>   render `--frames` frames, save a screenshot and exit
//   --frames <n>        frames to simulate before the screenshot (default 90)
//   --script <name>     scripted input for screenshots (see DemoApp::RunScript)
//   --size <w>x<h>      client size in logical pixels
//   --novsync           present without vsync
//   --overlay           transparent, top-most, full-screen overlay over other apps
//                       (clicks pass through everywhere except the UI)

#include "demo_app.h"

#include "silk/core/utf8.h"
#include "silk/render/screenshot.h"

#include <d3d11.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <shellapi.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

struct D3D {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11RenderTargetView> rtv;
    // Overlay mode: the swap chain is presented through DirectComposition so
    // its alpha channel (premultiplied) makes the window see-through.
    ComPtr<IDCompositionDevice> dcomp;
    ComPtr<IDCompositionTarget> dcompTarget;
    ComPtr<IDCompositionVisual> dcompVisual;
    int width = 0, height = 0;
};

D3D g_d3d;
DemoApp g_app;
bool g_resizePending = false;
bool g_overlay = false;

bool CreateDevice(HWND hwnd) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                                   D3D11_SDK_VERSION, &g_d3d.device, nullptr, &g_d3d.ctx);
    if (FAILED(hr)) {
        // Debug layer may be missing; retry without it, then fall back to WARP.
        flags &= ~UINT(D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2, D3D11_SDK_VERSION,
                               &g_d3d.device, nullptr, &g_d3d.ctx);
        if (FAILED(hr))
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2, D3D11_SDK_VERSION,
                                   &g_d3d.device, nullptr, &g_d3d.ctx);
        if (FAILED(hr)) return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    g_d3d.device.As(&dxgiDevice);
    dxgiDevice->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&factory));

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    if (!g_overlay) {
        sd.Scaling = DXGI_SCALING_NONE;
        if (FAILED(factory->CreateSwapChainForHwnd(g_d3d.device.Get(), hwnd, &sd, nullptr, nullptr, &g_d3d.swap)))
            return false;
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        return true;
    }

    // Composition swap chains need an explicit size, stretch scaling and an
    // alpha mode. Silk's blending writes premultiplied colour when the target
    // is cleared to transparent black, which is exactly what DWM expects here.
    RECT rc;
    GetClientRect(hwnd, &rc);
    sd.Width = UINT(std::max(1L, rc.right - rc.left));
    sd.Height = UINT(std::max(1L, rc.bottom - rc.top));
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(factory->CreateSwapChainForComposition(g_d3d.device.Get(), &sd, nullptr, &g_d3d.swap))) return false;
    if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&g_d3d.dcomp)))) return false;
    if (FAILED(g_d3d.dcomp->CreateTargetForHwnd(hwnd, TRUE, &g_d3d.dcompTarget))) return false;
    if (FAILED(g_d3d.dcomp->CreateVisual(&g_d3d.dcompVisual))) return false;
    g_d3d.dcompVisual->SetContent(g_d3d.swap.Get());
    g_d3d.dcompTarget->SetRoot(g_d3d.dcompVisual.Get());
    return SUCCEEDED(g_d3d.dcomp->Commit());
}

// Overlay: the window ignores the mouse (clicks reach the app below) unless
// the cursor is over a piece of UI. Toggled only when the state changes.
void SetClickThrough(HWND hwnd, bool through) {
    // WS_EX_TRANSPARENT only lets input fall through on layered windows, and a
    // layered window without a redirection bitmap never takes hits itself, so
    // both flags are switched together.
    constexpr LONG_PTR kFlags = WS_EX_LAYERED | WS_EX_TRANSPARENT;
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const LONG_PTR want = through ? (ex | kFlags) : (ex & ~kFlags);
    if (want == ex) return;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, want);
    if (through) SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
}

void CreateRenderTarget() {
    ComPtr<ID3D11Texture2D> back;
    g_d3d.swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_d3d.device->CreateRenderTargetView(back.Get(), nullptr, &g_d3d.rtv);
    D3D11_TEXTURE2D_DESC td{};
    back->GetDesc(&td);
    g_d3d.width = int(td.Width);
    g_d3d.height = int(td.Height);
}

void Resize(HWND hwnd) {
    g_d3d.rtv.Reset();
    RECT rc;
    GetClientRect(hwnd, &rc);
    // Composition swap chains don't track the window size, so pass it explicitly.
    g_d3d.swap->ResizeBuffers(0, UINT(std::max(1L, rc.right - rc.left)), UINT(std::max(1L, rc.bottom - rc.top)),
                              DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_app.HandleMessage(hwnd, msg, wp, lp)) return 0;
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) g_resizePending = true;
        return 0;
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU) return 0; // no beep / menu on Alt
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::string ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    p = p.substr(0, p.find_last_of(L"\\/"));
    return silk::utf8::FromWide(p);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // --- command line ---
    std::wstring shotPath, script;
    int frames = 90, logicalW = 1440, logicalH = 900;
    bool vsync = true;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--shot" && i + 1 < argc) shotPath = argv[++i];
        else if (a == L"--frames" && i + 1 < argc) frames = _wtoi(argv[++i]);
        else if (a == L"--script" && i + 1 < argc) script = argv[++i];
        else if (a == L"--size" && i + 1 < argc) swscanf_s(argv[++i], L"%dx%d", &logicalW, &logicalH);
        else if (a == L"--novsync") vsync = false;
        else if (a == L"--overlay") g_overlay = true;
    }
    LocalFree(argv);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SilkDemo";
    RegisterClassExW(&wc);

    HWND hwnd = nullptr;
    if (g_overlay) {
        // Borderless, top-most window covering the primary monitor. No
        // redirection bitmap: DirectComposition provides the only content.
        // Layered + transparent lets mouse input fall through when wanted.
        const int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT |
                                   WS_EX_TOOLWINDOW,
                               wc.lpszClassName, L"Silk UI — overlay", WS_POPUP, 0, 0, sw, sh, nullptr, nullptr, inst,
                               nullptr);
        if (hwnd) SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    } else {
        const UINT dpi = GetDpiForSystem();
        RECT rc{0, 0, MulDiv(logicalW, int(dpi), 96), MulDiv(logicalH, int(dpi), 96)};
        AdjustWindowRectExForDpi(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
        hwnd = CreateWindowExW(0, wc.lpszClassName, L"Silk UI — demo", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                               CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
    }
    if (!hwnd || !CreateDevice(hwnd)) {
        MessageBoxW(nullptr, L"Failed to create a Direct3D 11 device.", L"Silk", MB_ICONERROR);
        return 1;
    }
    CreateRenderTarget();

    if (!g_app.Init(hwnd, g_d3d.device.Get(), g_d3d.ctx.Get(), ExeDir() + "/assets")) {
        MessageBoxW(nullptr, L"Failed to initialise the UI (fonts/shaders).", L"Silk", MB_ICONERROR);
        return 1;
    }
    g_app.SetOverlay(g_overlay);
    const bool shotMode = !shotPath.empty();
    if (shotMode) g_app.SetScript(silk::utf8::FromWide(script));
    ShowWindow(hwnd, shotMode ? SW_SHOWNOACTIVATE : show);

    int frame = 0;
    MSG msg{};
    bool running = true;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;
        if (g_resizePending) { Resize(hwnd); g_resizePending = false; }
        if (IsIconic(hwnd)) { Sleep(16); continue; }

        const float scale = float(GetDpiForWindow(hwnd)) / 96.0f;
        // In screenshot mode simulate a fixed 60 FPS clock for deterministic output.
        g_app.Frame(g_d3d.rtv.Get(), g_d3d.width, g_d3d.height, scale, shotMode ? 1.0f / 60.0f : -1.0f);

        if (shotMode && ++frame >= frames) {
            silk::SaveRenderTargetPng(g_d3d.device.Get(), g_d3d.ctx.Get(), g_d3d.rtv.Get(), shotPath);
            break;
        }
        g_d3d.swap->Present(vsync || shotMode ? 1 : 0, 0);
        // Composition swap chains don't throttle on their own; pace the
        // overlay to the DWM refresh so it doesn't spin at thousands of FPS.
        if (g_overlay && vsync) DwmFlush();
        g_app.AfterPresent();
        if (g_overlay) SetClickThrough(hwnd, !g_app.WantsMouse());
        if (g_app.QuitRequested()) DestroyWindow(hwnd);
    }
    g_app.Shutdown();
    return 0;
}
