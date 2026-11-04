# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# QuickJS-ng: the JavaScript engine that runs HorusGram plugins
# (SourceFiles/hoursgram/plugins). Only the engine itself is built, without
# the libc bindings, so a plugin gets no files, processes or network from it.

add_library(lib_quickjs STATIC)
init_target(lib_quickjs "(external)")

add_library(desktop-app::lib_quickjs ALIAS lib_quickjs)

set(quickjs_loc ${third_party_loc}/quickjs-ng)

set_target_properties(lib_quickjs PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

# Silence third-party warnings. On MSVC the flags must ride an INTERFACE lib
# linked after common_options to land last and beat its /W4 /WX. The engine
# uses C11 atomics, which MSVC keeps behind a flag.
if (MSVC)
    add_library(lib_quickjs_warnings_off INTERFACE)
    target_compile_options(lib_quickjs_warnings_off
    INTERFACE
        /W0
        /WX-
        /experimental:c11atomics)
endif()

nice_target_sources(lib_quickjs ${quickjs_loc}
PRIVATE
    builtin-array-fromasync.h
    builtin-iterator-zip-keyed.h
    builtin-iterator-zip.h
    cutils.h
    dtoa.c
    dtoa.h
    libregexp-opcode.h
    libregexp.c
    libregexp.h
    libunicode-table.h
    libunicode.c
    libunicode.h
    list.h
    quickjs-atom.h
    quickjs-c-atomics.h
    quickjs-opcode.h
    quickjs.c
    quickjs.h
)

target_include_directories(lib_quickjs
PUBLIC
    ${quickjs_loc}
)

target_compile_definitions(lib_quickjs PRIVATE _GNU_SOURCE)
if (WIN32)
    target_compile_definitions(lib_quickjs
    PRIVATE
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0601)
endif()

if (LINUX)
    target_link_libraries(lib_quickjs PUBLIC m)
endif()

if (MSVC)
    target_link_libraries(lib_quickjs PRIVATE lib_quickjs_warnings_off)
else()
    target_compile_options(lib_quickjs PRIVATE -w -funsigned-char)
endif()
