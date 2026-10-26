#include "Overlay.h"

#include "ImGuiService.h"
#include "App/Steam/SteamLobby.h"

namespace
{
struct ToastEntry
{
    std::string text;
    std::chrono::steady_clock::time_point shown;
};

std::mutex s_toastLock;
std::vector<ToastEntry> s_toasts;
std::atomic<bool> s_mainMenu{false};

constexpr auto kToastLife = std::chrono::seconds(6);
constexpr int kSlots = 4;

// One colour per party slot (host first). The map markers use the same palette.
constexpr ImU32 kSlotColours[kSlots] = {
    IM_COL32(255, 85, 85, 255),  // red
    IM_COL32(94, 246, 255, 255), // cyan
    IM_COL32(243, 230, 0, 255),  // yellow
    IM_COL32(181, 101, 255, 255) // purple
};

constexpr ImGuiWindowFlags kPassive = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;

std::string Initials(const std::string& aName)
{
    for (const char c : aName)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return "?";
}

void DrawToasts(float aScale)
{
    std::vector<ToastEntry> toasts;
    {
        std::lock_guard _(s_toastLock);
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(s_toasts, [now](const ToastEntry& t) { return now - t.shown > kToastLife; });
        toasts = s_toasts;
    }
    if (toasts.empty())
        return;

    const auto& io = ImGui::GetIO();
    const auto now = std::chrono::steady_clock::now();

    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, 90.f * aScale}, ImGuiCond_Always, {0.5f, 0.f});
    ImGui::SetNextWindowBgAlpha(0.f);
    if (ImGui::Begin("##cpmp-toasts", nullptr, kPassive))
    {
        ImGui::SetWindowFontScale(1.5f * aScale);
        for (const auto& toast : toasts)
        {
            const float age = std::chrono::duration<float>(now - toast.shown).count();
            const float life = std::chrono::duration<float>(kToastLife).count();
            const float alpha = std::clamp(life - age, 0.f, 1.f);

            const ImVec2 size = ImGui::CalcTextSize(toast.text.c_str());
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float pad = 10.f * aScale;
            ImGui::GetWindowDrawList()->AddRectFilled({pos.x - pad, pos.y - pad * 0.5f},
                                                      {pos.x + size.x + pad, pos.y + size.y + pad * 0.5f},
                                                      IM_COL32(10, 12, 20, static_cast<int>(200 * alpha)), 4.f * aScale);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.37f, 0.96f, 1.f, alpha));
            ImGui::TextUnformatted(toast.text.c_str());
            ImGui::PopStyleColor();
            ImGui::Dummy({0.f, pad});
        }
    }
    ImGui::End();
}

void DrawParty(float aScale)
{
    auto& lobby = App::SteamLobby::Get();
    const auto party = lobby.GetSnapshot();
    if (party.members.empty())
        return;

    auto* imgui = Core::Container::Get<App::ImGuiService>().get();
    const float box = 88.f * aScale;
    const float gap = 14.f * aScale;
    const float border = 3.f * aScale;

    ImGui::SetNextWindowPos({40.f * aScale, 40.f * aScale}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.f * aScale, 12.f * aScale});
    if (ImGui::Begin("##cpmp-party", nullptr, kPassive))
    {
        ImGui::SetWindowFontScale(1.35f * aScale);
        ImGui::TextColored(ImVec4(1.f, 0.33f, 0.33f, 1.f), "PARTY");
        ImGui::SameLine();
        ImGui::TextColored(party.versionMismatch ? ImVec4(1.f, 0.6f, 0.2f, 1.f) : ImVec4(0.8f, 0.8f, 0.85f, 1.f), "%s",
                           party.status.c_str());

        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        for (int i = 0; i < kSlots; ++i)
        {
            const ImVec2 a{origin.x + i * (box + gap), origin.y + 6.f * aScale};
            const ImVec2 b{a.x + box, a.y + box};
            const bool filled = i < static_cast<int>(party.members.size());

            if (!filled)
            {
                draw->AddRect(a, b, IM_COL32(255, 255, 255, 60), 4.f * aScale, 0, 1.5f * aScale);
                const char* plus = "+";
                const ImVec2 ts = ImGui::CalcTextSize(plus);
                draw->AddText({a.x + (box - ts.x) * 0.5f, a.y + (box - ts.y) * 0.5f}, IM_COL32(255, 255, 255, 70), plus);
                continue;
            }

            const auto& member = party.members[i];
            const ImU32 colour = kSlotColours[i];

            draw->AddRectFilled(a, b, IM_COL32(20, 22, 32, 230), 4.f * aScale);
            if (auto texture = imgui ? imgui->GetAvatarTexture(member.avatarHandle) : std::optional<ImTextureID>{})
            {
                draw->AddImageRounded(*texture, {a.x + border, a.y + border}, {b.x - border, b.y - border}, {0, 0},
                                      {1, 1}, IM_COL32_WHITE, 3.f * aScale);
            }
            else
            {
                const auto initials = Initials(member.name);
                const ImVec2 ts = ImGui::CalcTextSize(initials.c_str());
                draw->AddText({a.x + (box - ts.x) * 0.5f, a.y + (box - ts.y) * 0.5f}, colour, initials.c_str());
            }
            draw->AddRect(a, b, colour, 4.f * aScale, 0, border);

            // Status dot: green connected, cyan in game, grey in menu
            const ImU32 dot = member.connected            ? IM_COL32(80, 230, 120, 255)
                              : member.state == "ingame" ? IM_COL32(94, 246, 255, 255)
                                                         : IM_COL32(150, 150, 160, 255);
            draw->AddCircleFilled({b.x - 9.f * aScale, a.y + 9.f * aScale}, 5.f * aScale, dot);
            if (member.isHost)
                draw->AddText({a.x + 6.f * aScale, a.y + 4.f * aScale}, IM_COL32(255, 210, 90, 255), "HOST");

            // Name under the box, clipped to the box width
            auto name = member.name.empty() ? std::string("...") : member.name;
            while (name.size() > 1 && ImGui::CalcTextSize(name.c_str()).x > box)
                name.pop_back();
            draw->AddText({a.x, b.y + 4.f * aScale}, member.isLocal ? colour : IM_COL32(230, 230, 235, 255),
                          name.c_str());
        }

        ImGui::Dummy({kSlots * (box + gap) - gap, box + 6.f * aScale + ImGui::GetTextLineHeight() + 8.f * aScale});
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
} // namespace

namespace App::Overlay
{
void Toast(std::string aText)
{
    spdlog::info("[Toast] {}", aText);
    std::lock_guard _(s_toastLock);
    s_toasts.push_back({std::move(aText), std::chrono::steady_clock::now()});
    if (s_toasts.size() > 5)
        s_toasts.erase(s_toasts.begin());
}

void SetMainMenuVisible(bool aVisible)
{
    s_mainMenu = aVisible;
}

bool IsMainMenuVisible()
{
    return s_mainMenu;
}

void Draw()
{
    const auto& io = ImGui::GetIO();
    if (io.DisplaySize.y <= 0.f)
        return;

    const float scale = io.DisplaySize.y / 1080.f;

    SteamLobby::Get().Update();

    DrawToasts(scale);
    if (s_mainMenu)
        DrawParty(scale);
}
} // namespace App::Overlay
