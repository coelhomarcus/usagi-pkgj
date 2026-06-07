#pragma once

#include "logbuffer.hpp"
#include <fmt/format.h>
#include <stdexcept>
#include <string>

// Compile-time: strip directory and extension from __FILE__, e.g. "downloader"
namespace pkgi_log_detail
{
constexpr const char* basename(const char* path)
{
    const char* last = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/' || *p == '\\')
            last = p + 1;
    return last;
}

// Returns length of stem (chars before the last '.')
constexpr int stem_len(const char* name)
{
    int len = 0, last_dot = -1;
    for (int i = 0; name[i]; ++i)
    {
        if (name[i] == '.')
            last_dot = i;
        ++len;
    }
    return last_dot > 0 ? last_dot : len;
}
} // namespace pkgi_log_detail

// We can't use a runtime-truncated string in a constexpr context easily,
// so we format the stem length at runtime — still zero .cpp allocation.
#define PKGI_LOG_MODULE                                           \
    ([]() -> std::string {                                        \
        const char* b = pkgi_log_detail::basename(__FILE__);      \
        return std::string(b, pkgi_log_detail::stem_len(b));      \
    }())

#define LOG(msg, ...)                                                                    \
    do                                                                                   \
    {                                                                                    \
        pkgi_log(LogLevel::Info, "[%s] " msg, PKGI_LOG_MODULE.c_str(), ##__VA_ARGS__);   \
    } while (0)

#define LOG_WARN(msg, ...)                                                               \
    do                                                                                   \
    {                                                                                    \
        pkgi_log(LogLevel::Warn, "[%s] " msg, PKGI_LOG_MODULE.c_str(), ##__VA_ARGS__);   \
    } while (0)

#define LOG_ERR(msg, ...)                                                                \
    do                                                                                   \
    {                                                                                    \
        pkgi_log(LogLevel::Error, "[%s] " msg, PKGI_LOG_MODULE.c_str(), ##__VA_ARGS__);  \
    } while (0)

// fmt-based variants
#define LOGF(msg, ...)                                                                   \
    do                                                                                   \
    {                                                                                    \
        pkgi_log(LogLevel::Info, "[%s] %s",                                              \
                PKGI_LOG_MODULE.c_str(),                                                 \
                fmt::format(msg, ##__VA_ARGS__).c_str());                                \
    } while (0)

#define LOGFW(msg, ...)                                                                  \
    do                                                                                   \
    {                                                                                    \
        pkgi_log(LogLevel::Warn, "[%s] %s",                                              \
                PKGI_LOG_MODULE.c_str(),                                                 \
                fmt::format(msg, ##__VA_ARGS__).c_str());                                \
    } while (0)

#define LOGFE(msg, ...)                                                                  \
    do                                                                                   \
    {                                                                                    \
        pkgi_log(LogLevel::Error, "[%s] %s",                                             \
                PKGI_LOG_MODULE.c_str(),                                                 \
                fmt::format(msg, ##__VA_ARGS__).c_str());                                \
    } while (0)

template <typename E = std::runtime_error, typename... Args>
[[nodiscard]] E formatEx(Args&&... args)
{
    return E(fmt::format(std::forward<Args>(args)...));
}

void pkgi_log(LogLevel level, const char* msg, ...);
