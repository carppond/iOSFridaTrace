/**
 * fridatrace.js - Remote controller for FridaTrace C library.
 *
 * This script calls our native C API (gum_trace_start_thread / gum_trace_stop)
 * via Frida's NativeFunction. The actual tracing uses our patched Stalker with
 * inline ARM64 micro-prolog and lock-free ring buffer — NOT Frida's JS Stalker.
 *
 * Requirements:
 *   - Target app/dylib must link libfrida-gum-1.0.a (our patched version)
 *   - For jailbroken: frida-server on device
 *   - For non-jailbroken: FridaGadget embedded in app
 *
 * Usage:
 *   frida -U -n <AppName> -l fridatrace.js
 *   frida -U -f <BundleId> -l fridatrace.js --no-pause
 *
 * Commands (in REPL):
 *   modules()                              - List loaded modules
 *   threads()                              - List threads
 *   trace("ModuleName")                    - Trace entire module (main thread)
 *   trace("ModuleName", 0x1234)            - From offset (relative to module base)
 *   trace("ModuleName", 0x1234, 0x5678)    - Specific range (offsets)
 *   trace("ModuleName", 0, 0, threadId)    - Trace specific thread
 *   stop()                                 - Stop tracing
 *   stats()                                - Show statistics
 *   info()                                 - Show current session info
 */

'use strict';

/* ─── Locate our C API ──────────────────────────────────────────── */

function findFunc(name) {
    /* Search all loaded images */
    let addr = Module.findExportByName(null, name);
    if (addr !== null) return addr;

    /* If not in export table, try dlsym */
    const dlsym = new NativeFunction(
        Module.findExportByName(null, 'dlsym'),
        'pointer', ['pointer', 'pointer']);
    const RTLD_DEFAULT = ptr(-2);
    addr = dlsym(RTLD_DEFAULT, Memory.allocUtf8String(name));
    if (!addr.isNull()) return addr;

    return null;
}

function resolveAPI() {
    const api = {};

    const funcs = {
        /* name: [retType, argTypes] */
        'gum_trace_start_thread': ['pointer',
            ['pointer', 'uint64', 'uint64', 'pointer', 'uint64']],
        'gum_trace_stop': ['void', ['pointer']],
        'gum_trace_get_record_count': ['uint64', ['pointer']],
        'gum_trace_get_dropped_count': ['uint64', ['pointer']],
        'gum_trace_flush': ['void', ['pointer']],
    };

    for (const [name, sig] of Object.entries(funcs)) {
        const addr = findFunc(name);
        if (addr === null) {
            console.log(`[!] WARNING: ${name} not found`);
            continue;
        }
        api[name] = new NativeFunction(addr, sig[0], sig[1]);
    }

    return api;
}

const api = resolveAPI();

/* ─── State ─────────────────────────────────────────────────────── */

let g_session = null;
let g_moduleName = null;
let g_outputPath = null;

/* ─── Helpers ───────────────────────────────────────────────────── */

function getMainThreadId() {
    const threads = Process.enumerateThreads();
    /* Thread with index 0 is typically the main thread */
    return threads.length > 0 ? threads[0].id : 0;
}

function findModule(name) {
    let mod = Process.findModuleByName(name);
    if (mod !== null) return mod;

    /* Partial match */
    const mods = Process.enumerateModules();
    for (const m of mods) {
        if (m.name.toLowerCase().indexOf(name.toLowerCase()) !== -1)
            return m;
    }
    return null;
}

function getDocsDir() {
    /* iOS Documents directory */
    const NSSearchPathForDirectoriesInDomains = new NativeFunction(
        Module.findExportByName('Foundation', 'NSSearchPathForDirectoriesInDomains')
            || Module.findExportByName(null, 'NSSearchPathForDirectoriesInDomains'),
        'pointer', ['uint64', 'uint64', 'bool']);

    if (NSSearchPathForDirectoriesInDomains) {
        const dirs = new ObjC.Object(
            NSSearchPathForDirectoriesInDomains(9, 1, true));  /* NSDocumentDirectory */
        if (dirs.count() > 0) {
            return dirs.objectAtIndex_(0).toString();
        }
    }
    return '/var/mobile/Documents';
}

