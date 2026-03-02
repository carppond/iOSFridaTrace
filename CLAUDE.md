# CLAUDE.md - FridaTrace Project Guide

## Project Overview

FridaTrace 是一个基于 frida-gum Stalker 的 iOS ARM64 指令级追踪库。通过内联 ARM64 代码生成实现低开销追踪，使用无锁环形缓冲区记录执行轨迹。

**仓库**: https://github.com/carppond/iOSFridaTrace.git
**许可**: MIT License
**frida-gum 固定版本**: `7804914181117f05d20d3b143f7c13935658777f`

## Architecture

```
目标代码 → Stalker 动态重编译 → 内联 ARM64 micro-prolog
    → GumTraceRecorder (无锁环形缓冲区)
    → 后台刷写线程 (100ms)
    → 二进制 trace 文件 (FRIT 格式)
    → trace_parser 解析工具
```

**核心设计**:
- **Strategy B (默认)**: 内联 ARM64 代码生成，micro-prolog ~15 条指令，比 PROLOG_FULL (~50+) 更快
- **Strategy A (备选)**: 基于 transformer callback + `put_callout()` 的慢速方案
- **无锁设计**: 单生产者 (Stalker 线程) / 单消费者 (flush 线程)
- **32 字节定长记录**: type(1) + reserved(3) + thread_id(4) + location(8) + target(8) + timestamp(8)

## Directory Structure

```
FridaTrace/
├── src/trace/                    # 核心追踪模块源码
│   ├── gumtrace.h/c             # 顶层 API (start/stop/flush)
│   ├── gumtracerecorder.h/c     # 环形缓冲区 + 录制器
│   └── gumtraceinline-arm64.h/c # ARM64 内联代码生成
├── patches/
│   └── gumstalker-trace-recorder.patch  # Stalker 修改补丁
├── tools/
│   └── trace_parser.c           # trace 文件解析工具
├── tests/
│   ├── test_trace.c             # 完整追踪测试
│   └── test_stalker_basic.c     # 基础 Stalker 测试
├── example/FridaTraceDemo/      # iOS Xcode 示例工程
├── setup.sh                     # 一键环境搭建脚本
├── ios-arm64.cross              # Meson iOS 交叉编译配置
└── frida-gum/                   # (gitignored) 克隆的 frida-gum
```

## Key Files

| 文件 | 用途 |
|------|------|
| `src/trace/gumtraceinline-arm64.c` | ARM64 micro-prolog/epilog 代码生成 (176 字节栈帧, 保存 X0-X18/LR/NZCV) |
| `src/trace/gumtracerecorder.c` | 无锁环形缓冲区实现 (默认 1M 记录, 100ms 刷写) |
| `src/trace/gumtrace.c` | 顶层 API, 连接 Stalker 与 Recorder |
| `patches/gumstalker-trace-recorder.patch` | 修改 Stalker 的 `iterator_next()` 和 `iterator_keep()` |

## Build Commands

### 初始设置
```bash
./setup.sh   # 克隆 frida-gum, 复制源码, 应用补丁, 初始化子模块
```

### iOS ARM64 构建
```bash
cd frida-gum
./configure --host=ios-arm64 -- -Dtests=disabled
make
# 产物: frida-gum/build/gum/libfrida-gum-1.0.a
```

### macOS 测试构建
```bash
cd frida-gum
./configure --host=macos-arm64 -- -Dtests=disabled -Dprefix=$PWD/build-macos
make
# 产物: frida-gum/build-macos/gum/libfrida-gum-1.0.a
```

### 编译测试程序 (macOS)
```bash
SDK=frida-gum/deps/sdk-macos-arm64
GUM=frida-gum
cc -o tests/test_trace tests/test_trace.c \
  -I "$GUM" -I "$GUM/gum" -I "$GUM/gum/trace" -I "$GUM/build-macos/gum" \
  -I "$SDK/include/glib-2.0" -I "$SDK/lib/glib-2.0/include" \
  -I "$SDK/include/capstone" -I "$SDK/include/json-glib-1.0" \
  "$GUM/build-macos/gum/libfrida-gum-1.0.a" \
  -L "$SDK/lib" \
  -lglib-2.0 -lgobject-2.0 -lgio-2.0 -lffi -lz \
  -lcapstone -lpcre2-8 -ljson-glib-1.0 -lsqlite3 -llzma -lxml2 \
  -framework Foundation -framework CoreFoundation \
  -lresolv -ldl -lpthread -lbsm -liconv -lc++ -arch arm64
```

### trace_parser 工具
```bash
cc -o tests/trace_parser tools/trace_parser.c -arch arm64
# 用法:
./tests/trace_parser trace.bin --stats
./tests/trace_parser trace.bin --json --limit 20
./tests/trace_parser trace.bin --filter-type call --limit 50
```

## ARM64 Technical Details

### Micro-Prolog 栈帧布局 (176 字节)
```
[SP+160]: NZCV 条件标志
[SP+144]: X18, LR
[SP+128]: X14, X15
[SP+112]: X12, X13
[SP+ 96]: X10, X11
[SP+ 80]: X8,  X9
[SP+ 64]: X6,  X7
[SP+ 48]: X4,  X5
[SP+ 32]: X2,  X3
[SP+ 16]: X0,  X1
[SP+  0]: X16, X17
```

**重要**: 必须保存所有 ARM64 ABI caller-saved 寄存器 (X0-X18, LR, NZCV)。
X19-X28 是 callee-saved，C 函数调用不会破坏，无需保存。

### Trace 文件格式 (FRIT)
- Magic: `0x46524954` ("FRIT")
- Version: 1
- 64 字节文件头 + N × 32 字节记录
- 记录类型: EXEC(0), CALL(1), RET(2), BLOCK(3)

## Important Conventions

- **frida-gum 目录被 gitignore**: 不要提交 frida-gum/ 到仓库，它由 setup.sh 自动克隆
- **源码双份存在**: `src/trace/` 是源头，`frida-gum/gum/trace/` 是 setup.sh 复制的副本
- **修改源码后**: 需要同步 `src/trace/` → `frida-gum/gum/trace/`，然后重新 `make`
- **Include 路径注意**: `gum.h` 使用 `#include <gum/gumstalker.h>` (尖括号)，编译时需要 `-I "$GUM"` 让项目头文件优先于 `/usr/local/include/gum/`
- **iOS 构建目录**: `frida-gum/build/` = iOS，`frida-gum/build-macos/` = macOS

## Common Issues

1. **编译时找到系统 frida 头文件**: 确保 `-I "$GUM"` 在 include 路径最前面
2. **链接缺少符号**: 需要链接 SDK 中所有依赖库 (capstone, pcre2, json-glib, sqlite3 等)
3. **"Already configured" 错误**: `build/` 目录已存在，直接 `make` 即可；若需重配置，删除 `build/` 目录
4. **寄存器破坏崩溃**: 内联代码必须保存所有 caller-saved 寄存器 (已修复)
