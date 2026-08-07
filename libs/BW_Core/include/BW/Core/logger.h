#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace BW::Core
{

/// Installs the global spdlog sinks (coloured stdout plus a rotating file in
/// %LOCALAPPDATA%/BuildWeather/logs/). Idempotent.
void initLogger(
    const std::string &appName = "BuildWeather",
    spdlog::level::level_enum level = spdlog::level::info);

/// Channel-named logger: BW::Core::log("ninja")->info(...). Cheap, cached.
[[nodiscard]]
auto log(const std::string &channel = "bw")
    -> std::shared_ptr<spdlog::logger>;

}