/* ─── Core commands ─────────────────────────────────────────────── */

function startTrace(moduleName, startOffset, endOffset, threadId) {
    if (g_session !== null) {
        console.log('[!] Already tracing. Call stop() first.');
        return;
    }

    if (!api.gum_trace_start_thread) {
        console.log('[!] gum_trace_start_thread not found. Is our library linked?');
        return;
    }

    /* Resolve module */
    let startAddr = 0;
    let size = 0;

    if (moduleName) {
        const mod = findModule(moduleName);
        if (mod === null) {
            console.log(`[!] Module "${moduleName}" not found.`);
            console.log('[*] Use modules() to list loaded modules.');
            return;
        }
        moduleName = mod.name;

        if (startOffset !== undefined && startOffset !== 0) {
            startAddr = mod.base.add(startOffset).toUInt32
                ? mod.base.add(startOffset) : startOffset;
            /* Convert to absolute address */
            startAddr = parseInt(mod.base) + startOffset;

            if (endOffset !== undefined && endOffset !== 0) {
                size = endOffset - startOffset;
            } else {
                size = mod.size - startOffset;
            }
        }

        console.log(`[*] Module:  ${mod.name}`);
        console.log(`[*] Base:    ${mod.base}  Size: 0x${mod.size.toString(16)}`);
        if (startAddr !== 0) {
            console.log(`[*] Offset:  0x${startOffset.toString(16)} - 0x${(startOffset + size).toString(16)}`);
        }
    }

    /* Resolve thread */
    const tid = threadId || getMainThreadId();
    console.log(`[*] Thread:  ${tid}`);

    /* Output path */
    const docsDir = getDocsDir();
    const timestamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
    const outFile = `${docsDir}/fridatrace_${timestamp}.bin`;
    g_outputPath = outFile;
    console.log(`[*] Output:  ${outFile}`);

    /* Allocate strings */
    const moduleStr = moduleName ? Memory.allocUtf8String(moduleName) : ptr(0);
    const outputStr = Memory.allocUtf8String(outFile);

    /* Call our C API */
    g_session = api.gum_trace_start_thread(
        moduleStr,
        startAddr,
        size,
        outputStr,
        tid);

    if (g_session.isNull()) {
        console.log('[!] ERROR: gum_trace_start_thread returned NULL');
        g_session = null;
        return;
    }

    g_moduleName = moduleName;
    console.log('[*] Tracing started!');
    console.log('[*] Use stop() to stop, stats() for live stats.');
}

function stopTrace() {
    if (g_session === null) {
        console.log('[!] Not tracing.');
        return;
    }

    if (!api.gum_trace_stop) {
        console.log('[!] gum_trace_stop not found.');
        return;
    }

    /* Get final stats before stopping */
    const total = api.gum_trace_get_record_count
        ? api.gum_trace_get_record_count(g_session) : 0;
    const dropped = api.gum_trace_get_dropped_count
        ? api.gum_trace_get_dropped_count(g_session) : 0;

    api.gum_trace_stop(g_session);
    g_session = null;

    console.log('[*] Tracing stopped.');
    console.log(`[*] Records: ${total}  Dropped: ${dropped}`);
    console.log(`[*] Output:  ${g_outputPath}`);
}

function showStats() {
    if (g_session === null) {
        console.log('[!] Not tracing.');
        return;
    }

    const total = api.gum_trace_get_record_count(g_session);
    const dropped = api.gum_trace_get_dropped_count(g_session);

    console.log(`[*] Records: ${total}  Dropped: ${dropped}`);
    if (g_moduleName) console.log(`[*] Module:  ${g_moduleName}`);
    console.log(`[*] Output:  ${g_outputPath}`);
}

function showInfo() {
    console.log(`[*] Session: ${g_session !== null ? 'ACTIVE' : 'IDLE'}`);
    if (g_session) {
        showStats();
    }
    console.log(`[*] Process: ${Process.id} (${Process.arch})`);
    console.log(`[*] Main:    ${Process.enumerateModules()[0].name}`);
}

