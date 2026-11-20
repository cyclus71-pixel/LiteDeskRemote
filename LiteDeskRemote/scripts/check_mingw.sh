#!/bin/bash
# check_mingw.sh — link-check seluruh kode Windows (termasuk host/viewer/tests)
# memakai llvm-mingw cross toolchain di Linux. Bukan jalur build produksi
# (produksi = MSVC via scripts/build.ps1); ini QA sintaks/API/link ekstra.
set -e
export MINGW="${MINGW:-/tmp/llvm-mingw-20240917-ucrt-ubuntu-20.04-x86_64/bin}"
export PATH=$MINGW:$PATH
CXX="$MINGW/x86_64-w64-mingw32-g++"
WINDRES="$MINGW/x86_64-w64-mingw32-windres"
cd /home/z/my-project/download/LiteDeskRemote
mkdir -p /tmp/ldrcheck

FLAGS="-std=c++20 -Wall -Wextra -Wno-unused-parameter -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DSECURITY_WIN32 -DUNICODE -D_UNICODE -I."
LIBS="-lws2_32 -lsecur32 -lbcrypt -lcrypt32 -ladvapi32 -luser32 -lgdi32 -lshell32 -liphlpapi -ldwmapi -lcomctl32 -lole32"

echo "== ldcore =="
$CXX $FLAGS -c common/util/log.cpp        -o /tmp/ldrcheck/log.o
$CXX $FLAGS -c common/util/json.cpp       -o /tmp/ldrcheck/json.o
$CXX $FLAGS -c common/util/config.cpp     -o /tmp/ldrcheck/config.o
$CXX $FLAGS -c common/protocol/protocol.cpp  -o /tmp/ldrcheck/protocol.o
$CXX $FLAGS -c common/protocol/messages.cpp  -o /tmp/ldrcheck/messages.o
$CXX $FLAGS -c common/security/security.cpp  -o /tmp/ldrcheck/security.o
$CXX $FLAGS -c common/security/crypto_portable.cpp -o /tmp/ldrcheck/cport.o

echo "== ldwin =="
$CXX $FLAGS -c common/security/crypto_win.cpp      -o /tmp/ldrcheck/cwin.o
$CXX $FLAGS -c common/security/cert_identity.cpp   -o /tmp/ldrcheck/cident.o
$CXX $FLAGS -c common/networking/net.cpp           -o /tmp/ldrcheck/net.o
$CXX $FLAGS -c common/networking/tls.cpp           -o /tmp/ldrcheck/tls.o
$CXX $FLAGS -c common/networking/framed.cpp        -o /tmp/ldrcheck/framed.o
$CXX $FLAGS -c common/ui/theme.cpp                 -o /tmp/ldrcheck/theme.o
$CXX $FLAGS -c common/ui/widgets.cpp               -o /tmp/ldrcheck/widgets.o
$CXX $FLAGS -c common/ui/window.cpp                -o /tmp/ldrcheck/window.o

echo "== host & viewer =="
$CXX $FLAGS -c host/main.cpp           -o /tmp/ldrcheck/hmain.o
$CXX $FLAGS -c host/session.cpp        -o /tmp/ldrcheck/hsess.o
$CXX $FLAGS -c host/pages.cpp          -o /tmp/ldrcheck/hpages.o
$CXX $FLAGS -c viewer/main.cpp         -o /tmp/ldrcheck/vmain.o
$CXX $FLAGS -c viewer/session.cpp      -o /tmp/ldrcheck/vsess.o
$CXX $FLAGS -c viewer/pages.cpp        -o /tmp/ldrcheck/vpages.o

echo "== resources =="
$WINDRES assets/host.rc   -O coff -o /tmp/ldrcheck/host.res
$WINDRES assets/viewer.rc -O coff -o /tmp/ldrcheck/viewer.res

echo "== tests =="
$CXX $FLAGS -c tests/core/test_core.cpp -o /tmp/ldrcheck/tcore.o
$CXX $FLAGS -c tests/win/test_win.cpp   -o /tmp/ldrcheck/twin.o

echo "== link =="
CORE="/tmp/ldrcheck/log.o /tmp/ldrcheck/json.o /tmp/ldrcheck/config.o /tmp/ldrcheck/protocol.o /tmp/ldrcheck/messages.o /tmp/ldrcheck/security.o"
$CXX $FLAGS -municode -o /tmp/ldrcheck/ldhost.exe   /tmp/ldrcheck/hmain.o /tmp/ldrcheck/hsess.o /tmp/ldrcheck/hpages.o $CORE /tmp/ldrcheck/cwin.o /tmp/ldrcheck/cident.o /tmp/ldrcheck/net.o /tmp/ldrcheck/tls.o /tmp/ldrcheck/framed.o /tmp/ldrcheck/theme.o /tmp/ldrcheck/widgets.o /tmp/ldrcheck/window.o /tmp/ldrcheck/host.res $LIBS
$CXX $FLAGS -municode -o /tmp/ldrcheck/ldviewer.exe /tmp/ldrcheck/vmain.o /tmp/ldrcheck/vsess.o /tmp/ldrcheck/vpages.o $CORE /tmp/ldrcheck/cwin.o /tmp/ldrcheck/cident.o /tmp/ldrcheck/net.o /tmp/ldrcheck/tls.o /tmp/ldrcheck/framed.o /tmp/ldrcheck/theme.o /tmp/ldrcheck/widgets.o /tmp/ldrcheck/window.o /tmp/ldrcheck/viewer.res $LIBS
$CXX $FLAGS -o /tmp/ldrcheck/ldcore_tests.exe /tmp/ldrcheck/tcore.o $CORE /tmp/ldrcheck/cport.o /tmp/ldrcheck/cwin.o -static -lbcrypt
$CXX $FLAGS -o /tmp/ldrcheck/ldwin_tests.exe /tmp/ldrcheck/twin.o /tmp/ldrcheck/hsess.o /tmp/ldrcheck/vsess.o $CORE /tmp/ldrcheck/cwin.o /tmp/ldrcheck/cident.o /tmp/ldrcheck/net.o /tmp/ldrcheck/tls.o /tmp/ldrcheck/framed.o $LIBS

echo "== HASIL =="
ls -la /tmp/ldrcheck/*.exe
echo "LINK-CHECK MINGW: SEMUA LOLOS"
