/*
 * fridatrace_inject.c - Dylib for injecting FridaTrace into any iOS process.
 *
 * Build as a dynamic library, then inject via:
 *   - DYLD_INSERT_LIBRARIES (jailbroken device)
 *   - MobileSubstrate / ElleKit MobileLoader
 *
 * The trace starts automatically when the dylib is loaded (constructor),
 * and stops when the process exits (destructor / atexit).
 *
 * Trace output is written to /var/mobile/Documents/fridatrace/.
 *
 * Environment variables:
 *   FRIDATRACE_MODULE  - Module name to trace (default: main executable)
 *   FRIDATRACE_OUTPUT  - Output directory (default: /var/mobile/Documents/fridatrace)
 *   FRIDATRACE_DISABLE - Set to "1" to skip tracing
 */

#include "gum/gum.h"
#include "gum/trace/gumtrace.h"
#include "gum/trace/gumtracerecorder.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <mach-o/dyld.h>
#include <libgen.h>

static GumTraceSession *g_session = NULL;
static char g_trace_path[1024] = {0};

static const char *
get_executable_name (void)
{
  const char *path = _dyld_get_image_name (0);
  if (path == NULL)
    return "unknown";
  const char *name = strrchr (path, '/');
  return (name != NULL) ? name + 1 : path;
}

static void
trace_cleanup (void)
{
  if (g_session == NULL)
    return;

  guint64 total   = gum_trace_get_record_count (g_session);
  guint64 dropped = gum_trace_get_dropped_count (g_session);

  gum_trace_stop (g_session);
  g_session = NULL;

  fprintf (stderr, "[FridaTrace] Stopped. records=%llu dropped=%llu path=%s\n",
      (unsigned long long) total, (unsigned long long) dropped, g_trace_path);
}

__attribute__((constructor))
static void
fridatrace_init (void)
{
  /* Check if tracing is disabled */
  const char *disable = getenv ("FRIDATRACE_DISABLE");
  if (disable != NULL && strcmp (disable, "1") == 0)
    return;

  /* Determine module to trace */
  const char *module = getenv ("FRIDATRACE_MODULE");
  if (module == NULL || module[0] == '\0')
    module = get_executable_name ();

  /* Determine output directory */
  const char *output_dir = getenv ("FRIDATRACE_OUTPUT");
  if (output_dir == NULL || output_dir[0] == '\0')
    output_dir = "/var/mobile/Documents/fridatrace";

  /* Create output directory */
  mkdir (output_dir, 0755);

  /* Generate output filename with timestamp */
  time_t now = time (NULL);
  struct tm *tm = localtime (&now);
  snprintf (g_trace_path, sizeof (g_trace_path),
      "%s/%s_%04d%02d%02d_%02d%02d%02d.bin",
      output_dir, module,
      tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
      tm->tm_hour, tm->tm_min, tm->tm_sec);

  fprintf (stderr, "[FridaTrace] Injected into %s, tracing module: %s\n",
      get_executable_name (), module);
  fprintf (stderr, "[FridaTrace] Output: %s\n", g_trace_path);

  /* Start tracing */
  g_session = gum_trace_start (module, 0, 0, g_trace_path);
  if (g_session == NULL)
  {
    fprintf (stderr, "[FridaTrace] ERROR: Failed to start trace!\n");
    return;
  }

  atexit (trace_cleanup);

  fprintf (stderr, "[FridaTrace] Tracing started.\n");
}

__attribute__((destructor))
static void
fridatrace_fini (void)
{
  trace_cleanup ();
}
