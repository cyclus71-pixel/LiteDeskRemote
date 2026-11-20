# LiteDesk Remote — CMake build (FASE 1)
# Target:
#   ldcore       : inti lintas-platform (util, protokol, security murni) — juga dibangun di Linux utk test
#   ldwin        : lapisan Windows (CNG, Schannel, Winsock, UI Win32)          [Windows saja]
#   ldhost.exe   : aplikasi Host                                              [Windows saja]
#   ldviewer.exe : aplikasi Viewer                                            [Windows saja]
#   ldcore_tests : unit test inti (berjalan di OS mana pun)
#   ldwin_tests  : test integrasi Windows (CNG, TLS+auth localhost, discovery) [Windows saja]
cmake_minimum_required(VERSION 3.20)
project(LiteDeskRemote VERSION 0.1.0 LANGUAGES CXX)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW) # MSVC_RUNTIME_LIBRARY via property (static CRT)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_compile_options(
    $<$<CXX_COMPILER_ID:MSVC>:/W3>
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:GNU>:-Wall>
    $<$<CXX_COMPILER_ID:GNU>:-Wextra>
    $<$<CXX_COMPILER_ID:GNU>:-Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:Clang>:-Wall>
    $<$<CXX_COMPILER_ID:Clang>:-Wextra>
    $<$<CXX_COMPILER_ID:Clang>:-Wno-unused-parameter>
)

# ===== ldcore (lintas-platform) =====
add_library(ldcore STATIC
    common/util/log.cpp
    common/util/json.cpp
    common/util/config.cpp
    common/protocol/protocol.cpp
    common/protocol/messages.cpp
    common/security/security.cpp
    common/security/crypto_portable.cpp   # kosong di Windows (guard #if !defined(_WIN32))
)
target_include_directories(ldcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    enable_language(RC)

    # ===== ldwin (lapisan Windows) =====
    add_library(ldwin STATIC
        common/security/crypto_win.cpp
        common/security/cert_identity.cpp
        common/networking/net.cpp
        common/networking/tls.cpp
        common/networking/framed.cpp
        common/ui/theme.cpp
        common/ui/widgets.cpp
        common/ui/window.cpp
    )
    target_include_directories(ldwin PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(ldwin PUBLIC
        WIN32_LEAN_AND_MEAN NOMINMAX
        _WIN32_WINNT=0x0A00 WINVER=0x0A00
        SECURITY_WIN32
        UNICODE _UNICODE
    )
    target_link_libraries(ldwin PUBLIC ldcore
        ws2_32 secur32 bcrypt crypt32 advapi32
        user32 gdi32 shell32 iphlpapi dwmapi comctl32
    )

    # ===== Aplikasi =====
    add_executable(ldhost WIN32
        host/main.cpp
        host/session.cpp
        host/pages.cpp
        assets/host.rc
    )
    target_compile_definitions(ldhost PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
    target_link_libraries(ldhost PRIVATE ldwin)

    add_executable(ldviewer WIN32
        viewer/main.cpp
        viewer/session.cpp
        viewer/pages.cpp
        assets/viewer.rc
    )
    target_compile_definitions(ldviewer PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
    target_link_libraries(ldviewer PRIVATE ldwin)

    if(MSVC)
        target_link_options(ldhost PRIVATE /MANIFEST:NO)
        target_link_options(ldviewer PRIVATE /MANIFEST:NO)
    endif()

    add_executable(ldwin_tests
        tests/win/test_win.cpp
        host/session.cpp
        viewer/session.cpp
    )
    target_compile_definitions(ldwin_tests PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE)
    target_link_libraries(ldwin_tests PRIVATE ldwin)
endif()

# ===== Test inti (lintas-platform) =====
add_executable(ldcore_tests tests/core/test_core.cpp)
target_compile_definitions(ldcore_tests PRIVATE
    $<$<NOT:$<BOOL:${WIN32}>>:LDR_CORE_TEST>)
if(WIN32)
    target_link_libraries(ldcore_tests PRIVATE ldwin)
else()
    target_link_libraries(ldcore_tests PRIVATE ldcore)
endif()
if(NOT WIN32 AND NOT MSVC)
    find_package(Threads REQUIRED)
    target_link_libraries(ldcore_tests PRIVATE Threads::Threads)
endif()

# ===== Static CRT (exe portable tanpa dependensi runtime) =====
if(MSVC)
    set_property(TARGET ldcore ldwin ldhost ldviewer ldcore_tests ldwin_tests
                 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
