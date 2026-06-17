#pragma once

#include <functional>
#include <string>

namespace pmcp
{
    enum class LogLevel
    {
        Debug,
        Info,
        Warn,
        Error,
    };

    // Severity-tagged log callback. Implementations route to PE_INFO/PE_WARN/PE_ERROR or any host logger.
    using LogCallback = std::function<void(LogLevel level, const std::string &message)>;

    inline const char *LogLevelName(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warn:
            return "warn";
        case LogLevel::Error:
            return "error";
        }
        return "info";
    }
} // namespace pmcp
