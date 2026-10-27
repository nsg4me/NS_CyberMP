#include "CrashHandler.h"

#include <App/Settings.h>
#include <DbgHelp.h>
#include <algorithm>
#include <cctype>

extern std::filesystem::path GCyberpunkMpLocation;

namespace
{
LPTOP_LEVEL_EXCEPTION_FILTER s_gameFilter = nullptr;
std::atomic<bool> s_writing{false};
std::atomic<int> s_firstChanceLogged{0};
std::atomic<uintptr_t> s_dumpedAddress{0};

using SetUnhandledExceptionFilter_t = LPTOP_LEVEL_EXCEPTION_FILTER(WINAPI*)(LPTOP_LEVEL_EXCEPTION_FILTER);
SetUnhandledExceptionFilter_t s_realSetFilter = nullptr;

const char* ExceptionName(DWORD aCode)
{
    switch (aCode)
    {
    case EXCEPTION_ACCESS_VIOLATION: return "access violation";
    case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
    case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
    case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
    case 0xC0000374: return "heap corruption";
    case 0xC0000409: return "stack buffer overrun / fail-fast";
    case 0xE06D7363: return "C++ exception";
    default: return "exception";
    }
}

bool IsFatal(DWORD aCode)
{
    switch (aCode)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case 0xC0000374:
    case 0xC0000409:
        return true;
    default:
        return false;
    }
}

// "Cyberpunk2077.exe+0x1234abc" (module file name + offset), the form a PDB / address list can be looked up with.
std::string Describe(uintptr_t aAddress)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(aAddress), &module) ||
        !module)
        return fmt::format("0x{:x} (no module)", aAddress);

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    return fmt::format("{}+0x{:x}", std::filesystem::path(path).filename().string(),
                       aAddress - reinterpret_cast<uintptr_t>(module));
}

bool InOurModule(uintptr_t aAddress)
{
    HMODULE module = nullptr;
    HMODULE ours = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(aAddress), &module);
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&InOurModule), &ours);
    return module && module == ours;
}

void LogStack(const CONTEXT& aContext)
{
    // Walk the faulting thread's stack from the exception context (x64 unwind data, no dbghelp needed).
    CONTEXT context = aContext;
    for (int frame = 0; frame < 40 && context.Rip; ++frame)
    {
        spdlog::error("[Crash]   #{:02} {}", frame, Describe(context.Rip));

        DWORD64 imageBase = 0;
        const auto* function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
        if (!function)
        {
            // Leaf function: the return address is on top of the stack.
            context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
            context.Rsp += 8;
            continue;
        }

        void* handlerData = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, const_cast<PRUNTIME_FUNCTION>(function), &context,
                         &handlerData, &establisher, nullptr);
    }
}

void WriteDump(EXCEPTION_POINTERS* apInfo)
{
    using MiniDumpWriteDump_t = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                              PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    const auto dbghelp = LoadLibraryW(L"dbghelp.dll");
    const auto write = dbghelp ? reinterpret_cast<MiniDumpWriteDump_t>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
    if (!write)
        return;

    std::error_code ec;
    const auto folder = GCyberpunkMpLocation / "crashes";
    std::filesystem::create_directories(folder, ec);

    SYSTEMTIME time;
    GetLocalTime(&time);
    const auto file = folder / fmt::format("crash-{:04}{:02}{:02}-{:02}{:02}{:02}.dmp", time.wYear, time.wMonth,
                                           time.wDay, time.wHour, time.wMinute, time.wSecond);

    const HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return;

    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), apInfo, FALSE};
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory |
                                                 MiniDumpScanMemory);
    const bool ok = write(GetCurrentProcess(), GetCurrentProcessId(), handle, type, &info, nullptr, nullptr);
    CloseHandle(handle);
    spdlog::error("[Crash] crash dump {}: {}", ok ? "saved" : "FAILED", file.string());
}

