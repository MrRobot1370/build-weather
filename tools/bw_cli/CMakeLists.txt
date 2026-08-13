# bw_cli - headless driver over the same libraries the GUI uses.
#
# It exists for two reasons: it is the post-build sanity check (parse a real
# .ninja_log and print the analysis without starting a window), and it is the
# thing you run in CI to diff two builds.

add_executable(bw_cli main.cpp)
target_link_libraries(bw_cli PRIVATE BW::Core BW::Build BW::Treemap)
target_compile_features(bw_cli PRIVATE cxx_std_20)
target_compile_definitions(bw_cli PRIVATE BW_VERSION="${PROJECT_VERSION}")
set_property(TARGET bw_cli PROPERTY FOLDER "tools")

# Links no Qt, so it needs nothing from the deployment but the C runtime.
install(TARGETS bw_cli RUNTIME DESTINATION ".")
