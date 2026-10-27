#include "SpdlogProvider.hpp"
#include "Core/Facades/Runtime.hpp"
#include "Core/Stl.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

void Support::SpdlogProvider::OnInitialize()
{
    if (m_logPath.empty())
    {
        m_logPath = Core::Runtime::GetModulePath().replace_extension(L".log");
    }

    // Keep the previous session's log (CyberpunkMP.prev.log): a crash or a quick relaunch used to wipe it before
    // anyone could send it.
    std::error_code ec;
    if (std::filesystem::exists(m_logPath, ec) && std::filesystem::file_size(m_logPath, ec) > 0)
    {
        auto previous = m_logPath;
        previous.replace_extension(L".prev.log");
        std::filesystem::remove(previous, ec);
        std::filesystem::rename(m_logPath, previous, ec);
    }

    // Size-capped: 20 MB per file, two older parts kept (CyberpunkMP.1.log, CyberpunkMP.2.log).
    auto sink = Core::MakeShared<spdlog::sinks::rotating_file_sink_mt>(m_logPath.wstring(), 20 * 1024 * 1024, 2, false);
    auto logger = Core::MakeShared<spdlog::logger>("", spdlog::sinks_init_list{sink});
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [t%t] %v");
    logger->flush_on(spdlog::level::trace);

    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::trace);

    SetDefault(*this);
}

void Support::SpdlogProvider::LogInfo(const std::string_view& aMessage)
{
    spdlog::default_logger_raw()->info(aMessage);
}

void Support::SpdlogProvider::LogWarning(const std::string_view& aMessage)
{
    spdlog::default_logger_raw()->warn(aMessage);
}

void Support::SpdlogProvider::LogError(const std::string_view& aMessage)
{
    spdlog::default_logger_raw()->error(aMessage);
}

void Support::SpdlogProvider::LogDebug(const std::string_view& aMessage)
{
    spdlog::default_logger_raw()->debug(aMessage);
}

void Support::SpdlogProvider::LogFlush()
{
    spdlog::default_logger_raw()->flush();
}
