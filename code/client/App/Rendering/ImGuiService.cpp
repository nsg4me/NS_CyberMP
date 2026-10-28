#include "ImGuiService.h"

#include "dx12.h"
#include "win32.h"
#include "App/Application.h"
#include "App/Debugging/DebugService.h"
#include "App/Rendering/Overlay.h"
#include "App/Steam/SteamLobby.h"

namespace App
{
struct AvatarTexture
{
    UINT slot = 0;
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    bool ready = false;
    bool failed = false;
};

ImGuiService::ImGuiService()
{
}

ImGuiService::~ImGuiService()
{
}

void ImGuiService::OnRenderInit()
{
    Microsoft::WRL::ComPtr<ID3D12Device> pDevice;
    if (FAILED(GetSwapChain()->GetDevice(IID_PPV_ARGS(&pDevice))))
    {
        return;
    }

    DXGI_SWAP_CHAIN_DESC sdesc;
    GetSwapChain()->GetDesc(&sdesc);

    const auto buffersCounts = std::min(sdesc.BufferCount, 3u);
    m_frameContexts.resize(buffersCounts);

    D3D12_DESCRIPTOR_HEAP_DESC srvdesc = {};
    srvdesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    // Slots after the ones ImGui uses hold the party avatars.
    srvdesc.NumDescriptors = buffersCounts + kAvatarSlots;
    m_avatarSlotBase = buffersCounts;
    srvdesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(pDevice->CreateDescriptorHeap(&srvdesc, IID_PPV_ARGS(&m_pSrvDescHeap))))
    {
        LogError("RenderingProvider::Initialize() - failed to create SRV descriptor heap!");
        OnReset();
        return;
    }
    m_pDevice = pDevice;
    m_srvIncrement = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC rtvdesc;
    rtvdesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvdesc.NumDescriptors = buffersCounts;
    rtvdesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvdesc.NodeMask = 1;

    if (FAILED(pDevice->CreateDescriptorHeap(&rtvdesc, IID_PPV_ARGS(&m_pRtvDescHeap))))
    {
        LogError("RenderingProvider::Initialize() - failed to create RTV descriptor heap!");
        OnReset();
        return;
    }

    const SIZE_T rtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_pRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < buffersCounts; i++)
    {
        auto& context = m_frameContexts[i];
        context.MainRenderTargetDescriptor = rtvHandle;
        GetSwapChain()->GetBuffer(i, IID_PPV_ARGS(&context.BackBuffer));
        pDevice->CreateRenderTargetView(context.BackBuffer.Get(), nullptr, context.MainRenderTargetDescriptor);
        rtvHandle.ptr += rtvDescriptorSize;
    }

    for (auto& context : m_frameContexts)
        if (FAILED(pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&context.CommandAllocator))))
        {
            LogError("RenderingProvider::Initialize() - failed to create command allocator!");
            OnReset();
            return;
        }

    if (FAILED(pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frameContexts[0].CommandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_pCommandList))) ||
        FAILED(m_pCommandList->Close()))
    {
        LogError("RenderingProvider::Initialize() - failed to create command list!");
        OnReset();
        return;
    }

    {
        std::lock_guard _(m_imguiLock);

        // TODO - scale also by DPI
        const auto [resx, resy] = std::tie(sdesc.BufferDesc.Width, sdesc.BufferDesc.Height);
        const auto scaleFromReference = std::min(static_cast<float>(resx) / 1920.0f, static_cast<float>(resy) / 1080.0f);

        auto& io = ImGui::GetIO();
        // Never touch the Windows cursor: the game hides it and draws its own. ImGui's backend set the arrow cursor
        // every frame, which left the real mouse pointer visible on top of the game.
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
       // io.Fonts->Clear();
        #if 0
        // tmp:
        struct FontSettings
        {
            std::string Path{};
            std::string Language{"Default"};
            float BaseSize{18.0f};
            int32_t OversampleHorizontal{3};
            int32_t OversampleVertical{1};
        } fontSettings;

        ImFontConfig config;
       // const auto& fontSettings = m_options.Font;
        config.SizePixels = std::floorf(fontSettings.BaseSize * scaleFromReference);
        config.OversampleH = fontSettings.OversampleHorizontal;
        config.OversampleV = fontSettings.OversampleVertical;
        if (config.OversampleH == 1 && config.OversampleV == 1)
            config.PixelSnapH = true;
        config.MergeMode = false;
        #endif

        if (ImGui::GetCurrentContext() == nullptr)
        {
            // do this once, do not repeat context creation!
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();

            // TODO - make this configurable eventually and overridable by mods for themselves easily
            // setup CET default style
            ImGui::StyleColorsDark(&m_styleReference);
            m_styleReference.WindowRounding = 6.0f;
            m_styleReference.WindowTitleAlign.x = 0.5f;
            m_styleReference.ChildRounding = 6.0f;
            m_styleReference.PopupRounding = 6.0f;
            m_styleReference.FrameRounding = 6.0f;
            m_styleReference.ScrollbarRounding = 12.0f;
            m_styleReference.GrabRounding = 12.0f;
            m_styleReference.TabRounding = 6.0f;
        }

        ImGui::GetStyle() = m_styleReference;
        ImGui::GetStyle().ScaleAllSizes(scaleFromReference);

        if (!ImGui_ImplWin32_Init(GetWindow()))
        {
            LogError("RenderingProvider::Initialize() - ImGui_ImplWin32_Init call failed!");
            return;
        }

        if (!ImGui_ImplDX12_Init(
                pDevice.Get(), static_cast<int>(buffersCounts), DXGI_FORMAT_R8G8B8A8_UNORM, m_pSrvDescHeap.Get(), m_pSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
                m_pSrvDescHeap->GetGPUDescriptorHandleForHeapStart()))
        {
            LogError("RenderingProvider::Initialize() - ImGui_ImplDX12_Init call failed!");
            ImGui_ImplWin32_Shutdown();
            return;
        }

        // ReloadFonts();

        if (!ImGui_ImplDX12_CreateDeviceObjects())
        {
            LogError("RenderingProvider::Initialize() - ImGui_ImplDX12_CreateDeviceObjects call failed!");
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            return;
        }
    }

    m_initialized = true;
}

