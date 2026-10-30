#pragma once
// Public immediate-mode API.
//
//   if (ui::Begin("Settings", &open)) {
//       ui::Toggle("Enable glow", &glow);
//       ui::SliderFloat("Radius", &radius, 0, 32);
//       if (ui::Button("Apply", ButtonVariant::Primary)) Apply();
//   }
//   ui::End();
//
// Labels may contain "##suffix" to make IDs unique without changing the text.

#include "silk/widgets/context.h"

#include <span>
#include <string>
#include <string_view>

namespace silk::ui {

// ---------------------------------------------------------------------------
// IDs / state
// ---------------------------------------------------------------------------
void PushID(std::string_view s);
void PushID(int i);
void PopID();
void BeginDisabled(bool disabled = true);
void EndDisabled();
// Shows a tooltip for the previously submitted item after a short hover.
void Tooltip(std::string_view text);
bool IsItemHovered();

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void BeginRow(const LayoutStyle& style = {Axis::Row});
void EndRow();
// Equal-width columns in a row. height: 0 = fit content, <0 = fill remaining space.
void BeginColumns(int count, float gap = -1.0f, float height = 0.0f);
void EndColumns();
// Column occupying `size` in the parent (non-positive = fill).
void BeginColumn(Vec2 size = {}, const LayoutStyle& style = {});
void EndColumn();
void Spacing(float amount = -1.0f);
Rect NextRect(Vec2 size);
Vec2 AvailableSpace();

// ---------------------------------------------------------------------------
// Text & decoration
// ---------------------------------------------------------------------------
void Text(std::string_view text);
void TextDim(std::string_view text);
void TextColored(Color c, std::string_view text);
void Heading(std::string_view text);        // section heading (medium weight)
void Title(std::string_view text);          // large title
void Caption(std::string_view text);        // small uppercase-ish caption
void Icon(std::string_view name, float size = 16.0f, Color color = {0, 0, 0, 0}); // alpha 0 = theme text
void Separator(std::string_view label = {});
// Page header: large title with a small subtitle underneath.
void PageHeader(std::string_view title, std::string_view subtitle = {});
// Labelled progress bar (fraction 0..1) and a spinner with text.
void ProgressBar(std::string_view label, float fraction);
void Spinner(std::string_view text);

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------
enum class ButtonVariant : uint8_t { Primary, Secondary, Ghost, Danger };

bool Button(std::string_view label, ButtonVariant variant = ButtonVariant::Secondary, Vec2 size = {});
bool IconButton(std::string_view icon, std::string_view idLabel, ButtonVariant variant = ButtonVariant::Ghost,
                float size = 32.0f);
bool Checkbox(std::string_view label, bool* value);
bool Toggle(std::string_view label, bool* value);
bool SliderFloat(std::string_view label, float* value, float min, float max, const char* fmt = "%.2f");
bool SliderInt(std::string_view label, int* value, int min, int max, const char* fmt = "%d");
bool Combo(std::string_view label, int* current, std::span<const char* const> items);
bool MultiSelect(std::string_view label, bool* selected, std::span<const char* const> items);
bool ColorEdit(std::string_view label, Color* color, bool alpha = true);
bool Keybind(std::string_view label, int* vk);
// Returns true when the text changed. `submitted` (optional) is set on Enter.
bool InputText(std::string_view label, std::string* text, std::string_view hint = {}, bool* submitted = nullptr);
// Same, without a label row: just the field.
bool InputField(std::string_view id, std::string* text, std::string_view hint = {}, std::string_view icon = {},
                bool* submitted = nullptr);

// Name for a virtual-key code ("F", "Mouse 4", "None").
std::string KeyName(int vk);

// ---------------------------------------------------------------------------
// Containers
// ---------------------------------------------------------------------------
enum WindowFlags : uint32_t {
    WindowFlags_None = 0,
    WindowFlags_NoTitleBar = 1 << 0,
    WindowFlags_NoResize = 1 << 1,
    WindowFlags_NoMove = 1 << 2,
    WindowFlags_NoGlass = 1 << 3,
};

void SetNextWindowPos(Vec2 pos);
void SetNextWindowSize(Vec2 size);
// Returns true when the window is visible (also during its close animation).
bool Begin(std::string_view title, bool* open = nullptr, uint32_t flags = WindowFlags_None);
void End();

// Vertical navigation strip on the left of the current window. The rest of
// the window becomes a raised content panel.
void BeginSidebar(float width = 186.0f);
void SidebarHeader(std::string_view brand, std::string_view subtitle = {});
bool SidebarItem(std::string_view icon, std::string_view label, int* selected, int index);
void SidebarSection(std::string_view label);
// User badge pinned to the bottom of the sidebar.
void SidebarUser(std::string_view name, std::string_view subtitle, Color status = {0.24f, 0.84f, 0.55f, 1.0f});
void EndSidebar();

// Segmented tab bar with a sliding indicator. Returns true when changed.
bool Tabs(std::string_view id, int* selected, std::span<const char* const> labels);
// Wrap the content of the selected tab: fades + slides when `selected` changes.
void BeginTabContent(std::string_view id, int selected);
void EndTabContent();

// Scrollable child region with inertial, spring-clamped scrolling.
void BeginChild(std::string_view id, Vec2 size = {}, bool border = false);
void EndChild();

// Card: rounded panel with an optional titled header (accent underline).
// size.y: 0 = fit content (previous frame), <0 = fill remaining space.
void BeginCard(std::string_view title, Vec2 size = {});
void EndCard();

// Modal dialog. Open with OpenModal(name); BeginModal returns true while visible.
void OpenModal(std::string_view name);
bool BeginModal(std::string_view name, Vec2 size = {420, 0});
void CloseCurrentModal();
void EndModal();

} // namespace silk::ui
