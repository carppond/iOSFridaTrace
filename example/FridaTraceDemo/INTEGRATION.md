# FridaTrace iOS Integration Guide

## Quick Start

### 1. Build frida-gum

```bash
cd FridaTrace
./setup.sh                                    # Clone and patch frida-gum
cd frida-gum
./configure --host=ios-arm64 -- -Dtests=disabled
make
```

### 2. Prepare libraries for your project

```bash
cd example/FridaTraceDemo
./prepare_libs.sh
```

### 3. Xcode Project Settings

Add to your target's **Build Settings**:

| Setting | Value |
|---------|-------|
| Header Search Paths | `$(PROJECT_DIR)/libs/include` |
| Library Search Paths | `$(PROJECT_DIR)/libs/lib` |
| Other Linker Flags | `-lfrida-gum-1.0 -lglib-2.0 -lgobject-2.0 -lgthread-2.0 -lffi -lcapstone -lpcre2-8 -liconv -lcharset -lz -lresolv` |

### 4. Usage in Code

```objc
#import "fridatrace.h"

// Start tracing (e.g., in application:didFinishLaunchingWithOptions:)
GumTraceSession *session = gum_trace_start(
    "MyApp",           // Module name (your executable)
    0,                 // Start address (0 = entire module)
    0,                 // Size (0 = entire module)
    "/path/trace.bin"  // Output file
);

// Stop tracing (e.g., in applicationWillTerminate:)
gum_trace_stop(session);
```

### 5. Analyze Trace Data

```bash
# Build the parser
cc -o trace_parser tools/trace_parser.c

# View trace statistics
./trace_parser trace.bin --stats

# View first 100 records
./trace_parser trace.bin --limit 100

# Filter by type
./trace_parser trace.bin --filter-type call

# Export as JSON
./trace_parser trace.bin --json > trace.json
```

## API Reference

| Function | Description |
|----------|-------------|
| `gum_trace_start(module, addr, size, path)` | Start tracing |
| `gum_trace_stop(session)` | Stop and flush |
| `gum_trace_flush(session)` | Manual flush to disk |
| `gum_trace_get_record_count(session)` | Total records captured |
| `gum_trace_get_dropped_count(session)` | Records dropped (buffer full) |

## Trace Record Format

Each record is 32 bytes:

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | Type: 0=EXEC, 1=CALL, 2=RET, 3=BLOCK |
| 1-3 | 3 | Reserved |
| 4 | 4 | Thread ID |
| 8 | 8 | Location (instruction address) |
| 16 | 8 | Target (call/ret target address) |
| 24 | 8 | Timestamp (mach_absolute_time) |

## Non-Jailbroken Deployment

For internal apps on non-jailbroken devices:

1. Link `libfrida-gum-1.0.a` statically into your app
2. Sign with your enterprise/development certificate
3. The trace module works entirely within the app's process
4. No special entitlements required
