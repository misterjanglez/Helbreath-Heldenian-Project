# Sentry crash reporting for the Linux CMake builds. Windows links the
# prebuilt DLL from Sources/Dependencies/sentry-native instead (see its
# README). Requires libcurl-dev + zlib1g-dev on the build host; disable with
# -DHB_ENABLE_SENTRY=OFF if the SDK cannot be built.

set(HB_SENTRY_VERSION 0.15.4)
option(HB_ENABLE_SENTRY "Enable Sentry crash reporting" ON)

function(hb_enable_sentry target)
    if(NOT HB_ENABLE_SENTRY)
        return()
    endif()
    include(FetchContent)
    # Static lib keeps deployment a single binary (plus crashpad_handler).
    set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SENTRY_BACKEND crashpad CACHE STRING "" FORCE)
    set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SENTRY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        sentry
        URL https://github.com/getsentry/sentry-native/releases/download/${HB_SENTRY_VERSION}/sentry-native.zip
    )
    FetchContent_MakeAvailable(sentry)
    target_link_libraries(${target} PRIVATE sentry::sentry)
    target_compile_definitions(${target} PRIVATE HB_SENTRY)
    # crashpad_handler must sit next to the executable at runtime
    if(TARGET crashpad_handler)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:crashpad_handler> $<TARGET_FILE_DIR:${target}>)
    endif()
endfunction()
