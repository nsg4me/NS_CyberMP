#pragma once

struct InterpolationComponent
{
    struct Timepoint
    {
        glm::vec3 Position{};
        glm::vec3 Rotation{};
        float Velocity{0.f};
        uint64_t Tick{0};
    };

    List<Timepoint> TimePoints{};
    Timepoint PreviousFrame{};

    // Diagnostics (CyberpunkMP.log)
    std::chrono::steady_clock::time_point LastReceived{};
    uint64_t Received{0};
    bool GapLogged{false};
};