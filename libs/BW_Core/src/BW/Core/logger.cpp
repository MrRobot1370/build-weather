#include "BW/Core/logger.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <cstdlib>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace BW::Core
{

namespace {

std::mutex g_mutex;
std::vector<spdlog::sink_ptr> g_sinks;
std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> g_loggers;
spdlog::level::level_enum g_level = spdlog::level::info;
bool g_initialized = false;

auto logDirectory(const std::string &appName) -> std::string
{
#ifdef _WIN32
    const char *base = std::getenv("LOCALAPPDATA");
#else
    const char *base = std::getenv("HOME");
#endif
    if (base == nullptr) {
        return "logs/" + appName + ".log";
    }
    return std::string { base } + "/" + appName + "/logs/" + appName + ".log";
}

}

void initLogger(const std::string &appName, spdlog::level::level_enum level)
{
    const std::lock_guard<std::mutex> lock { g_mutex };
    if (g_initialized) {
        return;
    }

    g_level = level;
    g_sinks.push_back(
        std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    try {
        constexpr std::size_t kMaxSize = 4u * 1024u * 1024u;
        constexpr std::size_t kMaxFiles = 3;
        g_sinks.push_back(
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                logDirectory(appName),
                kMaxSize,
                kMaxFiles));
    }
    catch (const spdlog::spdlog_ex &) {
        // A read-only or missing profile directory must not stop the app;
        // the console sink is enough.
    }

    g_initialized = true;
}

auto log(const std::string &channel) -> std::shared_ptr<spdlog::logger>
{
    const std::lock_guard<std::mutex> lock { g_mutex };

    if (const auto it = g_loggers.find(channel); it != g_loggers.end()) {
        return it->second;
    }

    if (g_sinks.empty()) {
        g_sinks.push_back(
            std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    auto logger = std::make_shared<spdlog::logger>(
        channel,
        g_sinks.begin(),
        g_sinks.end());
    logger->set_level(g_level);
    logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
    g_loggers.emplace(channel, logger);
    return logger;
}

}
