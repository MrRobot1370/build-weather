# BWDeployRuntime.cmake
#
# bw_deploy_runtime(<exe_target>)  - windeployqt next to the executable, so it
#                                    runs from the build tree without PATH
#                                    munging.
# bw_install_runtime(<exe_target>) - the same for the install tree.

# Qt 6.6's qt_generate_deploy_qml_app_script writes its include() and
# EXECUTABLE lines unquoted, so a source path containing a space breaks the
# install step.
function(bw_find_windeployqt out_var)
    find_program(BW_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
              "${QT6_INSTALL_PREFIX}/bin"
        DOC   "Qt deployment tool used to make the build and install trees runnable")
    set(${out_var} "${BW_WINDEPLOYQT_EXECUTABLE}" PARENT_SCOPE)
endfunction()

# --no-compiler-runtime: windeployqt drops vc_redist.x64.exe, an installer the
# user would have to run. InstallRequiredSystemLibraries ships the DLLs.
set(BW_WINDEPLOYQT_ARGS
    --no-translations
    --no-system-d3d-compiler
    --no-opengl-sw
    --no-compiler-runtime)

function(bw_deploy_runtime tgt)
    if(NOT WIN32)
        return()
    endif()

    if(TARGET Qt6::windeployqt)
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND Qt6::windeployqt ${BW_WINDEPLOYQT_ARGS}
                    --qmldir "${CMAKE_SOURCE_DIR}/apps/BuildWeather/qml"
                    "$<TARGET_FILE:${tgt}>"
            VERBATIM)
        return()
    endif()

    bw_find_windeployqt(windeployqt)
    if(windeployqt)
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND "${windeployqt}" ${BW_WINDEPLOYQT_ARGS}
                    --qmldir "${CMAKE_SOURCE_DIR}/apps/BuildWeather/qml"
                    "$<TARGET_FILE:${tgt}>"
            VERBATIM)
    endif()
endfunction()

function(bw_install_runtime tgt)
    if(NOT WIN32)
        return()
    endif()

    bw_find_windeployqt(windeployqt)
    if(NOT windeployqt)
        message(WARNING
            "windeployqt not found: the installed tree will not contain the Qt "
            "runtime and the package will not be self-contained.")
        return()
    endif()

    # Explicit: windeployqt otherwise infers the flavour from the binary.
    set(config_flag "$<IF:$<CONFIG:Debug>,--debug,--release>")
    set(qml_dir "${CMAKE_SOURCE_DIR}/apps/BuildWeather/qml")

    install(CODE "
        set(exe \"\${CMAKE_INSTALL_PREFIX}/$<TARGET_FILE_NAME:${tgt}>\")
        message(STATUS \"windeployqt: \${exe}\")
        execute_process(
            COMMAND \"${windeployqt}\" ${config_flag} ${BW_WINDEPLOYQT_ARGS}
                    --qmldir \"${qml_dir}\"
                    \"\${exe}\"
            RESULT_VARIABLE deploy_result)
        if(NOT deploy_result EQUAL 0)
            message(FATAL_ERROR
                \"windeployqt failed (\${deploy_result}); the package would not \"
                \"be self-contained\")
        endif()
    ")
endfunction()
