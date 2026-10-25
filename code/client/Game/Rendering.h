namespace Game::Rendering
{
struct Context
{
    struct Device
    {
        IDXGISwapChain4* pSwapChain;
        uint8_t pad8[0xB0 - 0x8];
    };

    Context() = delete;
    ~Context() = delete;

    // Layout and instance lookup as in Cyber Engine Tweaks 1.38 (game 2.5). The old lookup decoded an instruction
    // inside GpuApi::Present, which moved in 2.5 and yielded a null context (crash on the first frame).
    static Context* GetInstance() noexcept
    {
        static Red::UniversalRelocPtr<Context*> s_instance(1239944840UL); // CRenderGlobal instance
        return *s_instance.GetAddr();
    }

    uint8_t pad0[0xCEBF48];
    Device devices[0x30]; // Count unknown, it is at least 0x20
    uint8_t pad[0x14104D0 - (0xCEBF48 + sizeof(devices))];
    ID3D12CommandQueue* pDirectQueue; // 0x14104D0
};

static_assert(sizeof(Context::Device) == 0xB0);
static_assert(offsetof(Context, devices) == 0xCEBF48);
static_assert(offsetof(Context, pDirectQueue) == 0x14104D0);

constexpr auto CRenderNode_Present_DoInternal = Core::RawFunc<
    /* hash = */ 2468877568UL,
    /* type = */ void* (*)(int32_t*, uint8_t, UINT)>();

constexpr auto CRenderGlobal_Resize = Core::RawFunc<
    /* hash = */ 239671909UL,
    /* type = */ void* (*)(uint32_t a1, uint32_t a2, uint32_t a3, uint8_t a4, int32_t* a5)>();

constexpr auto CRenderGlobal_Shutdown = Core::RawFunc<
    /* hash = */ 3192982283UL,
    /* type = */ void* (*)(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)>();
}