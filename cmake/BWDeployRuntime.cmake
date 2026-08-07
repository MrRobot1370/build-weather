# BWDeployRuntime.cmake
#
# bw_deploy_runtime(<exe_target>) - runs windeployqt next to the executable on
# Windows so the app launches from $<TARGET_FILE_DIR:exe> without PATH munging.

function(bw_deploy_runtime tgt)
    if(NOT WIN32)
        return()
    endif()

    if(TARGET Qt6::windeployqt)
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND Qt6::windeployqt
                    --no-translations --no-system-d3d-compiler --no-opengl-sw
                    --qmldir "${CMAKE_SOURCE_DIR}/apps/BuildWeather/qml"
                    "$<TARGET_FILE:${tgt}>"
            VERBATIM)
    else()
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt
            HINTS "${Qt6_DIR}/../../../bin")
        if(WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET ${tgt} POST_BUILD
                COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                        --no-translations --no-system-d3d-compiler --no-opengl-sw
                        --qmldir "${CMAKE_SOURCE_DIR}/apps/BuildWeather/qml"
                        "$<TARGET_FILE:${tgt}>"
                VERBATIM)
        endif()
    endif()
endfunction()
