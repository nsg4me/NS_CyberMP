#pragma once

// One colour per party slot (0 = host). Party boxes, the pause-menu list and map markers all use these.
namespace App::PartyColours
{
struct Rgb
{
    float r, g, b;
};

inline constexpr Rgb kSlots[4] = {
    {1.00f, 0.33f, 0.33f}, // red
    {0.37f, 0.96f, 1.00f}, // cyan
    {0.95f, 0.90f, 0.00f}, // yellow
    {0.71f, 0.40f, 1.00f}  // purple
};

inline constexpr Rgb kUnknown = {0.85f, 0.85f, 0.90f};

inline Rgb ForSlot(int aSlot)
{
    return aSlot >= 0 && aSlot < 4 ? kSlots[aSlot] : kUnknown;
}
} // namespace App::PartyColours