void Report(EXCEPTION_POINTERS* apInfo, const char* acKind)
{
    if (s_writing.exchange(true))
        return; // a crash inside the crash report: stop

    const auto* record = apInfo->ExceptionRecord;
    const auto address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);

    std::string detail;
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
        detail = fmt::format(" ({} address 0x{:x})",
                             record->ExceptionInformation[0] == 0 ? "reading" : (record->ExceptionInformation[0] == 1 ? "writing" : "executing"),
                             record->ExceptionInformation[1]);

    spdlog::error("[Crash] ================= {} =================", acKind);
    spdlog::error("[Crash] {} 0x{:08X}{} at {} on thread {}", ExceptionName(record->ExceptionCode),
                  record->ExceptionCode, detail, Describe(address), GetCurrentThreadId());
    const auto* c = apInfo->ContextRecord;
    spdlog::error("[Crash] rax={:x} rbx={:x} rcx={:x} rdx={:x} rsi={:x} rdi={:x} rsp={:x} rbp={:x} r8={:x} r9={:x}",
                  c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->Rsp, c->Rbp, c->R8, c->R9);
    spdlog::error("[Crash] call stack (newest first):");
    LogStack(*c);
    spdlog::default_logger()->flush();

    // One dump per fault (the same fault can come through the vectored handler and then the unhandled filter).
    if (s_dumpedAddress.exchange(address) != address)
    {
        WriteDump(apInfo);
        spdlog::default_logger()->flush();
    }

    s_writing = false;
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* apInfo)
{
    Report(apInfo, "GAME CRASHED (unhandled exception)");
    return s_gameFilter ? s_gameFilter(apInfo) : EXCEPTION_CONTINUE_SEARCH;
}

// First-chance faults: logged when they come from the mod itself (the game may still handle them), plus the first few
// elsewhere. Logs only; the exception carries on to whatever handles it.
LONG WINAPI VectoredHandler(EXCEPTION_POINTERS* apInfo)
{
    const auto code = apInfo->ExceptionRecord->ExceptionCode;
    if (!IsFatal(code))
        return EXCEPTION_CONTINUE_SEARCH;

    const auto address = reinterpret_cast<uintptr_t>(apInfo->ExceptionRecord->ExceptionAddress);
    if (InOurModule(address))
        Report(apInfo, "fault inside NS-CyberMP (may still be caught)");
    else if (s_firstChanceLogged++ < 5)
        spdlog::warn("[Crash] first-chance {} 0x{:08X} at {} (may be handled by the game)", ExceptionName(code), code,
                     Describe(address));
    return EXCEPTION_CONTINUE_SEARCH;
}

// The game installs its own crash reporter after we load; keep ours in front and chain to theirs.
LPTOP_LEVEL_EXCEPTION_FILTER WINAPI HookedSetFilter(LPTOP_LEVEL_EXCEPTION_FILTER aFilter)
{
    const auto previous = s_gameFilter;
    if (aFilter != &UnhandledFilter)
        s_gameFilter = aFilter;
    return previous;
}

std::string FileVersion(const std::filesystem::path& aFile)
{
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(aFile.c_str(), &handle);
    if (!size)
        return "unknown";
    std::vector<uint8_t> data(size);
    if (!GetFileVersionInfoW(aFile.c_str(), 0, size, data.data()))
        return "unknown";
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info)
        return "unknown";
    return fmt::format("{}.{}.{}.{}", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                       HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
}

size_t CountFiles(const std::filesystem::path& aDir, const wchar_t* acExtension)
{
    std::error_code ec;
    size_t count = 0;
    for (auto it = std::filesystem::recursive_directory_iterator(aDir, ec); !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec))
    {
        if (it->is_regular_file(ec) && _wcsicmp(it->path().extension().c_str(), acExtension) == 0)
            ++count;
    }
    return count;
}
} // namespace

