// Silk demo: a Nova-style settings menu that exercises every widget.

#include "demo_app.h"

#include "silk/core/utf8.h"

#include <d3d11.h>

#include <cmath>
#include <cstdio>
#include <random>
#include <sstream>

using namespace silk;

bool DemoApp::Init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* ctx, const std::string& assetDir) {
    hwnd_ = hwnd;
    ctx_ = ctx;
    SetCurrentContext(&ui_);
    if (!ui_.Init(device, ctx, assetDir)) return false;
    configPath_ = assetDir + "/../silk_config.json";
    LoadConfig();
    Notify::Push(Notify::Type::Info, "Welcome to Silk", "Insert opens / closes the menu.", 5.0f);

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (int i = 0; i < 80; ++i)
        particles_.push_back({{u(rng) * 1920.0f, u(rng) * 1080.0f}, {(u(rng) - 0.5f) * 18.0f, (u(rng) - 0.5f) * 18.0f}});
    return true;
}

void DemoApp::Shutdown() { SaveConfig(); }

bool DemoApp::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return ui_.HandleMessage(reinterpret_cast<HWND__*>(hwnd), msg, wp, lp);
}

void DemoApp::AfterPresent() {
    if (fpsCap_ > 0) {
        // Crude limiter to demonstrate frame-rate independent animation.
        static LARGE_INTEGER last{};
        LARGE_INTEGER f, now;
        QueryPerformanceFrequency(&f);
        const double target = 1.0 / fpsCap_;
        for (;;) {
            QueryPerformanceCounter(&now);
            const double el = double(now.QuadPart - last.QuadPart) / double(f.QuadPart);
            if (el >= target) break;
            if (target - el > 0.002) Sleep(1);
        }
        last = now;
    }
}

// ---------------------------------------------------------------------------
// Scripted input (screenshots)
// ---------------------------------------------------------------------------

void DemoApp::SetScript(std::string script) {
    std::stringstream ss(script);
    std::string tok;
    while (std::getline(ss, tok, ';')) {
        int f = 0, a = 0, b = 0;
        if (sscanf_s(tok.c_str(), "page=%d", &a) == 1) page_ = a;
        else if (sscanf_s(tok.c_str(), "tab=%d", &a) == 1) widgetTab_ = a;
        else if (sscanf_s(tok.c_str(), "theme=%d", &a) == 1) {
            themeIdx_ = a;
            ui_.Themes().Set(a == 0 ? Theme::Dark() : a == 1 ? Theme::Midnight() : Theme::Light(), false);
        } else if (sscanf_s(tok.c_str(), "menu=%d", &a) == 1) menuOpen_ = a != 0;
        else if (sscanf_s(tok.c_str(), "toasts@%d", &f) == 1) script_.push_back({f, ScriptEvent::Key, {}, -1});
        else if (sscanf_s(tok.c_str(), "click=%d,%d@%d", &a, &b, &f) == 3) {
            script_.push_back({f, ScriptEvent::Move, {float(a), float(b)}, 0});
            script_.push_back({f + 1, ScriptEvent::Down, {}, 0});
            script_.push_back({f + 3, ScriptEvent::Up, {}, 0});
        } else if (sscanf_s(tok.c_str(), "move=%d,%d@%d", &a, &b, &f) == 3) {
            script_.push_back({f, ScriptEvent::Move, {float(a), float(b)}, 0});
        } else if (sscanf_s(tok.c_str(), "down@%d", &f) == 1) {
            script_.push_back({f, ScriptEvent::Down, {}, 0});
        } else if (sscanf_s(tok.c_str(), "up@%d", &f) == 1) {
            script_.push_back({f, ScriptEvent::Up, {}, 0});
        } else if (sscanf_s(tok.c_str(), "key=%d@%d", &a, &f) == 2) {
            script_.push_back({f, ScriptEvent::Key, {}, a});
        } else if (tok.rfind("type=", 0) == 0) {
            const size_t at = tok.find('@');
            const std::string text = tok.substr(5, at - 5);
            f = std::atoi(tok.c_str() + at + 1);
            for (size_t i = 0, n = 0; i < text.size(); ++n)
                script_.push_back({f + int(n), ScriptEvent::Char, {}, int(utf8::Decode(text, i))});
        } else if (tok == "modal") {
            ui::OpenModal("Confirm action");
        }
    }
}

