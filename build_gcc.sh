#!/bin/bash
# MinGW(GCC) 构建脚本 - 生成静态链接的 MijiaPower.dll（x64 Release）
# 用法: ./build_gcc.sh
set -e
cd "$(dirname "$0")"

GXX="$(command -v g++ 2>/dev/null || true)"
if [ -z "$GXX" ]; then
    # PATH 中没有 g++ 时，在 WinGet 安装目录中查找（WinLibs 工具链）
    GXX=$(find "/c/Users/${USERNAME}/AppData/Local/Microsoft/WinGet/Packages" -maxdepth 4 -name "g++.exe" 2>/dev/null | head -1)
fi
[ -z "$GXX" ] && { echo "未找到 g++.exe"; exit 1; }

OUT="bin/Release/x64"
mkdir -p "$OUT"

echo "编译: $("$GXX" --version | head -1)"
"$GXX" -std=c++17 -O2 -shared \
    -D_UNICODE -DUNICODE -D_WIN32_WINNT=0x0A00 -DNDEBUG \
    -Wall -Wno-unknown-pragmas \
    -static -static-libgcc -static-libstdc++ \
    -o "$OUT/MijiaPower.dll" \
    MiioDevice.cpp MijiaPowerPlugin.cpp OptionsDlg.cpp pch.cpp PluginConfig.cpp PowerHistory.cpp \
    -lws2_32 -lcomctl32 -lgdi32 -luser32 -lole32

# strip 减小体积（MinGW 工具链无法处理中文路径，需经 ASCII 临时目录）
TMPSTRIP=$(mktemp -d)
cp "$OUT/MijiaPower.dll" "$TMPSTRIP/"
"$(dirname "$GXX")/strip.exe" -s "$TMPSTRIP/MijiaPower.dll" && cp "$TMPSTRIP/MijiaPower.dll" "$OUT/MijiaPower.dll"
rm -rf "$TMPSTRIP"

echo "构建完成: $OUT/MijiaPower.dll ($(stat -c%s "$OUT/MijiaPower.dll") 字节)"

# ── 测试宿主构建（与 DLL 编译选项一致；exe 不做 strip）──
# 静态链接运行库：与 DLL 同样无运行时依赖，任何 PATH 下直接可跑
# （否则会按 PATH 顺序加载到不兼容的 libstdc++-6.dll，如 Git Bash 自带的 MSVCRT 版）
TESTFLAGS="-std=c++17 -O2 -D_UNICODE -DUNICODE -D_WIN32_WINNT=0x0A00 -DNDEBUG -Wall -Wno-unknown-pragmas -static -static-libgcc -static-libstdc++"

# test_unit.exe：离线单元测试。test_unit.cpp 内部 #include 了 MiioDevice.cpp 与
# MijiaPowerPlugin.cpp（为访问 static HexToBytes 与 FormatWatts），
# 因此这两个 .cpp 绝不能再单独编译进本命令，否则重复定义。
echo "构建: test_unit.exe"
"$GXX" $TESTFLAGS -o "$OUT/test_unit.exe" \
    test_unit.cpp PluginConfig.cpp PowerHistory.cpp OptionsDlg.cpp pch.cpp \
    -lws2_32 -lcomctl32 -lgdi32 -luser32 -lole32

echo "构建: test_harness.exe"
"$GXX" $TESTFLAGS -o "$OUT/test_harness.exe" test_harness.cpp \
    -lshell32 -luser32

echo "构建: test_dialog.exe"
"$GXX" $TESTFLAGS -o "$OUT/test_dialog.exe" test_dialog.cpp \
    -lshell32 -luser32

echo "构建: test_dpi.exe"
# test_dpi.cpp：CommandLineToArgvW→shell32；SetWindowPos/FindWindowW/GetMonitorInfoW→user32；
# CreateCompatibleDC/CreateCompatibleBitmap/GetDIBits/SelectObject→gdi32（Shcore 动态加载）
"$GXX" $TESTFLAGS -o "$OUT/test_dpi.exe" test_dpi.cpp \
    -lshell32 -luser32 -lgdi32

echo "构建完成: $OUT/MijiaPower.dll + test_unit.exe + test_harness.exe + test_dialog.exe + test_dpi.exe"
