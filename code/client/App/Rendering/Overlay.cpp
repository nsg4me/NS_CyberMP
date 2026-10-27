#include "Overlay.h"

#include <fstream>

extern std::filesystem::path GCyberpunkMpLocation;

#include "ImGuiService.h"
#include "App/Steam/SteamLobby.h"
#include "App/Rendering/PartyColours.h"
#include "App/World/NetworkWorldSystem.h"
#include "App/Network/NetworkService.h"
#include "App/Steam/SteamRelay.h"
#include "App/Settings.h"
#include "App/World/SyncDiagnostics.h"

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
std::atomic<bool> s_pauseMenu{false};
std::atomic<bool> s_inGameMenu{false}; // any full-screen game menu (inventory, journal, ...): no nameplates
uint64_t s_confirmKick = 0;
bool s_confirmEnd = false;

constexpr auto kToastLife = std::chrono::seconds(6);
constexpr int kSlots = 4;

ImU32 SlotColour(int aSlot, int aAlpha = 255)
{
    const auto c = App::PartyColours::ForSlot(aSlot);
    return IM_COL32(static_cast<int>(c.r * 255), static_cast<int>(c.g * 255), static_cast<int>(c.b * 255), aAlpha);
}

constexpr ImGuiWindowFlags kPassive = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;

// Draggable panels remember where they were left (overlay.json next to the mod), stored as fractions of the screen
// so they stay in place across resolutions.
struct PanelPositions
{
    bool loaded = false;
    std::unordered_map<std::string, ImVec2> positions;
} s_panels;

std::filesystem::path PanelFile()
{
    return GCyberpunkMpLocation / "overlay.json";
}

void LoadPanels()
{
    if (s_panels.loaded)
        return;
    s_panels.loaded = true;
    try
    {
        std::ifstream in(PanelFile());
        if (!in)
            return;
        const auto json = nlohmann::json::parse(in);
        for (const auto& [name, value] : json.items())
        {
            if (value.is_array() && value.size() == 2)
                s_panels.positions[name] = {value[0].get<float>(), value[1].get<float>()};
        }
    }
    catch (...)
    {
    }
}

void SavePanels()
{
    try
    {
        nlohmann::json json;
        for (const auto& [name, pos] : s_panels.positions)
            json[name] = {pos.x, pos.y};
        std::ofstream(PanelFile()) << json.dump(2);
    }
    catch (...)
    {
    }
}

// Places a draggable panel: the saved spot if there is one, the default otherwise.
void PlacePanel(const char* acName, ImVec2 aDefault, ImVec2 aPivot)
{
    LoadPanels();
    const auto& io = ImGui::GetIO();
    if (const auto it = s_panels.positions.find(acName); it != s_panels.positions.end())
        ImGui::SetNextWindowPos({it->second.x * io.DisplaySize.x, it->second.y * io.DisplaySize.y}, ImGuiCond_Appearing);
    else
        ImGui::SetNextWindowPos(aDefault, ImGuiCond_Appearing, aPivot);
}

// After Begin(): keep the panel on screen and remember where it was dropped.
void TrackPanel(const char* acName)
{
    const auto& io = ImGui::GetIO();
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();

    ImVec2 clamped{std::clamp(pos.x, 0.f, std::max(0.f, io.DisplaySize.x - size.x)),
                   std::clamp(pos.y, 0.f, std::max(0.f, io.DisplaySize.y - size.y))};
    if (clamped.x != pos.x || clamped.y != pos.y)
        ImGui::SetWindowPos(clamped);

    if (ImGui::IsWindowFocused() && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        const ImVec2 relative{clamped.x / io.DisplaySize.x, clamped.y / io.DisplaySize.y};
        auto& saved = s_panels.positions[acName];
        if (saved.x != relative.x || saved.y != relative.y)
        {
            saved = relative;
            SavePanels();
        }
    }
}