void DemoApp::RunScript() {
    Input& in = ui_.In();
    for (const ScriptEvent& e : script_) {
        if (e.frame != frame_) continue;
        switch (e.kind) {
        case ScriptEvent::Move: in.InjectMouseMove(e.pos); break;
        case ScriptEvent::Down: in.InjectMouseButton(MouseButton::Left, true); break;
        case ScriptEvent::Up: in.InjectMouseButton(MouseButton::Left, false); break;
        case ScriptEvent::Key:
            if (e.key == -1) { // demo helper: a burst of toasts
                Notify::Push(Notify::Type::Success, "Saved", "Configuration saved.");
                Notify::Push(Notify::Type::Warning, "Warning", "High latency: 140 ms.");
                Notify::Push(Notify::Type::Error, "Error", "Could not connect.");
            } else {
                in.InjectKey(e.key, true);
                in.InjectKey(e.key, false);
            }
            break;
        case ScriptEvent::Char: in.InjectChar(char32_t(e.key)); break;
        }
    }
}

// ---------------------------------------------------------------------------
// Background: navy gradient + drifting particle network
// ---------------------------------------------------------------------------

void DemoApp::DrawBackground(DrawList& dl, Rect screen) {
    const Theme& t = ui_.Style();
    const float dt = animatedBg_ ? ui_.Dt() : 0.0f;
    dl.RectGradient(screen, CornerColors::Vertical(Lerp(t.background, Color::Hex(0x10102A), 0.5f), t.background));
    dl.RadialGradient({screen.w * 0.18f, screen.h * 0.15f}, 520.0f, t.accent.WithAlpha(0.16f), t.accent.WithAlpha(0.0f));
    dl.RadialGradient({screen.w * 0.85f, screen.h * 0.85f}, 600.0f, t.accent2.WithAlpha(0.12f), t.accent2.WithAlpha(0.0f));

    for (Particle& p : particles_) {
        p.pos += p.vel * dt;
        if (p.pos.x < 0 || p.pos.x > screen.w) p.vel.x = -p.vel.x;
        if (p.pos.y < 0 || p.pos.y > screen.h) p.vel.y = -p.vel.y;
        p.pos.x = Clamp(p.pos.x, 0.0f, screen.w);
        p.pos.y = Clamp(p.pos.y, 0.0f, screen.h);
    }
    const Color lc = Lerp(t.accent2, t.accent, 0.5f);
    for (size_t i = 0; i < particles_.size(); ++i) {
        for (size_t j = i + 1; j < particles_.size(); ++j) {
            const float d = (particles_[i].pos - particles_[j].pos).Length();
            if (d < 150.0f) dl.Line(particles_[i].pos, particles_[j].pos, lc.WithAlpha(0.22f * (1.0f - d / 150.0f)), 1.0f);
        }
    }
    for (const Particle& p : particles_) dl.Circle(p.pos, 2.0f, lc.WithAlpha(0.7f));
}

// ---------------------------------------------------------------------------
// Pages
// ---------------------------------------------------------------------------

void DemoApp::PageHome() {
    ui::PageHeader("Home", "silk");
    ui::BeginColumns(2, 14.0f, -1.0f);
    ui::BeginColumn();
    ui::BeginCard("Welcome", {0, -1});
    ui::Text("Silk UI — a custom UI framework");
    ui::TextDim("C++20 · Win32 · DirectX 11");
    ui::TextDim("Only FreeType and stb_image.");
    ui::Spacing(4);
    const float load = std::fmod(float(ui_.Now()) * 0.12f, 1.0f);
    ui::ProgressBar("Loading modules", 0.25f + 0.75f * load);
    ui::Spinner("frame-rate independent animations");
    ui::Spacing(2);
    if (ui::Button("Open dialog", ui::ButtonVariant::Secondary, {-1, 34})) ui::OpenModal("Confirm action");
    ui::EndCard();
    ui::EndColumn();

    ui::BeginColumn();
    ui::BeginCard("Notifications", {0, -1});
    if (ui::Button("Info", ui::ButtonVariant::Secondary, {-1, 34}))
        Notify::Push(Notify::Type::Info, "Heads up", "This is a regular notification.");
    if (ui::Button("Success", ui::ButtonVariant::Secondary, {-1, 34}))
        Notify::Push(Notify::Type::Success, "Done", "The operation completed successfully.");
    if (ui::Button("Warning", ui::ButtonVariant::Secondary, {-1, 34}))
        Notify::Push(Notify::Type::Warning, "Careful", "Hover to pause the timer.", 6.0f);
    if (ui::Button("Error", ui::ButtonVariant::Secondary, {-1, 34}))
        Notify::Push(Notify::Type::Error, "Error", "Click a notification to dismiss it.");
    ui::Separator("QUICK OPTIONS");
    ui::Toggle("Watermark", &showWatermark_);
    ui::Toggle("Keybind list", &showKeybinds_);
    ui::Toggle("Animated background", &animatedBg_);
    ui::EndCard();
    ui::EndColumn();
    ui::EndColumns();
}

