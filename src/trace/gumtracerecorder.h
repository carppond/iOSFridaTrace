/*
 * GumTraceRecorder - High-performance instruction trace recorder for Stalker
 *
 * Designed for global instruction-level tracing on iOS ARM64.
 * Uses a lock-free ring buffer to minimize overhead, bypassing the
 * GumEventSink GObject virtual dispatch entirely.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __GUM_TRACE_RECORDER_H__
#define __GUM_TRACE_RECORDER_H__

#include <gum/gumdefs.h>
#include <gum/gumprocess.h>

G_BEGIN_DECLS

/*
 * Trace record types - what kind of event was recorded.
 */
typedef enum {
  GUM_TRACE_EXEC  = 0,  /* Instruction executed */
  GUM_TRACE_CALL  = 1,  /* Function call (BL/BLR) */
  GUM_TRACE_RET   = 2,  /* Function return (RET) */
  GUM_TRACE_BLOCK = 3,  /* Entered a new basic block */
} GumTraceType;

/*
 * A single trace record - 32 bytes, cache-line friendly.
 *
 * For EXEC:  location = instruction address
 * For CALL:  location = call site, target = call target
 * For RET:   location = ret instruction, target = return address
 * For BLOCK: location = block start address, target = block end address
 */
typedef struct {
  guint8  type;         /* GumTraceType */
  guint8  _reserved[3];
  guint32 thread_id;
  guint64 location;     /* Address where the event happened */
  guint64 target;       /* Target address (for CALL/RET/BLOCK) */
  guint64 timestamp;    /* Mach absolute time (low overhead on iOS) */
} GumTraceRecord;

G_STATIC_ASSERT (sizeof (GumTraceRecord) == 32);

/*
 * Filter configuration - which addresses to trace.
 */
typedef struct {
  gchar   module_name[256]; /* Executable/module name to trace */
  guint64 range_base;       /* Start address (0 = trace entire module) */
  guint64 range_size;       /* Size in bytes (0 = trace entire module) */
} GumTraceFilter;

/*
 * Recorder configuration.
 */
typedef struct {
  guint          buffer_size;    /* Ring buffer size in records (power of 2) */
  gchar          output_path[1024]; /* File path to write trace data */
  guint          flush_interval_ms; /* How often to flush buffer to disk */
  gboolean       record_timestamps; /* Include timestamps (small overhead) */
  GumTraceFilter filter;         /* Which addresses to trace */
} GumTraceRecorderConfig;

#ifndef __GUM_TRACE_RECORDER_TYPEDEF__
#define __GUM_TRACE_RECORDER_TYPEDEF__
typedef struct _GumTraceRecorder GumTraceRecorder;
#endif

/*
 * Create a new trace recorder with the given configuration.
 * Returns NULL on failure.
 */
GUM_API GumTraceRecorder * gum_trace_recorder_new (
    const GumTraceRecorderConfig * config);

/*
 * Destroy the recorder, flushing any remaining data.
 */
GUM_API void gum_trace_recorder_free (GumTraceRecorder * self);

/*
 * Start recording. The background flush thread begins.
 */
GUM_API void gum_trace_recorder_start (GumTraceRecorder * self);

/*
 * Stop recording and flush remaining data to disk.
 */
GUM_API void gum_trace_recorder_stop (GumTraceRecorder * self);

/*
 * Core recording functions - called from Stalker instrumented code.
 * These are designed to be as fast as possible (no locks, no allocation).
 *
 * IMPORTANT: These are called from recompiled code at very high frequency.
 * They must be async-signal-safe and lock-free.
 */
GUM_API void gum_trace_recorder_record_exec (GumTraceRecorder * self,
    gpointer location);

GUM_API void gum_trace_recorder_record_call (GumTraceRecorder * self,
    gpointer location, gpointer target);

GUM_API void gum_trace_recorder_record_ret (GumTraceRecorder * self,
    gpointer location, gpointer target);

GUM_API void gum_trace_recorder_record_block (GumTraceRecorder * self,
    gpointer start, gpointer end);

/*
 * Check if an address falls within the trace filter range.
 * Used during block compilation to decide whether to instrument.
 */
GUM_API gboolean gum_trace_recorder_should_trace (GumTraceRecorder * self,
    gpointer address);

/*
 * Get statistics about the recorder.
 */
GUM_API guint64 gum_trace_recorder_get_total_records (
    GumTraceRecorder * self);
GUM_API guint64 gum_trace_recorder_get_dropped_records (
    GumTraceRecorder * self);

/*
 * Manually flush the buffer to disk.
 */
GUM_API void gum_trace_recorder_flush (GumTraceRecorder * self);

G_END_DECLS

#endif