void App::CrashHandler::Install()
{
    AddVectoredExceptionHandler(0, &VectoredHandler);
    s_gameFilter = SetUnhandledExceptionFilter(&UnhandledFilter);

    const auto kernel = GetModuleHandleW(L"kernel32.dll");
    auto* target = kernel ? GetProcAddress(kernel, "SetUnhandledExceptionFilter") : nullptr;
    bool hooked = false;
    if (target && MH_CreateHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(&HookedSetFilter), reinterpret_cast<void**>(&s_realSetFilter)) == MH_OK)
        hooked = MH_EnableHook(reinterpret_cast<void*>(target)) == MH_OK;
    spdlog::info("[Crash] crash logging installed (dumps go to {}){}", (GCyberpunkMpLocation / "crashes").string(),
                 hooked ? "" : "; could not keep it in front of the game's crash reporter");
}

const std::vector<std::string>& App::CrashHandler::ContentMods()
{
    static std::vector<std::string> s_mods;
    static bool s_done = false;
    if (s_done)
        return s_mods;
    s_done = true;

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto game = std::filesystem::path(exe).parent_path().parent_path().parent_path();

    const auto collect = [&](const std::filesystem::path& aDir, std::initializer_list<const wchar_t*> aExtensions)
    {
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(aDir, ec);
             !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            if (!it->is_regular_file(ec))
                continue;
            for (const auto* extension : aExtensions)
            {
                if (_wcsicmp(it->path().extension().c_str(), extension) == 0)
                {
                    auto name = std::filesystem::relative(it->path(), game, ec).generic_string();
                    std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    s_mods.push_back(std::move(name));
                    break;
                }
            }
        }
    };
    collect(game / "archive" / "pc" / "mod", {L".archive", L".xl"});
    collect(game / "mods", {L".archive", L".xl"});
    collect(game / "r6" / "tweaks", {L".yaml", L".yml", L".tweak"});
    std::ranges::sort(s_mods);
    return s_mods;
}

void App::CrashHandler::LogEnvironment()
{
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path exePath(exe);
    const auto game = exePath.parent_path().parent_path().parent_path();

    spdlog::info("[Env] game {} version {}", exePath.string(), FileVersion(exePath));

    std::error_code ec;
    const auto plugins = game / "red4ext" / "plugins";
    std::string list;
    for (auto it = std::filesystem::directory_iterator(plugins, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
    {
        if (!it->is_directory(ec))
            continue;
        for (auto file = std::filesystem::directory_iterator(it->path(), ec);
             !ec && file != std::filesystem::directory_iterator(); file.increment(ec))
        {
            if (_wcsicmp(file->path().extension().c_str(), L".dll") == 0)
                list += fmt::format("{}{} {}", list.empty() ? "" : ", ", file->path().filename().string(),
                                    FileVersion(file->path()));
        }
    }
    ec.clear();
    spdlog::info("[Env] RED4ext plugins: {}", list.empty() ? "(none found)" : list);

    const auto cet = game / "bin" / "x64" / "plugins" / "cyber_engine_tweaks.asi";
    spdlog::info("[Env] Cyber Engine Tweaks: {}", std::filesystem::exists(cet, ec) ? "installed" : "not installed");
    for (const auto& mod : ContentMods())
        spdlog::debug("[Env]   content mod: {}", mod);
    spdlog::info("[Env] other mods: {} .archive in archive/pc/mod, {} .reds in r6/scripts, {} tweak files in r6/tweaks, "
                 "{} .archive in mods/",
                 CountFiles(game / "archive" / "pc" / "mod", L".archive"), CountFiles(game / "r6" / "scripts", L".reds"),
                 CountFiles(game / "r6" / "tweaks", L".yaml") + CountFiles(game / "r6" / "tweaks", L".tweak"),
                 CountFiles(game / "mods", L".archive"));

    const auto& s = Settings::Get();
    spdlog::info("[Env] settings: multiplayer {}, host {}, server {}:{}, public address '{}', wait for invite {}, "
                 "auto-connect {}, steam host {}, lobby {}, debug menu {}, debug mode {}",
                 s.enabled, s.host, s.ip.c_str(), s.port, s.publicAddress.c_str(), s.waitForInvite, s.autoConnect,
                 s.steamHost, s.lobby, s.debugMenu, s.debugMode);
}
