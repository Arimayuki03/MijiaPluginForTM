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
