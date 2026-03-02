/*
 * GumTraceRecorder - High-performance instruction trace recorder
 *
 * Implementation uses a single-producer lock-free ring buffer per thread
 * and a background thread that periodically flushes data to disk.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gumtracerecorder.h"

#include <gum/gummemory.h>
#include <gum/gummodule.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#ifdef HAVE_DARWIN
# include <mach/mach_time.h>
#endif

#define GUM_TRACE_DEFAULT_BUFFER_SIZE    (1024 * 1024)  /* 1M records */
#define GUM_TRACE_DEFAULT_FLUSH_INTERVAL 100            /* 100ms */
#define GUM_TRACE_FILE_HEADER_MAGIC      0x46524954     /* "FRIT" */
#define GUM_TRACE_FILE_VERSION           1

/*
 * File header written at the beginning of the trace output file.
 */
typedef struct {
  guint32 magic;
  guint32 version;
  guint32 record_size;
  guint32 _padding;
  guint64 start_time;
  gchar   module_name[256];
  guint64 range_base;
  guint64 range_size;
} GumTraceFileHeader;

/*
 * The ring buffer: single-producer (Stalker thread), single-consumer
 * (flush thread). Uses atomic operations on write_pos and read_pos.
 *
 * We use a power-of-2 size so modulo can be done with bitwise AND.
 */
struct _GumTraceRecorder
{
  /* Ring buffer */
  GumTraceRecord * buffer;
  guint            buffer_size;     /* Must be power of 2 */
  guint            buffer_mask;     /* buffer_size - 1 */
  volatile guint   write_pos;       /* Next write position (atomic) */
  volatile guint   read_pos;        /* Next read position (atomic) */

  /* Statistics */
  volatile guint64 total_records;
  volatile guint64 dropped_records;

  /* Configuration */
  GumTraceRecorderConfig config;

  /* Filter: resolved module range */
  gboolean filter_active;
  guint64  filter_base;
  guint64  filter_end;

  /* Output file */
  int      output_fd;

  /* Flush thread */
  GThread * flush_thread;
  volatile gboolean running;

  /* Timestamp baseline */
  guint64 start_time;
};

/* --- Internal helpers --- */

static guint
gum_round_up_to_power_of_2 (guint v)
{
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  return v;
}

static inline guint64
gum_trace_get_timestamp (void)
{
#ifdef HAVE_DARWIN
  return mach_absolute_time ();
#else
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (guint64) ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
}

static gboolean
gum_trace_resolve_filter (GumTraceRecorder * self)
{
  const GumTraceFilter * f = &self->config.filter;

  if (f->module_name[0] == '\0')
  {
    self->filter_active = FALSE;
    return TRUE;
  }

  if (f->range_base != 0 && f->range_size != 0)
  {
    self->filter_base = f->range_base;
    self->filter_end = f->range_base + f->range_size;
    self->filter_active = TRUE;
    return TRUE;
  }

  /*
   * Try to resolve by module name. On iOS this finds the Mach-O in memory.
   * If the module isn't loaded yet, we'll retry on first should_trace call.
   */
  {
    GumModule * module;
    module = gum_process_find_module_by_name (f->module_name);
    if (module != NULL)
    {
      const GumMemoryRange * range = gum_module_get_range (module);
      self->filter_base = range->base_address;
      self->filter_end = range->base_address + range->size;
      self->filter_active = TRUE;
      g_object_unref (module);
      return TRUE;
    }
  }

  /* Module not found yet - will trace everything until resolved */
  self->filter_active = FALSE;
  return FALSE;
}

static void
gum_trace_write_file_header (GumTraceRecorder * self)
{
  GumTraceFileHeader header;

  memset (&header, 0, sizeof (header));
  header.magic = GUM_TRACE_FILE_HEADER_MAGIC;
  header.version = GUM_TRACE_FILE_VERSION;
  header.record_size = sizeof (GumTraceRecord);
  header.start_time = self->start_time;
  strncpy (header.module_name, self->config.filter.module_name,
      sizeof (header.module_name) - 1);
  header.range_base = self->filter_base;
  header.range_size = self->filter_end - self->filter_base;

  write (self->output_fd, &header, sizeof (header));
}

