# FridaTrace

iOS ARM64 instruction-level tracing library built on [frida-gum](https://github.com/nicejudy/frida-gum) Stalker.

FridaTrace hooks into Stalker's dynamic recompilation engine to record every instruction execution, function call, and return with minimal overhead. Trace data is written to a compact binary format for offline analysis.

## Features

- **Inline ARM64 code generation** - Custom micro-prolog (~30 instructions) instead of Stalker's full prolog (~50+), with complete integer + NEON/FP register preservation
- **Lock-free ring buffer** - Single-producer / single-consumer design, no mutex contention
- **Background flush** - Dedicated thread writes to disk on 100ms intervals
- **Event types** - EXEC (instruction), CALL (BL/BLR/BLRAA), RET (RET/RETAA/RETAB), BLOCK (basic block entry)
- **Module filtering** - Trace specific modules or address ranges
- **32-byte fixed records** - Cache-line friendly, includes thread ID and timestamp
- **No jailbreak required** - Static linking, runs within app sandbox


## Quick Start

```bash
# 1. Clone and setup
git clone https://github.com/carppond/iOSFridaTrace.git
cd iOSFridaTrace
./setup.sh


# 2. Build for iOS
cd frida-gum
./configure --host=ios-arm64 -- -Dtests=disabled
make
```

The static library is at `frida-gum/build/gum/libfrida-gum-1.0.a`.

## API Usage

```c
#include "gumtrace.h"

// Start tracing
GumTraceSession *session = gum_trace_start(
    "MyApp",              // module name (NULL = trace everything)
    0,                    // start address (0 = entire module)
    0,                    // size (0 = entire module)
    "/path/trace.bin"     // output file
);

// ... application runs ...

// Get statistics
guint64 records = gum_trace_get_record_count(session);
guint64 dropped = gum_trace_get_dropped_count(session);

// Stop and flush
gum_trace_stop(session);
```

## Trace Analysis

```bash
# Build the parser
cc -o trace_parser tools/trace_parser.c

# View statistics
./trace_parser trace.bin --stats

# View first 50 records
./trace_parser trace.bin --limit 50

# Filter by event type
./trace_parser trace.bin --filter-type call --limit 100

# Export as JSON
./trace_parser trace.bin --json > trace.json
```

## Project Structure

```
FridaTrace/
├── src/trace/                    # Core trace module
│   ├── gumtrace.h/c             # Top-level API (start/stop/flush)
│   ├── gumtracerecorder.h/c     # Lock-free ring buffer recorder
│   └── gumtraceinline-arm64.h/c # ARM64 inline code generation
├── patches/                      # frida-gum Stalker patches
├── tools/trace_parser.c          # Binary trace file parser
├── tests/                        # macOS test programs
├── example/FridaTraceDemo/       # iOS Xcode demo app
└── setup.sh                      # One-step environment setup
```

## Trace Record Format

Binary file with magic `0x46524954` ("FRIT"), version 1.

Each record is 32 bytes:

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | Type: 0=EXEC, 1=CALL, 2=RET, 3=BLOCK |
| 1-3 | 3 | Reserved |
| 4 | 4 | Thread ID |
| 8 | 8 | Location (instruction address) |
| 16 | 8 | Target (call/return target) |
| 24 | 8 | Timestamp (mach_absolute_time) |

## iOS Integration

See [example/FridaTraceDemo/INTEGRATION.md](example/FridaTraceDemo/INTEGRATION.md) for Xcode project setup, build settings, and deployment instructions.

## License

MIT License - see [LICENSE](LICENSE) for details.
