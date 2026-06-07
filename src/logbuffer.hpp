#pragma once

#include <string>
#include <vector>

enum class LogLevel
{
    Info,
    Warn,
    Error,
};

struct LogEntry
{
    LogLevel    level;
    std::string text; // already includes timestamp + level tag
};

void                    pkgi_log_buffer_append(LogLevel level, const char* line);
std::vector<LogEntry>   pkgi_log_buffer_snapshot();