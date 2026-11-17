---
name: heart-stego
description: 心形图片隐写加解密。把进程完整环境变量快照 AES-256-GCM 加密后藏进心形 BMP 像素最低位（LSB），支持 Morok 混淆构建。触发词"心形隐写"、"heart stego"、"图片加密"、"图片藏数据"、"环境快照"、"BMP 隐写"、"LSB 隐写"、"隐写加解密"。
---

# 心形图片隐写 (heart-stego)

把当前进程的完整环境变量快照加密后，藏进一张 512×512 心形 BMP 图片的像素最低位（LSB 隐写）。
全程无网络访问。源码在 `scripts/` 下：

- `generate_heart.cpp` — 编码：采集环境变量 → 加密 → 隐写入图
- `decode_heart.cpp` — 解码：提取 → 校验 → 解密 → 打印
- `obfuscate.sh` — 用 Morok（LLVM IR 混淆器）混淆编译，抬高逆向成本

## 原理（一句话）

环境变量 → PBKDF2-HMAC-SHA256 派生 256-bit 密钥 → AES-256-GCM 认证加密 →
加帧头尾（魔数 `HRTSAFE1` + 长度 + CRC32）→ 逐 bit 写入像素 BGR 通道 LSB → 出图。
解码反向走，并做 GCM 认证。

- **保密靠 AES，不靠隐写**：LSB 只是藏起来，解密靠密码；错密码/篡改会认证失败。
- **默认密码 `heart-default-password` 写在源码里**，部署前务必改掉。
- 隐写容量约 **98 KB**（512×512×3 bit，扣帧头尾 16 字节 + 加密块头尾 44 字节）。

## 构建

### macOS 混淆版（Morok）

```sh
./scripts/obfuscate.sh          # 默认 mid 档 → bin/*-darwin-arm64-obf
./scripts/obfuscate.sh mid      # 显式档位 low|mid|high|max
```

> 依赖 Homebrew LLVM(≥18) + 已编译的 morok 插件（默认 `/tmp/morok/build/src/pipeline/libMorok.dylib`，
> 可用 `MOROK_PLUGIN` 覆盖）。`high`/`max` 在 macOS arm64 会挂起（W^X 禁止自修改代码），可用到 `mid`。

### macOS 普通版（不混淆）

```sh
g++ -std=c++17 -O2 -I/opt/homebrew/opt/openssl@3/include \
    scripts/generate_heart.cpp -o bin/generate_heart-darwin-arm64 \
    -L/opt/homebrew/opt/openssl@3/lib -lcrypto
# decode_heart.cpp 同理
```

### Linux x86_64（静态，任意 Linux 服务器可跑）

在 Linux x86_64 服务器上（需 `build-essential libssl-dev zlib1g-dev`）：

```sh
g++ -std=c++17 -O2 -static scripts/generate_heart.cpp -o generate_heart-linux-amd64 -lcrypto -lz
g++ -std=c++17 -O2 -static scripts/decode_heart.cpp  -o decode_heart-linux-amd64  -lcrypto -lz
```

## 使用

```sh
generate_heart heart.bmp "密码"     # 生成（不传密码用默认）
decode_heart  heart.bmp "密码"     # 解码打印环境快照
```

## 关键常量（源码内）

| 项 | 值 |
|----|----|
| 图片 | 512×512, 24-bit, 无压缩 BMP |
| 密钥派生 | PBKDF2-HMAC-SHA256, 盐 16B, 10 万次迭代 |
| 加密 | AES-256-GCM, nonce 12B, tag 16B |
| 帧 | 魔数 `HRTSAFE1`(8B) + 长度(4B LE) + 密文块 + CRC32(4B LE) |
| 容量 | 明文上限约 98 KB |
