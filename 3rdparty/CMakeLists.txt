# Header-only third-party drops surfaced as INTERFACE targets.

# -- spdlog (header-only mode) -----------------------------------------------
add_library(bw_spdlog INTERFACE)
target_include_directories(bw_spdlog SYSTEM INTERFACE
    ${BW_3RDPARTY_DIR}/spdlog/include)
target_compile_definitions(bw_spdlog INTERFACE
    SPDLOG_HEADER_ONLY=1
    SPDLOG_NO_THREAD_ID=0)
add_library(bw::spdlog ALIAS bw_spdlog)