void DemoApp::PageWidgets() {
    ui::PageHeader("Widgets", "every control in the framework");
    static const char* tabs[] = {"Basics", "Input", "Colors"};
    ui::Tabs("tabs", &widgetTab_, tabs);
    ui::Spacing(4);
    ui::BeginTabContent("tabcontent", widgetTab_);
    ui::BeginChild("scroll", {0, 0});
    if (widgetTab_ == 0) {
        ui::BeginColumns(2, 24.0f);
        ui::BeginColumn();
        ui::Caption("CHECKBOXES");
        ui::Checkbox("Show labels", &checks_[0]);
        ui::Tooltip("Checkbox with an animated checkmark");
        ui::Checkbox("Show grid", &checks_[1]);
        ui::Checkbox("Show stats", &checks_[2]);
        ui::Spacing(6);
        ui::Caption("TOGGLES");
        ui::Toggle("Smoothing", &toggles_[0]);
        ui::Toggle("Quiet mode", &toggles_[1]);
        ui::BeginDisabled(!toggles_[0]);
        ui::Toggle("Depends on smoothing", &toggles_[2]);
        ui::EndDisabled();
        ui::EndColumn();
        ui::BeginColumn();
        ui::Caption("SLIDERS");
        ui::SliderFloat("Smoothness", &sliderF_, 0.0f, 1.0f, "%.2f");
        ui::SliderInt("Field of view", &sliderI_, 30, 180, "%d°");
        ui::Spacing(6);
        ui::Caption("LISTS");
        static const char* modes[] = {"Off", "Subtle", "Aggressive", "Custom"};
        ui::Combo("Mode", &combo_, modes);
        static const char* parts[] = {"Windows", "macOS", "Linux", "Android", "iOS"};
        ui::MultiSelect("Platforms", multi_, parts);
        ui::EndColumn();
        ui::EndColumns();
        ui::Spacing(6);
        ui::Caption("BUTTONS");
        ui::BeginColumns(4, 10.0f);
        ui::Button("Primary", ui::ButtonVariant::Primary, {-1, 34});
        ui::Button("Secondary", ui::ButtonVariant::Secondary, {-1, 34});
        ui::Button("Ghost", ui::ButtonVariant::Ghost, {-1, 34});
        ui::Button("Danger", ui::ButtonVariant::Danger, {-1, 34});
        ui::EndColumns();
    } else if (widgetTab_ == 1) {
        ui::Caption("TEXT");
        ui::InputField("search", &search_, "Search settings…", "search");
        ui::InputText("Username", &name_, "enter a name");
        ui::Spacing(6);
        ui::Caption("KEYBINDS");
        ui::Keybind("Open menu", &keyMenu_);
        ui::Keybind("Screenshot", &keyAim_);
        ui::Keybind("Toggle HUD", &keyEsp_);
        ui::Tooltip("Click, then press a key (Esc = none)");
    } else {
        ui::Caption("COLORS");
        ui::ColorEdit("Accent", &colorA_);
        ui::ColorEdit("Glow", &colorB_);
        ui::Spacing(6);
        ui::TextDim("Click a swatch to open the HSV + alpha picker.");
    }
    ui::EndChild();
    ui::EndTabContent();
}