static void
gum_trace_flush_buffer (GumTraceRecorder * self)
{
  guint rp, wp, count;

  rp = g_atomic_int_get ((volatile gint *) &self->read_pos);
  wp = g_atomic_int_get ((volatile gint *) &self->write_pos);

  if (rp == wp)
    return;

  if (wp > rp)
  {
    count = wp - rp;
    write (self->output_fd, &self->buffer[rp & self->buffer_mask],
        count * sizeof (GumTraceRecord));
  }
  else
  {
    /* Wrap-around: write in two parts */
    guint first_part = self->buffer_size - (rp & self->buffer_mask);
    guint second_part = wp & self->buffer_mask;

    if (first_part > 0)
    {
      write (self->output_fd, &self->buffer[rp & self->buffer_mask],
          first_part * sizeof (GumTraceRecord));
    }
    if (second_part > 0)
    {
      write (self->output_fd, &self->buffer[0],
          second_part * sizeof (GumTraceRecord));
    }
  }

  g_atomic_int_set ((volatile gint *) &self->read_pos, wp);
}

static gpointer
gum_trace_flush_thread (gpointer data)
{
  GumTraceRecorder * self = (GumTraceRecorder *) data;
  guint interval_us = self->config.flush_interval_ms * 1000;

  while (g_atomic_int_get ((volatile gint *) &self->running))
  {
    g_usleep (interval_us);
    gum_trace_flush_buffer (self);
  }

  /* Final flush */
  gum_trace_flush_buffer (self);

  return NULL;
}

/* --- Public API --- */

GumTraceRecorder *
gum_trace_recorder_new (const GumTraceRecorderConfig * config)
{
  GumTraceRecorder * self;
  guint buf_size;

  self = g_new0 (GumTraceRecorder, 1);
  memcpy (&self->config, config, sizeof (GumTraceRecorderConfig));

  /* Ensure buffer size is power of 2 */
  buf_size = config->buffer_size;
  if (buf_size == 0)
    buf_size = GUM_TRACE_DEFAULT_BUFFER_SIZE;
  buf_size = gum_round_up_to_power_of_2 (buf_size);

  self->buffer_size = buf_size;
  self->buffer_mask = buf_size - 1;
  self->buffer = (GumTraceRecord *) g_malloc0 (
      buf_size * sizeof (GumTraceRecord));

  self->write_pos = 0;
  self->read_pos = 0;
  self->total_records = 0;
  self->dropped_records = 0;
  self->running = FALSE;

  if (self->config.flush_interval_ms == 0)
    self->config.flush_interval_ms = GUM_TRACE_DEFAULT_FLUSH_INTERVAL;

  /* Resolve filter */
  gum_trace_resolve_filter (self);

  /* Open output file */
  self->output_fd = -1;
  if (config->output_path[0] != '\0')
  {
    self->output_fd = open (config->output_path,
        O_WRONLY | O_CREAT | O_TRUNC, 0644);
  }

  return self;
}

void
gum_trace_recorder_free (GumTraceRecorder * self)
{
  if (self == NULL)
    return;

  gum_trace_recorder_stop (self);

  if (self->output_fd >= 0)
    close (self->output_fd);

  g_free (self->buffer);
  g_free (self);
}

void
gum_trace_recorder_start (GumTraceRecorder * self)
{
  if (g_atomic_int_get ((volatile gint *) &self->running))
    return;

  self->start_time = gum_trace_get_timestamp ();
  g_atomic_int_set ((volatile gint *) &self->running, TRUE);

  if (self->output_fd >= 0)
    gum_trace_write_file_header (self);

  self->flush_thread = g_thread_new ("gum-trace-flush",
      gum_trace_flush_thread, self);
}

