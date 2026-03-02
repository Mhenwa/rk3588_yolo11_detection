#include "core/log/app_log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace {
constexpr const char* kLogDir = "logs";
constexpr size_t kLogMaxSize = 5 * 1024 * 1024;
constexpr size_t kLogMaxFiles = 3;

void ensure_log_dir()
{
    struct stat st;
    if (stat(kLogDir, &st) == 0 && S_ISDIR(st.st_mode)) {
        return;
    }
    mkdir(kLogDir, 0755);
}

std::string vformat(const char* fmt, va_list args)
{
    if (!fmt) return std::string();
    va_list copy;
    va_copy(copy, args);
    int len = vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (len <= 0) {
        return std::string(fmt);
    }
    std::vector<char> buf(static_cast<size_t>(len) + 1, '\0');
    vsnprintf(buf.data(), buf.size(), fmt, args);
    std::string msg(buf.data());
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
        msg.pop_back();
    }
    return msg;
}

std::string make_log_path()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    char stamp[32];
    strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm_buf);
    std::string path = kLogDir;
    path += "/dock_blindspot_";
    path += stamp;
    path += ".log";
    return path;
}

void log_v(spdlog::level::level_enum level, const char* fmt, va_list args)
{
    std::string msg = vformat(fmt, args);
    auto logger = spdlog::default_logger();
    if (logger) {
        logger->log(level, "{}", msg);
        return;
    }
    if (!msg.empty()) {
        fprintf(stderr, "%s\n", msg.c_str());
    }
}
}  // namespace

extern "C" void init_logging()
{
    ensure_log_dir();
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    std::string log_path = make_log_path();
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        log_path, kLogMaxSize, kLogMaxFiles);
    console_sink->set_level(spdlog::level::info);
    console_sink->set_pattern("%v");
    file_sink->set_level(spdlog::level::debug);
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [t%t] %v");

    std::vector<spdlog::sink_ptr> sinks = {console_sink, file_sink};
    auto logger = std::make_shared<spdlog::logger>(
        "dock_blindspot", sinks.begin(), sinks.end());
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_on(spdlog::level::warn);
    spdlog::flush_every(std::chrono::seconds(2));
}

extern "C" void shutdown_logging()
{
    spdlog::shutdown();
}

extern "C" void log_info(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_v(spdlog::level::info, fmt, args);
    va_end(args);
}

extern "C" void log_warn(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_v(spdlog::level::warn, fmt, args);
    va_end(args);
}

extern "C" void log_error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_v(spdlog::level::err, fmt, args);
    va_end(args);
}

extern "C" void log_debug(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_v(spdlog::level::debug, fmt, args);
    va_end(args);
}
