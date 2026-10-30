#pragma once
// The Silk demo application: builds the UI every frame and renders it.

#include "silk/silk.h"

#include <string>
#include <vector>
#include <windows.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;

class DemoApp {
public:
    bool Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* ctx, const std::string& assetDir);
    void Shutdown();
    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    // fixedDt < 0 = real clock.
    void Frame(ID3D11RenderTargetView* rtv, int width, int height, float dpi, float fixedDt);
    void AfterPresent();
    // Scripted input for screenshots, e.g. "page=1;click=420,300@30".
    void SetScript(std::string script);

    // Overlay mode: transparent background, global menu key, click-through.
    void SetOverlay(bool on) { overlay_ = on; }
    bool WantsMouse() const { return ui_.In().wantCaptureMouse; }
    bool QuitRequested() const { return quit_; }

private:
    struct Particle { silk::Vec2 pos, vel; };
    struct ScriptEvent { int frame; enum { Move, Down, Up, Key, Char } kind; silk::Vec2 pos; int key; };

    void RunScript();
    void PollOverlayInput(float dpi);
    void DrawBackground(silk::DrawList& dl, silk::Rect screen);
    void DrawMenu();
    void PageHome();
    void PageWidgets();
    void PageVisuals();
    void PageKeybinds();
    void PageSettings();
    void DrawModal();
    void UpdateHud();
    void SaveConfig();
    bool LoadConfig();

    HWND hwnd_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;
    silk::Context ui_;
    silk::Hud hud_;
    std::string configPath_;
    std::vector<Particle> particles_;
    std::vector<ScriptEvent> script_;
    int frame_ = 0;

    // --- demo state ---
    bool menuOpen_ = true;
    int page_ = 0;
    int widgetTab_ = 0;
    bool animatedBg_ = true;
    bool showWatermark_ = true;
    bool showKeybinds_ = true;
    bool checks_[3] = {true, false, true};
    bool toggles_[3] = {true, false, true};
    float sliderF_ = 0.62f;
    int sliderI_ = 90;
    int combo_ = 1;
    bool multi_[5] = {true, false, true, true, false};
    silk::Color colorA_ = silk::Color::Hex(0x7F6AF2);
    silk::Color colorB_ = silk::Color::Hex(0x22D3EE, 0.8f);
    int keyMenu_ = VK_INSERT;
    int keyAim_ = VK_XBUTTON2;
    int keyEsp_ = 0;
    std::string name_ = "developer";
    std::string search_;
    int themeIdx_ = 0;
    int blur_ = 4;
    float radius_ = 8.0f;
    bool glass_ = true;
    int fpsCap_ = 0;
    bool overlay_ = false;
    bool quit_ = false;
    bool menuKeyPrev_ = false;
    bool quitKeyPrev_ = false;
};