void
gum_trace_recorder_stop (GumTraceRecorder * self)
{
  if (!g_atomic_int_get ((volatile gint *) &self->running))
    return;

  g_atomic_int_set ((volatile gint *) &self->running, FALSE);

  if (self->flush_thread != NULL)
  {
    g_thread_join (self->flush_thread);
    self->flush_thread = NULL;
  }
}

/*
 * Fast-path inline record writing.
 * Single-producer so no CAS needed - just atomic increment of write_pos.
 */
static inline void
gum_trace_recorder_write (GumTraceRecorder * self,
                          GumTraceType        type,
                          gpointer            location,
                          gpointer            target)
{
  guint wp, rp, next_wp;
  GumTraceRecord * record;

  wp = g_atomic_int_get ((volatile gint *) &self->write_pos);
  next_wp = (wp + 1) & self->buffer_mask;
  rp = g_atomic_int_get ((volatile gint *) &self->read_pos);

  /* Check if buffer is full - drop record if so */
  if (G_UNLIKELY (next_wp == (rp & self->buffer_mask)))
  {
    g_atomic_int_add ((volatile gint *) &self->dropped_records, 1);
    return;
  }

  record = &self->buffer[wp & self->buffer_mask];
  record->type = (guint8) type;
  record->thread_id = (guint32) gum_process_get_current_thread_id ();
  record->location = (guint64) location;
  record->target = (guint64) target;

  if (self->config.record_timestamps)
    record->timestamp = gum_trace_get_timestamp ();

  /* Publish the record */
  g_atomic_int_set ((volatile gint *) &self->write_pos, next_wp);
  g_atomic_int_add ((volatile gint *) &self->total_records, 1);
}

void
gum_trace_recorder_record_exec (GumTraceRecorder * self,
                                gpointer           location)
{
  gum_trace_recorder_write (self, GUM_TRACE_EXEC, location, NULL);
}

void
gum_trace_recorder_record_call (GumTraceRecorder * self,
                                gpointer           location,
                                gpointer           target)
{
  gum_trace_recorder_write (self, GUM_TRACE_CALL, location, target);
}

void
gum_trace_recorder_record_call_ex (GumTraceRecorder * self,
                                   gpointer           location,
                                   gpointer           arg0,
                                   gpointer           arg1)
{
  gum_trace_recorder_write (self, GUM_TRACE_CALL, location, NULL);
  gum_trace_recorder_write (self, GUM_TRACE_CALL_ARGS, arg0, arg1);
}

void
gum_trace_recorder_record_ret (GumTraceRecorder * self,
                               gpointer           location,
                               gpointer           target)
{
  gum_trace_recorder_write (self, GUM_TRACE_RET, location, target);
}

void
gum_trace_recorder_record_block (GumTraceRecorder * self,
                                 gpointer           start,
                                 gpointer           end)
{
  gum_trace_recorder_write (self, GUM_TRACE_BLOCK, start, end);
}

gboolean
gum_trace_recorder_should_trace (GumTraceRecorder * self,
                                 gpointer           address)
{
  guint64 addr;

  if (!self->filter_active)
  {
    /* Try resolving again if filter is configured but not yet resolved */
    if (self->config.filter.module_name[0] != '\0')
      gum_trace_resolve_filter (self);

    if (!self->filter_active)
      return TRUE;  /* No filter = trace everything */
  }

  addr = (guint64) address;
  return (addr >= self->filter_base && addr < self->filter_end);
}

guint64
gum_trace_recorder_get_total_records (GumTraceRecorder * self)
{
  return g_atomic_int_get ((volatile gint *) &self->total_records);
}

guint64
gum_trace_recorder_get_dropped_records (GumTraceRecorder * self)
{
  return g_atomic_int_get ((volatile gint *) &self->dropped_records);
}

void
gum_trace_recorder_flush (GumTraceRecorder * self)
{
  gum_trace_flush_buffer (self);
}