function flushTrace() {
    if (g_session === null) {
        console.log('[!] Not tracing.');
        return;
    }
    api.gum_trace_flush(g_session);
    console.log('[*] Flushed.');
}

function listModules() {
    const mods = Process.enumerateModules();
    console.log(`\n  ${'Base'.padEnd(20)} ${'Size'.padEnd(12)} Name`);
    console.log(`  ${'─'.repeat(20)} ${'─'.repeat(12)} ${'─'.repeat(30)}`);
    for (const m of mods) {
        console.log(`  ${m.base.toString().padEnd(20)} 0x${m.size.toString(16).padStart(8, '0')}   ${m.name}`);
    }
    console.log(`\n  Total: ${mods.length} modules\n`);
}

function listThreads() {
    const threads = Process.enumerateThreads();
    console.log(`\n  ${'ID'.padEnd(8)} ${'State'.padEnd(12)} PC`);
    console.log(`  ${'─'.repeat(8)} ${'─'.repeat(12)} ${'─'.repeat(20)}`);
    for (const t of threads) {
        const sym = DebugSymbol.fromAddress(t.context.pc);
        const name = sym && sym.name ? sym.name : '';
        console.log(`  ${t.id.toString().padEnd(8)} ${t.state.padEnd(12)} ${t.context.pc}  ${name}`);
    }
    console.log(`\n  Total: ${threads.length} threads\n`);
}

/* ─── REPL exports ──────────────────────────────────────────────── */

globalThis.trace = startTrace;
globalThis.stop = stopTrace;
globalThis.stats = showStats;
globalThis.info = showInfo;
globalThis.flush = flushTrace;
globalThis.modules = listModules;
globalThis.threads = listThreads;

/* ─── RPC exports (for Python automation) ───────────────────────── */

rpc.exports = {
    trace: startTrace,
    stop: stopTrace,
    stats: function () {
        if (!g_session) return { total: 0, dropped: 0 };
        return {
            total: api.gum_trace_get_record_count(g_session).toNumber(),
            dropped: api.gum_trace_get_dropped_count(g_session).toNumber(),
        };
    },
    info: function () {
        return {
            active: g_session !== null,
            module: g_moduleName,
            output: g_outputPath,
            pid: Process.id,
        };
    },
    listModules: function () {
        return Process.enumerateModules().map(m => ({
            name: m.name, base: m.base.toString(), size: m.size,
        }));
    },
};

/* ─── Startup ───────────────────────────────────────────────────── */

const foundCount = Object.keys(api).length;
const required = ['gum_trace_start_thread', 'gum_trace_stop',
    'gum_trace_get_record_count', 'gum_trace_get_dropped_count'];
const missing = required.filter(n => !api[n]);

console.log('');
console.log('╔══════════════════════════════════════════════╗');
console.log('║     FridaTrace - Native Library Controller   ║');
console.log('╠══════════════════════════════════════════════╣');
console.log('║  modules()                 List modules      ║');
console.log('║  threads()                 List threads      ║');
console.log('║  trace("Mod")              Trace module      ║');
console.log('║  trace("Mod", 0x100, 0x500)  Trace range     ║');
console.log('║  trace("Mod", 0, 0, tid)   Trace thread     ║');
console.log('║  stop()                    Stop & save       ║');
console.log('║  stats()                   Live stats        ║');
console.log('║  flush()                   Flush buffer      ║');
console.log('╚══════════════════════════════════════════════╝');
console.log('');
console.log(`[*] Process: ${Process.id} (${Process.arch})`);
console.log(`[*] Main:    ${Process.enumerateModules()[0].name}`);
console.log(`[*] API:     ${foundCount} functions found`);

if (missing.length > 0) {
    console.log(`[!] MISSING: ${missing.join(', ')}`);
    console.log('[!] Is our patched frida-gum library linked in this app?');
} else {
    console.log('[*] All API functions resolved. Ready to trace.');
}
console.log('');
