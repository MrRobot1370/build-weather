# Unit tests over the Qt-free libraries. Qt Test is only the harness: the code
# under test never touches a Qt type, which is why these run without a
# QGuiApplication.

set(BW_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")

function(bw_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})

    qt_add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBRARIES})
    target_compile_definitions(${name}
        PRIVATE BW_TEST_DATA_DIR="${BW_TEST_DATA_DIR}")
    target_compile_features(${name} PRIVATE cxx_std_20)
    set_property(TARGET ${name} PROPERTY FOLDER "tests")
    # Console application: the test output has to reach ctest.
    set_property(TARGET ${name} PROPERTY WIN32_EXECUTABLE FALSE)

    add_test(NAME ${name} COMMAND ${name})
    if(WIN32)
        # Qt6Core.dll lives in the Qt prefix, not next to the test binary.
        set_property(TEST ${name} PROPERTY ENVIRONMENT
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    endif()
endfunction()

bw_add_test(bw_path_tests
    SOURCES   path_utils_tests.cpp
    LIBRARIES BW::Core)

bw_add_test(bw_ninja_tests
    SOURCES   ninja_tests.cpp
    LIBRARIES BW::Build)

bw_add_test(bw_trace_tests
    SOURCES   trace_tests.cpp
    LIBRARIES BW::Build)

bw_add_test(bw_treemap_tests
    SOURCES   treemap_tests.cpp
    LIBRARIES BW::Treemap)
