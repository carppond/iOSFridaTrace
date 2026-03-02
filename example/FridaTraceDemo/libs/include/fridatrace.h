/*
 * FridaTrace - iOS instruction-level tracing library.
 *
 * Single-header integration file. Include this in your iOS app.
 *
 * Usage:
 *   #include "fridatrace.h"
 *
 *   // Start tracing all instructions in your app:
 *   GumTraceSession *s = gum_trace_start("MyApp", 0, 0, "/tmp/trace.bin");
 *
 *   // ... your app runs ...
 *
 *   // Stop and flush:
 *   gum_trace_stop(s);
 */

#ifndef __FRIDATRACE_H__
#define __FRIDATRACE_H__

#include <gum/gum.h>
#include <gum/gumstalker.h>
#include <gum/trace/gumtrace.h>
#include <gum/trace/gumtracerecorder.h>

#endif
