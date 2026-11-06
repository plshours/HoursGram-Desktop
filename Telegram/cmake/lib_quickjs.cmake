# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# QuickJS-ng, the JavaScript engine HorusGram plugins run on. It is C, so it
# gets its own flags here instead of the C++ ones of init_target.

add_library(lib_quickjs STATIC)
add_library(desktop-app::lib_quickjs ALIAS lib_quickjs)
init_target_folder(lib_quickjs "(external)")

set(quickjs_loc ${third_party_loc}/quickjs)

target_sources(lib_quickjs
PRIVATE
    ${quickjs_loc}/cutils.c
    ${quickjs_loc}/libregexp.c
    ${quickjs_loc}/libunicode.c
    ${quickjs_loc}/quickjs.c
    ${quickjs_loc}/xsum.c
)

target_include_directories(lib_quickjs
PUBLIC
    ${quickjs_loc}
)

set_target_properties(lib_quickjs PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

if (MSVC)
    target_compile_definitions(lib_quickjs
    PRIVATE
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0601
        _CRT_SECURE_NO_WARNINGS
    )
    target_compile_options(lib_quickjs
    PRIVATE
        /experimental:c11atomics
        /utf-8
        /W0
    )
else()
    target_compile_definitions(lib_quickjs
    PRIVATE
        _GNU_SOURCE
    )
    target_compile_options(lib_quickjs
    PRIVATE
        -funsigned-char
        -w
    )
    find_package(Threads REQUIRED)
    target_link_libraries(lib_quickjs
    PRIVATE
        Threads::Threads
        m
    )
endif()
