#!/bin/bash
# 用 Morok (LLVM IR 混淆器) 混淆编译本目录的 C++ 源码，产出到 bin/ 目录。
#
# 依赖（一次性）：
#   brew install llvm cmake
#   git clone https://github.com/19h/morok && cd morok && \
#     cmake -S . -B build -G Ninja -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm \
#       -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
#       -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
#       -DMOROK_BUILD_TESTS=OFF && cmake --build build --target morok_plugin
#   然后 MOROK_PLUGIN 指向 build/src/pipeline/libMorok.dylib（或用下面的默认路径）。
#
# 用法：./obfuscate.sh [preset]    # preset: low|mid|high|max，默认 mid
#   MOROK_PLUGIN=/path/to/libMorok.dylib MOROK_SEED=1234 ./obfuscate.sh mid
#
# 注意：high/max 在 macOS arm64 上会因运行时自解密/自封装（W^X 禁止自修改代码）
#       及 Linux/Windows 专用反分析 pass 而挂起，本机可用档位到 mid 为止。
set -euo pipefail
cd "$(dirname "$0")"

PRESET="${1:-mid}"
LLVM=/opt/homebrew/opt/llvm
PLUGIN="${MOROK_PLUGIN:-/tmp/morok/build/src/pipeline/libMorok.dylib}"
SSL_INC=/opt/homebrew/opt/openssl@3/include
SSL_LIB=/opt/homebrew/opt/openssl@3/lib

[ -x "$LLVM/bin/clang++" ] || { echo "缺少 LLVM：brew install llvm" >&2; exit 1; }
[ -f "$PLUGIN" ]         || { echo "缺少 morok 插件：$PLUGIN" >&2; exit 1; }

mkdir -p bin
for src in *.cpp; do
    [ -e "$src" ] || continue
    out="bin/${src%.cpp}-darwin-$(uname -m)-obf"
    echo "obfuscate $src -> $out (preset=$PRESET)"
    "$LLVM/bin/clang++" -std=c++17 -O2 -I"$SSL_INC" \
        -fpass-plugin="$PLUGIN" \
        -mllvm -morok -mllvm "-morok-preset=$PRESET" \
        ${MOROK_SEED:+-mllvm "-morok-seed=$MOROK_SEED"} \
        "$src" -o "$out" -L"$SSL_LIB" -lcrypto
done
echo "done -> ls bin/"
ls -la bin/
