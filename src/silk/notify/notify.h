#pragma once
// Toast notifications.
//
//   Notify::Push(Notify::Type::Success, "Saved", "Configuration saved.", 4.0f);
//   ...
//   Notify::Draw();   // once per frame, after the rest of the UI
//
// Toasts slide in from the right on a spring, show a lifetime bar, pause
// while hovered, close on click and the remaining stack glides into place.

#include <string>

namespace silk::Notify {

enum class Type { Info, Success, Warning, Error };

void Push(Type type, std::string title, std::string message, float duration = 4.0f);
void Draw();
void Clear();
int Count();

} // namespace silk::Notify