void ImGuiService::OnPresent()
{
    if (!m_initialized)
        return;

    // swap staging ImGui buffer with render ImGui buffer
    {
        std::lock_guard _(m_imguiLock);
        ImGui_ImplDX12_NewFrame();
        if (m_imguiDrawDataBuffers[1].Valid)
        {
            std::swap(m_imguiDrawDataBuffers[0], m_imguiDrawDataBuffers[1]);
            m_imguiDrawDataBuffers[1].Valid = false;
        }
    }

    if (!m_imguiDrawDataBuffers[0].Valid)
        return;

    UploadPendingTextures();

    const auto bufferIndex = GetSwapChain()->GetCurrentBackBufferIndex();
    auto& frameContext = m_frameContexts[bufferIndex];
    frameContext.CommandAllocator->Reset();

    D3D12_RESOURCE_BARRIER barrier;
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = frameContext.BackBuffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    ID3D12DescriptorHeap* heaps[] = {m_pSrvDescHeap.Get()};

    m_pCommandList->Reset(frameContext.CommandAllocator.Get(), nullptr);
    m_pCommandList->ResourceBarrier(1, &barrier);
    m_pCommandList->SetDescriptorHeaps(1, heaps);
    m_pCommandList->OMSetRenderTargets(1, &frameContext.MainRenderTargetDescriptor, FALSE, nullptr);

    ImGui_ImplDX12_RenderDrawData(&m_imguiDrawDataBuffers[0], m_pCommandList.Get());

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_pCommandList->ResourceBarrier(1, &barrier);
    m_pCommandList->Close();

    ID3D12CommandList* commandLists[] = {m_pCommandList.Get()};
    GetCommandQueue()->ExecuteCommandLists(1, commandLists);
}

void ImGuiService::OnReset(bool aCompleteReset)
{
    if (m_initialized)
    {
        std::lock_guard _(m_imguiLock);

        for (auto& drawData : m_imguiDrawDataBuffers)
        {
            for (auto i = 0; i < drawData.CmdListsCount; ++i)
                IM_DELETE(drawData.CmdLists[i]);
            drawData.Clear();
        }

        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();

        if (aCompleteReset)
            ImGui::DestroyContext();
    }

    m_frameContexts.clear();

    {
        // The textures live in the SRV heap being released; they are uploaded again after the next init.
        std::lock_guard _(m_imguiLock);
        m_avatars.clear();
    }
    m_pDevice.Reset();

    m_pRtvDescHeap.Reset();
    m_pSrvDescHeap.Reset();
    m_pCommandList.Reset();

    m_initialized = false;
}

bool ImGuiService::OnWindowProc(HWND ahWnd, UINT auMsg, WPARAM awParam, LPARAM alParam)
{
    if (m_initialized)
    {
        if (const auto res = ImGui_ImplWin32_WndProcHandler(ahWnd, auMsg, awParam, alParam))
            return res != 0;
    }

    return false;
}

void ImGuiService::OnBootstrap()
{
    
}

void ImGuiService::OnGameUpdate(RED4ext::CGameApplication* apApp)
{
    PrepareUpdate();
}

