#pragma once

// What the mod draws on top of the game with ImGui: toasts (join/leave, connection) everywhere, and the party boxes
// on the title screen only.
namespace App::Overlay
{
// Thread-safe; shown for a few seconds.
void Toast(std::string aText);

// Set by the title-screen script when it opens/closes; the party boxes only show while it is up.
void SetMainMenuVisible(bool aVisible);
bool IsMainMenuVisible();

// Called while building the ImGui frame (game thread).
void Draw();
} // namespace App::Overlay