void DemoApp::PageVisuals() {
    ui::PageHeader("Appearance", "theme, accent, effects");
    ui::BeginColumns(2, 14.0f, -1.0f);
    ui::BeginColumn();
    ui::BeginCard("Theme", {0, -1});
    static const char* themes[] = {"Dark", "Midnight", "Light"};
    if (ui::Combo("Theme", &themeIdx_, themes)) {
        const Theme t = themeIdx_ == 0 ? Theme::Dark() : themeIdx_ == 1 ? Theme::Midnight() : Theme::Light();
        ui_.Themes().Set(t);
    }
    if (ui::ColorEdit("Accent color", &colorA_, false))
        ui_.Themes().SetAccent(colorA_, ui_.Themes().Target().accent2);
    ui::Toggle("Glass effect", &glass_);
    ui::SliderInt("Blur strength", &blur_, 1, 6);
    ui::SliderFloat("Corner radius", &radius_, 0.0f, 16.0f, "%.0f px");
    ui::EndCard();
    ui::EndColumn();
    ui::BeginColumn();
    ui::BeginCard("Watermark", {0, -1});
    WatermarkConfig& wm = hud_.Watermark();
    static const char* corners[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
    ui::Combo("Position", &wm.corner, corners);
    ui::Toggle("User", &wm.showUser);
    ui::Toggle("FPS", &wm.showFps);
    ui::Toggle("Ping", &wm.showPing);
    ui::Toggle("Time", &wm.showTime);
    ui::SliderFloat("Opacity", &wm.opacity, 0.2f, 1.0f, "%.2f");
    ui::EndCard();
    ui::EndColumn();
    ui::EndColumns();
}

void DemoApp::PageKeybinds() {
    ui::PageHeader("Keybinds", "assign keys to actions");
    ui::BeginCard("Actions", {0, 0});
    ui::Keybind("Open menu##kb", &keyMenu_);
    ui::Keybind("Screenshot##kb", &keyAim_);
    ui::Keybind("Toggle HUD##kb", &keyEsp_);
    ui::EndCard();
}

void DemoApp::PageSettings() {
    ui::PageHeader("Settings", "performance and configuration");
    ui::BeginCard("Performance", {0, 0});
    ui::SliderInt("FPS limit (0 = vsync)", &fpsCap_, 0, 240);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.0f FPS · %d draw calls · %d vertices", ui_.Time().Fps(),
                  ui_.Gfx().Stats().drawCalls, ui_.Gfx().Stats().vertices);
    ui::TextDim(buf);
    std::snprintf(buf, sizeof(buf), "DPI scale: %.2f", ui_.Dpi());
    ui::TextDim(buf);
    ui::EndCard();
    ui::Spacing(6);
    ui::BeginCard("Configuration", {0, 0});
    ui::TextDim("HUD positions, watermark, theme and widget values (JSON).");
    ui::BeginColumns(2, 10.0f);
    if (ui::Button("Save", ui::ButtonVariant::Primary, {-1, 34})) {
        SaveConfig();
        Notify::Push(Notify::Type::Success, "Saved", "silk_config.json");
    }
    if (ui::Button("Load", ui::ButtonVariant::Secondary, {-1, 34})) {
        if (LoadConfig()) Notify::Push(Notify::Type::Info, "Loaded", "Configuration restored.");
        else Notify::Push(Notify::Type::Error, "Error", "No configuration file found.");
    }
    ui::EndColumns();
    ui::EndCard();
    if (overlay_) {
        ui::Spacing(6);
        ui::BeginCard("Overlay", {0, 0});
        ui::TextDim("Clicks outside the UI reach the apps below. End quits.");
        if (ui::Button("Close overlay", ui::ButtonVariant::Danger, {-1, 34})) quit_ = true;
        ui::EndCard();
    }
}

// ---------------------------------------------------------------------------
// HUD + config
// ---------------------------------------------------------------------------

