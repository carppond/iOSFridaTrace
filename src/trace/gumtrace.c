/*
 * GumTrace - Top-level API connecting GumStalker with GumTraceRecorder.
 *
 * Supports two tracing strategies:
 *
 * Strategy B (default, deep optimization):
 *   Uses gum_stalker_set_trace_recorder() to embed the recorder pointer
 *   into Stalker. The modified Stalker generates inline ARM64 code with
 *   a micro-prolog (~15 insns per trace point) instead of PROLOG_FULL
 *   (~50+ insns). No transformer callback needed for tracing.
 *
 * Strategy A (fallback, callout-based):
 *   Uses a custom transformer + put_callout() to record trace events.
 *   Works without modifying Stalker source. Slower due to PROLOG_FULL
 *   overhead on each callout.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gumtrace.h"
#include "gumtracerecorder.h"

#include <gum/gum.h>
#include <gum/gumstalker.h>
#include <string.h>

struct _GumTraceSession
{
  GumStalker * stalker;
  GumTraceRecorder * recorder;
  GumStalkerTransformer * transformer;
  GumEventSink * sink;

  GumThreadId target_thread;
  gboolean tracing_self;
};

/* --- Public API --- */

GumTraceSession *
gum_trace_start (const gchar * module_name,
                 guint64       start_addr,
                 guint64       size,
                 const gchar * output_path)
{
  return gum_trace_start_with_options (module_name, start_addr, size,
      output_path, 0, 0, TRUE);
}

GumTraceSession *
gum_trace_start_with_options (const gchar * module_name,
                              guint64       start_addr,
                              guint64       size,
                              const gchar * output_path,
                              guint         buffer_size,
                              guint         flush_interval_ms,
                              gboolean      record_timestamps)
{
  GumTraceSession * session;
  GumTraceRecorderConfig config;

  gum_init_embedded ();

  session = g_new0 (GumTraceSession, 1);

  /* Configure recorder */
  memset (&config, 0, sizeof (config));
  config.buffer_size = buffer_size;
  config.flush_interval_ms = flush_interval_ms;
  config.record_timestamps = record_timestamps;

  if (module_name != NULL)
  {
    strncpy (config.filter.module_name, module_name,
        sizeof (config.filter.module_name) - 1);
  }
  config.filter.range_base = start_addr;
  config.filter.range_size = size;

  if (output_path != NULL)
  {
    strncpy (config.output_path, output_path,
        sizeof (config.output_path) - 1);
  }

  /* Create recorder */
  session->recorder = gum_trace_recorder_new (&config);

  /* Create Stalker */
  session->stalker = gum_stalker_new ();
  gum_stalker_set_trust_threshold (session->stalker, -1);

  /*
   * Strategy B: Set the trace recorder directly on Stalker.
   * The modified Stalker will generate inline trace code during
   * block compilation, using micro-prolog for minimal overhead.
   * No custom transformer needed - use the default one.
   */
  gum_stalker_set_trace_recorder (session->stalker, session->recorder);

  session->transformer = gum_stalker_transformer_make_default ();
  session->sink = gum_event_sink_make_default ();

  /* Start recording */
  gum_trace_recorder_start (session->recorder);

  /* Follow current thread */
  session->tracing_self = TRUE;
  gum_stalker_follow_me (session->stalker, session->transformer,
      session->sink);

  return session;
}

void
gum_trace_stop (GumTraceSession * session)
{
  if (session == NULL)
    return;

  if (session->tracing_self)
    gum_stalker_unfollow_me (session->stalker);

  /* Detach recorder from Stalker before freeing */
  gum_stalker_set_trace_recorder (session->stalker, NULL);

  gum_trace_recorder_stop (session->recorder);
  gum_trace_recorder_free (session->recorder);

  g_object_unref (session->transformer);
  g_object_unref (session->sink);
  g_object_unref (session->stalker);

  g_free (session);
}

guint64
gum_trace_get_record_count (GumTraceSession * session)
{
  return gum_trace_recorder_get_total_records (session->recorder);
}

guint64
gum_trace_get_dropped_count (GumTraceSession * session)
{
  return gum_trace_recorder_get_dropped_records (session->recorder);
}

void
gum_trace_flush (GumTraceSession * session)
{
  gum_trace_recorder_flush (session->recorder);
}
