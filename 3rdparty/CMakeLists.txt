# Header-only third-party drops surfaced as INTERFACE targets.

# -- spdlog (header-only mode) -----------------------------------------------
add_library(bw_spdlog INTERFACE)
# BW::Core exposes spdlog through its own public headers, so this has to
# resolve in the build tree and against the installed sdk/include alike.
target_include_directories(bw_spdlog SYSTEM INTERFACE
    $<BUILD_INTERFACE:${BW_3RDPARTY_DIR}/spdlog/include>
    $<INSTALL_INTERFACE:sdk/include>)
target_compile_definitions(bw_spdlog INTERFACE
    SPDLOG_HEADER_ONLY=1
    SPDLOG_NO_THREAD_ID=0)
add_library(bw::spdlog ALIAS bw_spdlog)

set_property(TARGET bw_spdlog PROPERTY EXPORT_NAME spdlog)
install(TARGETS bw_spdlog EXPORT BuildWeatherTargets)