void DemoApp::UpdateHud() {
    hud_.SetModule("Show labels", checks_[0]);
    hud_.SetModule("Show grid", checks_[1]);
    hud_.SetModule("Show stats", checks_[2]);
    hud_.SetModule("Smoothing", toggles_[0]);
    hud_.SetModule("Quiet mode", toggles_[1]);
    hud_.SetModule("Animated background", animatedBg_);
    hud_.SetModule("Glass", glass_);
    hud_.Watermark().visible = showWatermark_;
    hud_.Watermark().user = name_;
    if (Hud::Element* e = hud_.Find("keybinds")) e->visible = showKeybinds_;
    std::vector<std::pair<std::string, std::string>> rows;
    if (keyMenu_) rows.emplace_back("Menu", ui::KeyName(keyMenu_));
    if (keyAim_) rows.emplace_back("Screenshot", ui::KeyName(keyAim_));
    if (keyEsp_) rows.emplace_back("Toggle HUD", ui::KeyName(keyEsp_));
    hud_.SetKeybinds(std::move(rows));
    hud_.SetPing(int(24.0 + 6.0 * std::sin(ui_.Now() * 0.7)));
    hud_.Draw(ui_);
}

void DemoApp::SaveConfig() {
    json::Value v;
    v["hud"] = hud_.Save();
    json::Value& d = v["demo"];
    d["theme"] = themeIdx_;
    d["glass"] = glass_;
    d["blur"] = blur_;
    d["radius"] = radius_;
    d["animatedBg"] = animatedBg_;
    d["watermark"] = showWatermark_;
    d["keybindList"] = showKeybinds_;
    d["slider"] = sliderF_;
    d["fov"] = sliderI_;
    d["mode"] = combo_;
    d["name"] = name_;
    d["keyMenu"] = keyMenu_;
    d["keyAim"] = keyAim_;
    d["keyEsp"] = keyEsp_;
    for (int i = 0; i < 3; ++i) {
        d["checks"].Push(checks_[i]);
        d["toggles"].Push(toggles_[i]);
    }
    for (bool b : multi_) d["zones"].Push(b);
    for (float f : {colorA_.r, colorA_.g, colorA_.b, colorA_.a}) d["accent"].Push(f);
    json::SaveFile(configPath_, v);
}

bool DemoApp::LoadConfig() {
    json::Value v;
    if (!json::LoadFile(configPath_, v)) return false;
    hud_.Load(v.Get("hud"));
    const json::Value& d = v.Get("demo");
    themeIdx_ = std::clamp(d.Get("theme").AsInt(themeIdx_), 0, 2);
    glass_ = d.Get("glass").AsBool(glass_);
    blur_ = d.Get("blur").AsInt(blur_);
    radius_ = d.Get("radius").AsFloat(radius_);
    animatedBg_ = d.Get("animatedBg").AsBool(animatedBg_);
    showWatermark_ = d.Get("watermark").AsBool(showWatermark_);
    showKeybinds_ = d.Get("keybindList").AsBool(showKeybinds_);
    sliderF_ = d.Get("slider").AsFloat(sliderF_);
    sliderI_ = d.Get("fov").AsInt(sliderI_);
    combo_ = d.Get("mode").AsInt(combo_);
    name_ = d.Get("name").AsString(name_);
    keyMenu_ = d.Get("keyMenu").AsInt(keyMenu_);
    keyAim_ = d.Get("keyAim").AsInt(keyAim_);
    keyEsp_ = d.Get("keyEsp").AsInt(keyEsp_);
    const auto& ch = d.Get("checks").AsArray();
    const auto& tg = d.Get("toggles").AsArray();
    for (size_t i = 0; i < 3; ++i) {
        if (i < ch.size()) checks_[i] = ch[i].AsBool(checks_[i]);
        if (i < tg.size()) toggles_[i] = tg[i].AsBool(toggles_[i]);
    }
    const auto& zs = d.Get("zones").AsArray();
    for (size_t i = 0; i < zs.size() && i < std::size(multi_); ++i) multi_[i] = zs[i].AsBool(multi_[i]);
    const auto& ac = d.Get("accent").AsArray();
    if (ac.size() == 4) colorA_ = {ac[0].AsFloat(), ac[1].AsFloat(), ac[2].AsFloat(), ac[3].AsFloat()};
    const Theme t = themeIdx_ == 0 ? Theme::Dark() : themeIdx_ == 1 ? Theme::Midnight() : Theme::Light();
    ui_.Themes().Set(t, false);
    return true;
}

