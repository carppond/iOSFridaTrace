/*
 * GumTrace - Top-level API for instruction tracing with Stalker.
 *
 * Provides a simple one-call interface to start global instruction tracing.
 * Integrates GumTraceRecorder with GumStalker internally.
 *
 * Usage in your iOS app:
 *
 *   #include <gum/trace/gumtrace.h>
 *
 *   // Start tracing all instructions in "MyApp" executable
 *   GumTraceSession * session = gum_trace_start ("MyApp", 0, 0,
 *       "/tmp/trace.bin");
 *
 *   // ... app runs, instructions are being recorded ...
 *
 *   // Stop and flush
 *   gum_trace_stop (session);
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __GUM_TRACE_H__
#define __GUM_TRACE_H__

#include <gum/gumdefs.h>
#include <gum/gumprocess.h>

G_BEGIN_DECLS

typedef struct _GumTraceSession GumTraceSession;

/*
 * Start a global instruction trace session.
 *
 * @module_name: Name of the executable/module to trace (e.g., "MyApp").
 *               Pass NULL or "" to trace all modules.
 * @start_addr:  Start address within the module (0 = module base).
 * @size:        Size of the address range to trace (0 = entire module).
 * @output_path: File path to write trace data. Pass NULL for in-memory only.
 *
 * Returns a session handle, or NULL on failure.
 */
GUM_API GumTraceSession * gum_trace_start (const gchar * module_name,
    guint64 start_addr, guint64 size, const gchar * output_path);

/*
 * Start tracing with full configuration control.
 * thread_id: 0 = current thread.
 */
GUM_API GumTraceSession * gum_trace_start_with_options (
    const gchar * module_name, guint64 start_addr, guint64 size,
    const gchar * output_path, guint buffer_size, guint flush_interval_ms,
    gboolean record_timestamps, GumThreadId thread_id);

/*
 * Start tracing a specific thread.
 *
 * @thread_id: Target thread ID (from gum_process_get_current_thread_id(),
 *             or obtained via Process.enumerateThreads() in Frida JS).
 *             Pass 0 to trace the current (calling) thread.
 *
 * Designed for remote control via Frida NativeFunction.
 */
GUM_API GumTraceSession * gum_trace_start_thread (const gchar * module_name,
    guint64 start_addr, guint64 size, const gchar * output_path,
    GumThreadId thread_id);

/*
 * Stop the trace session and flush all data.
 * The session handle is freed after this call.
 */
GUM_API void gum_trace_stop (GumTraceSession * session);

/*
 * Get the number of records captured so far.
 */
GUM_API guint64 gum_trace_get_record_count (GumTraceSession * session);

/*
 * Get the number of records dropped due to buffer overflow.
 */
GUM_API guint64 gum_trace_get_dropped_count (GumTraceSession * session);

/*
 * Manually flush the trace buffer to disk.
 */
GUM_API void gum_trace_flush (GumTraceSession * session);

G_END_DECLS

#endif