// Panels can be dragged anywhere (click and hold on them); no title bar, no resizing.
constexpr ImGuiWindowFlags kPanel = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                    ImGuiWindowFlags_AlwaysAutoResize;

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

    PlacePanel("party", {40.f * aScale, 40.f * aScale}, {0.f, 0.f});
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.f * aScale, 12.f * aScale});
    if (ImGui::Begin("##cpmp-party", nullptr, kPanel))
    {
        TrackPanel("party");
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
            const ImU32 colour = SlotColour(i);

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
// Pause menu / map: the party with colours and distances; the host can remove players or end the session.
void DrawPauseParty(float aScale)
{
    auto& lobby = App::SteamLobby::Get();
    const auto party = lobby.GetSnapshot();
    if (!party.inLobby || party.members.empty())
        return;

    auto* world = Red::GetGameSystem<NetworkWorldSystem>();
    std::optional<Red::Vector4> me;
    std::vector<NetworkWorldSystem::RemotePlayer> remotes;
    if (world)
    {
        me = world->GetLocalPlayerPosition();
        remotes = world->GetRemotePlayers();
    }

    const auto& io = ImGui::GetIO();
    PlacePanel("pause", {io.DisplaySize.x - 40.f * aScale, 140.f * aScale}, {1.f, 0.f});
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.f * aScale, 12.f * aScale});
    constexpr ImGuiWindowFlags flags = kPanel;
    if (ImGui::Begin("##cpmp-pause-party", nullptr, flags))
    {
        TrackPanel("pause");
        ImGui::SetWindowFontScale(1.3f * aScale);
        ImGui::TextColored(ImVec4(1.f, 0.33f, 0.33f, 1.f), "PARTY");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", party.status.c_str());
        ImGui::Separator();

        for (size_t i = 0; i < party.members.size(); ++i)
        {
            const auto& member = party.members[i];
            const auto c = App::PartyColours::ForSlot(static_cast<int>(i));
            ImGui::PushID(static_cast<int>(i));

            // colour swatch (matches the map marker)
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetTextLineHeight();
            ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + h, p.y + h}, SlotColour(static_cast<int>(i)), 3.f);
            ImGui::Dummy({h, h});
            ImGui::SameLine();

            ImGui::TextColored(ImVec4(c.r, c.g, c.b, 1.f), "%s%s", member.name.c_str(), member.isLocal ? " (you)" : "");
            if (member.isHost)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.f, 0.82f, 0.35f, 1.f), "HOST");
            }

            // where they are
            std::string where = !member.activity.empty() ? member.activity
                                : member.connected       ? "connected"
                                                         : (member.state == "ingame" ? "in game" : "in menu");
            if (!member.isLocal && me)
            {
                for (const auto& remote : remotes)
                {
                    if (remote.steamId == member.steamId)
                    {
                        if (const auto pos = world->GetRemotePlayerPosition(remote.serverId))
                        {
                            const float dx = pos->X - me->X, dy = pos->Y - me->Y, dz = pos->Z - me->Z;
                            where = fmt::format("{}, {:.0f} m away", where, std::sqrt(dx * dx + dy * dy + dz * dz));
                        }
                        break;
                    }
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", where.c_str());

            if (party.isHost && !member.isLocal)
            {
                ImGui::SameLine();
                if (s_confirmKick == member.steamId)
                {
                    if (ImGui::SmallButton("Confirm remove"))
                    {
                        lobby.Kick(member.steamId);
                        s_confirmKick = 0;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Cancel"))
                        s_confirmKick = 0;
                }
                else if (ImGui::SmallButton("Remove"))
                {
                    s_confirmKick = member.steamId;
                }
            }
            ImGui::PopID();
        }

        if (!party.modMismatch.empty())
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.f, 0.6f, 0.2f, 1.f), "%s", party.modMismatch.c_str());
            ImGui::TextDisabled("Clothes/items from those mods may not show. The list is in CyberpunkMP.log.");
        }

        ImGui::Separator();
        ImGui::TextDisabled("Map: select a friend's marker and set a waypoint to follow them.");
        ImGui::TextDisabled("Placing your own waypoint shows it to the party.");

        if (party.isHost)
        {
            ImGui::Separator();
            if (s_confirmEnd)
            {
                ImGui::TextUnformatted("Send everyone back?");
                ImGui::SameLine();
                if (ImGui::SmallButton("End session"))
                {
                    lobby.EndSession();
                    s_confirmEnd = false;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Cancel##end"))
                    s_confirmEnd = false;
            }
            else if (ImGui::SmallButton("End session for everyone"))
            {
                s_confirmEnd = true;
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
// Connection health, refreshed twice a second (asking the network library every frame is wasteful).
struct NetHealth
{
    bool connected = false;
    int ping = 0;
    float quality = 1.f;
    std::chrono::steady_clock::time_point next{};
} s_net;

void RefreshNetHealth()
{
    const auto now = std::chrono::steady_clock::now();
    if (now < s_net.next)
        return;
    s_net.next = now + std::chrono::milliseconds(500);
    const auto service = Core::Container::Get<NetworkService>();
    s_net.connected = service && service->IsConnected();
    if (s_net.connected)
    {
        const auto status = service->GetConnectionStatus();
        s_net.ping = status.m_nPing;
        s_net.quality = std::min(status.m_flConnectionQualityLocal, status.m_flConnectionQualityRemote);
    }
}

ImU32 HealthColour()
{
    if (s_net.ping > 180 || s_net.quality < 0.85f)
        return IM_COL32(255, 80, 80, 255);
    if (s_net.ping > 90 || s_net.quality < 0.97f)
        return IM_COL32(255, 200, 70, 255);
    return IM_COL32(80, 230, 120, 255);
}

// Small connection dot + ping in the top-right corner while connected.
void DrawPingIndicator(float aScale)
{
    if (!s_net.connected || !Settings::Enabled("pingIndicator"))
        return;
    auto* draw = ImGui::GetBackgroundDrawList();
    const auto& io = ImGui::GetIO();
    const auto text = fmt::format("{} ms", s_net.ping);
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize() * 1.1f * aScale;
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.f, text.c_str());
    const float r = 5.f * aScale;
    const ImVec2 pos{io.DisplaySize.x - ts.x - 24.f * aScale, 10.f * aScale};
    draw->AddCircleFilled({pos.x - r - 6.f * aScale, pos.y + ts.y * 0.5f}, r, HealthColour());
    draw->AddText(font, size, {pos.x + 1, pos.y + 1}, IM_COL32(0, 0, 0, 160), text.c_str());
    draw->AddText(font, size, pos, IM_COL32(235, 235, 240, 230), text.c_str());
}

// Names over friends' heads in their party colour, with distance. Positions are projected with the game camera.
void DrawNameplates(float aScale)
{
    if (!s_net.connected || !Settings::Enabled("nameplates"))
        return;
    auto* world = Red::GetGameSystem<NetworkWorldSystem>();
    if (!world || !world->IsPlayerReady())
        return;
    const auto me = world->GetLocalPlayerPosition();
    if (!me)
        return;

    // The camera system through the game instance (GameInstance.GetCameraSystem needs a GameInstance argument).
    static const auto* s_cameraType = Red::CRTTISystem::Get()->GetClass("gameICameraSystem");
    auto* camera = s_cameraType ? Red::CGameEngine::Get()->framework->gameInstance->GetSystem(s_cameraType) : nullptr;
    if (!camera)
    {
        if (SyncDiagnostics::Every("nameplate-no-camera", std::chrono::seconds(30)))
            spdlog::warn("[Overlay] nameplates: camera system not found");
        return;
    }
    Red::Vector4 forward{};
    Red::CallVirtual(camera, "GetActiveCameraForward", forward);

    const float maxDistance = Settings::Option("nameplateDistance");
    const bool showDistance = Settings::Enabled("nameplateShowDistance");
    const auto& io = ImGui::GetIO();
    auto* draw = ImGui::GetBackgroundDrawList();
    ImFont* font = ImGui::GetFont();
    auto& lobby = App::SteamLobby::Get();

    for (const auto& remote : world->GetRemotePlayers())
    {
        const auto pos = world->GetRemotePlayerPosition(remote.serverId);
        if (!pos)
            continue;
        const float dx = pos->X - me->X, dy = pos->Y - me->Y, dz = pos->Z - me->Z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance > maxDistance || distance < 0.5f)
            continue;
        if (dx * forward.X + dy * forward.Y + dz * forward.Z <= 0.f)
            continue; // behind the camera

        const Red::Vector4 head{pos->X, pos->Y, pos->Z + 2.05f, 1.f};
        Red::Vector4 projected{};
        if (!Red::CallVirtual(camera, "ProjectPoint", projected, head))
            continue;
        float nx = projected.X, ny = projected.Y;
        if ((std::abs(nx) > 2.f || std::abs(ny) > 2.f) && std::abs(projected.W) > 0.0001f)
        {
            nx /= projected.W;
            ny /= projected.W;
        }
        if (Settings::Get().debugMode && SyncDiagnostics::Every("nameplate-projection", std::chrono::seconds(10)))
            spdlog::debug("[Overlay] nameplate projection for {}: raw ({:.3f}, {:.3f}, {:.3f}, {:.3f})", remote.name,
                          projected.X, projected.Y, projected.Z, projected.W);
        if (nx < -1.1f || nx > 1.1f || ny < -1.1f || ny > 1.1f)
            continue;
        const ImVec2 screen{(nx + 1.f) * 0.5f * io.DisplaySize.x, (1.f - ny) * 0.5f * io.DisplaySize.y};

        const int slot = remote.steamId ? lobby.SlotOf(remote.steamId) : -1;
        const ImU32 colour = SlotColour(slot);
        const float closeness = std::clamp(1.f - distance / std::max(maxDistance, 1.f), 0.f, 1.f);
        const float size = ImGui::GetFontSize() * (1.0f + 0.6f * closeness) * aScale;
        const std::string name = remote.name.empty() ? std::string("Player") : remote.name;
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.f, name.c_str());
        const ImVec2 at{screen.x - ts.x * 0.5f, screen.y - ts.y};
        for (const ImVec2 o : {ImVec2{1, 1}, ImVec2{-1, 1}, ImVec2{1, -1}, ImVec2{-1, -1}})
            draw->AddText(font, size, {at.x + o.x * aScale, at.y + o.y * aScale}, IM_COL32(0, 0, 0, 200), name.c_str());
        draw->AddText(font, size, at, colour, name.c_str());

        if (showDistance)
        {
            const auto label = fmt::format("{:.0f} m", distance);
            const float labelSize = size * 0.7f;
            const ImVec2 ls = font->CalcTextSizeA(labelSize, FLT_MAX, 0.f, label.c_str());
            const ImVec2 lat{screen.x - ls.x * 0.5f, screen.y + 2.f * aScale};
            draw->AddText(font, labelSize, {lat.x + aScale, lat.y + aScale}, IM_COL32(0, 0, 0, 200), label.c_str());
            draw->AddText(font, labelSize, lat, IM_COL32(230, 230, 235, 220), label.c_str());
        }
    }
}