void DemoApp::DrawModal() {
    if (ui::BeginModal("Confirm action", {400, 0})) {
        ui::TextDim("A modal blocks input underneath and closes");
        ui::TextDim("on a backdrop click, Escape or a button.");
        ui::Spacing(6);
        ui::BeginColumns(2, 10.0f);
        if (ui::Button("Cancel", ui::ButtonVariant::Secondary, {-1, 34})) ui::CloseCurrentModal();
        if (ui::Button("Confirm", ui::ButtonVariant::Primary, {-1, 34})) ui::CloseCurrentModal();
        ui::EndColumns();
        ui::EndModal();
    }
}

void DemoApp::DrawMenu() {
    ui::SetNextWindowPos({250, 110});
    ui::SetNextWindowSize({860, 580});
    if (ui::Begin("Silk", &menuOpen_, ui::WindowFlags_NoTitleBar)) {
        ui::BeginSidebar(186.0f);
        ui::SidebarHeader("silk", "custom ui framework");
        ui::SidebarItem("house", "Home", &page_, 0);
        ui::SidebarItem("layout-list", "Widgets", &page_, 1);
        ui::SidebarItem("eye", "Appearance", &page_, 2);
        ui::SidebarItem("keyboard", "Keybinds", &page_, 3);
        ui::SidebarItem("settings", "Settings", &page_, 4);
        ui::SidebarUser(name_, "license: lifetime");
        ui::EndSidebar();

        ui::BeginTabContent("page", page_);
        switch (page_) {
        case 0: PageHome(); break;
        case 1: PageWidgets(); break;
        case 2: PageVisuals(); break;
        case 3: PageKeybinds(); break;
        default: PageSettings(); break;
        }
        ui::EndTabContent();
    }
    ui::End();
    DrawModal();
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void DemoApp::Frame(ID3D11RenderTargetView* rtv, int width, int height, float dpi, float fixedDt) {
    RunScript();
    if (overlay_) PollOverlayInput(dpi);
    ui_.NewFrame(width, height, dpi, fixedDt);
    ++frame_;

    // Keep a couple of demo settings in sync with the theme / renderer.
    Theme th = ui_.Themes().Target();
    if (th.glass != glass_ || std::abs(th.radius - radius_) > 0.01f) {
        th.glass = glass_;
        th.radius = radius_;
        th.radiusSm = std::max(0.0f, radius_ - 2.0f);
        th.radiusLg = radius_ + 4.0f;
        th.radiusXl = radius_ + 8.0f;
        ui_.Themes().Set(th);
    }
    ui_.Gfx().SetBlurStrength(blur_);

    // In overlay mode the menu key is read globally (PollOverlayInput) because
    // the window usually doesn't have keyboard focus.
    if (!overlay_ && ui_.In().KeyPressed(keyMenu_) && !ui_.KeybindWaiting()) menuOpen_ = !menuOpen_;
    ui_.menuOpen = menuOpen_;

    if (!overlay_) DrawBackground(ui_.Background(), ui_.Screen());
    DrawMenu();
    UpdateHud();
    Notify::Draw();

    // Overlay: clear to fully transparent so only the UI is composited over the desktop.
    const float clear[4] = {0, 0, 0, overlay_ ? 0.0f : 1.0f};
    ctx_->ClearRenderTargetView(rtv, clear);
    ui_.Render(rtv);
}

void DemoApp::PollOverlayInput(float dpi) {
    // A click-through window gets no WM_MOUSEMOVE, so feed the cursor position
    // every frame; this is what decides whether the next click is ours.
    POINT pt;
    if (GetCursorPos(&pt) && ScreenToClient(hwnd_, &pt))
        ui_.In().InjectMouseMove(Vec2(float(pt.x), float(pt.y)) / dpi);

    auto pressedGlobally = [](int vk, bool& prev) {
        const bool down = vk && (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool edge = down && !prev;
        prev = down;
        return edge;
    };
    if (pressedGlobally(keyMenu_, menuKeyPrev_) && !ui_.KeybindWaiting()) {
        menuOpen_ = !menuOpen_;
        if (menuOpen_) SetForegroundWindow(hwnd_); // so text fields receive typing
    }
    if (pressedGlobally(VK_END, quitKeyPrev_) && !ui_.KeybindWaiting()) quit_ = true;
}
