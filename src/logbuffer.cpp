#include "logbuffer.hpp"

#include <ctime>
#include <deque>
#include <mutex>

namespace
{
std::mutex g_log_mutex;
std::deque<LogEntry> g_log_lines;

constexpr std::size_t MaxLogLines = 512;
}

void pkgi_log_buffer_append(LogLevel level, const char* line)
{
    if (!line)
        return;

    std::string text(line);
    while (!text.empty() &&
           (text.back() == '\n' || text.back() == '\r'))
    {
        text.pop_back();
    }

    std::lock_guard<std::mutex> lock(g_log_mutex);

    // Prepend HH:MM:SS [LEVL] timestamp from system clock. std::localtime()
    // returns a pointer to static storage, so keep it under the same lock as
    // the log buffer.
    char ts[10];
    std::time_t now = std::time(nullptr);
    std::tm* tm_now = std::localtime(&now);
    std::strftime(ts, sizeof(ts), "%H:%M:%S", tm_now);
    const char* lvl = level == LogLevel::Error ? "ERR "
                    : level == LogLevel::Warn  ? "WARN"
                    :                            "INFO";
    text = std::string(ts) + " [" + lvl + "] " + text;

    g_log_lines.push_back({level, std::move(text)});
    while (g_log_lines.size() > MaxLogLines)
        g_log_lines.pop_front();
}

std::vector<LogEntry> pkgi_log_buffer_snapshot()
{
    std::lock_guard<std::mutex> lock(g_log_mutex);
    return {g_log_lines.begin(), g_log_lines.end()};
}