// Pause menu: what the mod is doing and where to look when something is wrong.
void DrawInfoPanel(float aScale)
{
    if (!Settings::Enabled("infoPanel"))
        return;
    static std::string s_version;
    if (s_version.empty())
    {
        std::ifstream in(GCyberpunkMpLocation / "version.txt");
        std::getline(in, s_version);
        if (s_version.empty())
            s_version = "dev";
    }

    const auto& settings = Settings::Get();
    std::string mode;
    if (settings.host)
        mode = "Hosting";
    else if (settings.steamHost)
        mode = "Joining through the Steam relay";
    else if (settings.enabled)
        mode = fmt::format("Joining {}:{}", settings.ip.c_str(), settings.port);
    else
        mode = "Offline";

    PlacePanel("info", {40.f * aScale, 140.f * aScale}, {0.f, 0.f});
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14.f * aScale, 12.f * aScale});
    if (ImGui::Begin("##cpmp-info", nullptr, kPanel))
    {
        TrackPanel("info");
        ImGui::SetWindowFontScale(1.15f * aScale);
        ImGui::TextColored(ImVec4(1.f, 0.33f, 0.33f, 1.f), "NS-CyberMP");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", s_version.c_str());
        ImGui::Separator();
        ImGui::Text("%s", mode.c_str());
        if (s_net.connected)
        {
            const ImU32 c = HealthColour();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(c), "Connected: %d ms, quality %.0f%%", s_net.ping,
                               s_net.quality * 100.f);
        }
        else
            ImGui::TextDisabled("Not connected to a server");
        ImGui::Text("Steam relay: %s", App::SteamRelay::Get().IsAvailable() ? "available" : "not available");
        ImGui::Text("Debug mode: %s%s", settings.debugMode ? "on" : "off",
                    settings.debugModeFromLaunchOption ? " (launch option)" : "");
        ImGui::Separator();
        ImGui::TextDisabled("Options: Mod Settings > NS-CyberMP");
        ImGui::TextDisabled("Something wrong? Type /mark <what happened> in chat,");
        ImGui::TextDisabled("then use Collect logs in the launcher.");
        const auto log = (GCyberpunkMpLocation / "CyberpunkMP.log").string();
        ImGui::TextDisabled("Log: %s", log.c_str());
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

void SetInGameMenuVisible(bool aVisible)
{
    s_inGameMenu = aVisible;
}

void SetPauseMenuVisible(bool aVisible)
{
    s_pauseMenu = aVisible;
    if (!aVisible)
    {
        s_confirmKick = 0;
        s_confirmEnd = false;
    }
}

void Draw()
{
    const auto& io = ImGui::GetIO();
    if (io.DisplaySize.y <= 0.f)
        return;

    const float scale = io.DisplaySize.y / 1080.f;

    SteamLobby::Get().Update();
    RefreshNetHealth();

    if (!s_mainMenu && !s_pauseMenu && !s_inGameMenu)
        DrawNameplates(scale);
    if (!s_mainMenu)
        DrawPingIndicator(scale);

    DrawToasts(scale);
    if (s_mainMenu)
    {
        if (Settings::Enabled("partyPanels"))
            DrawParty(scale);
    }
    else if (s_pauseMenu)
    {
        if (Settings::Enabled("partyPanels"))
            DrawPauseParty(scale);
        DrawInfoPanel(scale);
    }
}
} // namespace App::Overlay