void ImGuiService::PrepareUpdate()
{
    if (!m_pCommandList || !m_initialized)
        return;

    std::lock_guard _(m_imguiLock);

    DXGI_SWAP_CHAIN_DESC sdesc;
    GetSwapChain()->GetDesc(&sdesc);

    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    Core::Container::Get<DebugService>()->Draw();
    Overlay::Draw();
  
    ImGui::Render();

    auto& drawData = m_imguiDrawDataBuffers[2];

    for (auto i = 0; i < drawData.CmdListsCount; ++i)
        IM_DELETE(drawData.CmdLists[i]);
    drawData.Clear();

    drawData = *ImGui::GetDrawData();

    ImVector<ImDrawList*> copiedDrawLists;
    for (auto i = 0; i < drawData.CmdListsCount; ++i)
        copiedDrawLists.push_back(drawData.CmdLists[i]->CloneOutput());
    drawData.CmdLists = copiedDrawLists;

    std::swap(m_imguiDrawDataBuffers[1], m_imguiDrawDataBuffers[2]);
}

std::optional<ImTextureID> ImGuiService::GetAvatarTexture(int aSteamImageHandle)
{
    if (aSteamImageHandle <= 0 || !m_initialized)
        return std::nullopt;

    std::lock_guard _(m_imguiLock);

    auto it = m_avatars.find(aSteamImageHandle);
    if (it == m_avatars.end())
    {
        if (m_avatars.size() >= kAvatarSlots)
            m_avatars.clear(); // more avatars than slots over a session: start over

        auto texture = std::make_shared<AvatarTexture>();
        if (!SteamLobby::Get().GetAvatar(aSteamImageHandle, texture->rgba, texture->width, texture->height))
            return std::nullopt; // Steam has not downloaded it yet; asked again next frame

        // Lowest free slot
        for (UINT slot = 0; slot < kAvatarSlots; ++slot)
        {
            if (std::none_of(m_avatars.begin(), m_avatars.end(), [slot](const auto& e) { return e.second->slot == slot; }))
            {
                texture->slot = slot;
                break;
            }
        }
        it = m_avatars.emplace(aSteamImageHandle, texture).first;
    }

    const auto& texture = *it->second;
    if (!texture.ready)
        return std::nullopt;
    return (ImTextureID)(texture.gpu.ptr);
}

void ImGuiService::UploadPendingTextures()
{
    std::lock_guard _(m_imguiLock);
    for (auto& [handle, texture] : m_avatars)
    {
        if (!texture->ready && !texture->failed)
        {
            texture->failed = !UploadTexture(*texture);
            texture->ready = !texture->failed;
            if (texture->failed)
                LogError("ImGuiService - avatar texture upload failed");
        }
    }
}

// Standard D3D12 texture upload: copy the pixels through an upload buffer on the game's direct queue and wait for it
// (a 64x64 avatar, once per player), then describe it in the avatar's SRV slot.
bool ImGuiService::UploadTexture(AvatarTexture& aTexture)
{
    if (!m_pDevice || !m_pSrvDescHeap || aTexture.rgba.empty())
        return false;

    D3D12_HEAP_PROPERTIES defaultHeap{};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = aTexture.width;
    desc.Height = aTexture.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    if (FAILED(m_pDevice->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))))
        return false;

    const UINT pitch = (aTexture.width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                       ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    const UINT uploadSize = aTexture.height * pitch;

    D3D12_HEAP_PROPERTIES uploadHeap{};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = uploadSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    if (FAILED(m_pDevice->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))))
        return false;

    void* mapped = nullptr;
    D3D12_RANGE noRead{0, 0};
    if (FAILED(upload->Map(0, &noRead, &mapped)))
        return false;
    for (UINT y = 0; y < aTexture.height; ++y)
        std::memcpy(static_cast<uint8_t*>(mapped) + y * pitch, aTexture.rgba.data() + y * aTexture.width * 4,
                    aTexture.width * 4);
    upload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = aTexture.width;
    src.PlacedFootprint.Footprint.Height = aTexture.height;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = pitch;

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = texture.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    if (FAILED(m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
        FAILED(m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                            IID_PPV_ARGS(&list))) ||
        FAILED(m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
        return false;

    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    list->ResourceBarrier(1, &barrier);
    list->Close();

    ID3D12CommandList* lists[] = {list.Get()};
    GetCommandQueue()->ExecuteCommandLists(1, lists);
    GetCommandQueue()->Signal(fence.Get(), 1);

    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event)
        return false;
    fence->SetEventOnCompletion(1, event);
    WaitForSingleObject(event, 2000);
    CloseHandle(event);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    const UINT index = m_avatarSlotBase + aTexture.slot;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_pSrvDescHeap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(index) * m_srvIncrement;
    m_pDevice->CreateShaderResourceView(texture.Get(), &srv, cpu);

    aTexture.gpu = m_pSrvDescHeap->GetGPUDescriptorHandleForHeapStart();
    aTexture.gpu.ptr += static_cast<UINT64>(index) * m_srvIncrement;
    aTexture.resource = texture;
    aTexture.rgba.clear();
    aTexture.rgba.shrink_to_fit();
    return true;
}
}

