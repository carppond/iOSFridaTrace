/*
 * test_trace.c - Verify FridaTrace library on macOS.
 *
 * Traces itself, then converts the binary trace to a readable .log file
 * with ARM64 disassembly, symbol names (dladdr), and ObjC method resolution.
 */

#include "gum.h"
#include "gumtrace.h"
#include "gumtracerecorder.h"
#include <capstone.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

/* --- Trace file structures --- */

#define TRACE_MAGIC 0x46524954

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t record_size;
  uint32_t _padding;
  uint64_t start_time;
  char     module_name[256];
  uint64_t range_base;
  uint64_t range_size;
} TraceFileHeader;

typedef struct {
  uint8_t  type;
  uint8_t  _reserved[3];
  uint32_t thread_id;
  uint64_t location;
  uint64_t target;
  uint64_t timestamp;
} TraceRecord;

static const char *
trace_type_str (uint8_t type)
{
  switch (type)
  {
    case 0: return "EXEC";
    case 1: return "CALL";
    case 2: return "RET";
    case 3: return "BLOCK";
    case 4: return "ARGS";
    default: return "???";
  }
}

/* --- ObjC runtime function pointers (loaded via dlsym) --- */

typedef void * (* ObjcGetClassFunc) (void *);
typedef const char * (* ObjcClassNameFunc) (void *);
typedef const char * (* ObjcSelNameFunc) (void *);

static ObjcGetClassFunc  fn_object_getClass;
static ObjcClassNameFunc fn_class_getName;
static ObjcSelNameFunc   fn_sel_getName;

static void
init_objc_runtime (void)
{
  fn_object_getClass = (ObjcGetClassFunc) dlsym (RTLD_DEFAULT,
      "object_getClass");
  fn_class_getName = (ObjcClassNameFunc) dlsym (RTLD_DEFAULT,
      "class_getName");
  fn_sel_getName = (ObjcSelNameFunc) dlsym (RTLD_DEFAULT,
      "sel_getName");
}

static const char *
resolve_symbol (uint64_t addr, char * buf, size_t buf_size)
{
  Dl_info info;
  if (addr != 0 && dladdr ((void *)(uintptr_t) addr, &info) &&
      info.dli_sname != NULL)
  {
    snprintf (buf, buf_size, "%s", info.dli_sname);
    return buf;
  }
  return NULL;
}

static void
resolve_objc_call (uint64_t x0, uint64_t x1, char * buf, size_t buf_size)
{
  buf[0] = '\0';

  if (fn_object_getClass == NULL || fn_class_getName == NULL ||
      fn_sel_getName == NULL)
    return;

  if (x0 == 0 || x1 == 0)
    return;

  /* sel_getName is safe (selectors are interned, never freed) */
  const char *sel = fn_sel_getName ((void *)(uintptr_t) x1);
  if (sel == NULL)
    return;

  /* object_getClass: try to get class name, be defensive */
  void *cls = fn_object_getClass ((void *)(uintptr_t) x0);
  if (cls != NULL)
  {
    const char *cls_name = fn_class_getName (cls);
    if (cls_name != NULL)
      snprintf (buf, buf_size, "-[%s %s]", cls_name, sel);
  }
}

/* --- Convert binary trace → readable .log --- */

static void
convert_trace_to_log (const char * bin_path, const char * log_path)
{
  FILE *fp_in, *fp_out;
  TraceFileHeader hdr;
  TraceRecord rec;
  uint64_t count = 0;
  uint64_t type_counts[5] = {0};
  csh cs_handle;
  cs_insn *insn = NULL;

  fp_in = fopen (bin_path, "rb");
  if (fp_in == NULL)
  {
    fprintf (stderr, "  Cannot open trace file: %s\n", bin_path);
    return;
  }

  if (fread (&hdr, sizeof (hdr), 1, fp_in) != 1 || hdr.magic != TRACE_MAGIC)
  {
    fprintf (stderr, "  Invalid trace file\n");
    fclose (fp_in);
    return;
  }

  fp_out = fopen (log_path, "w");
  if (fp_out == NULL)
  {
    fprintf (stderr, "  Cannot create log file: %s\n", log_path);
    fclose (fp_in);
    return;
  }

  if (cs_open (CS_ARCH_ARM64, CS_MODE_ARM, &cs_handle) != CS_ERR_OK)
  {
    fprintf (stderr, "  Failed to init Capstone\n");
    fclose (fp_in);
    fclose (fp_out);
    return;
  }
  cs_option (cs_handle, CS_OPT_DETAIL, CS_OPT_ON);

  init_objc_runtime ();

  /* State for CALL + CALL_ARGS pairing */
  int pending_call = 0;
  uint64_t pending_loc = 0;
  char pending_disasm[64] = "";
  uint32_t pending_tid = 0;

  while (fread (&rec, sizeof (rec), 1, fp_in) == 1)
  {
    if (rec.type < 5)
      type_counts[rec.type]++;
    count++;

    if (rec.type == 3) /* BLOCK */
    {
      /* Flush any pending CALL without ARGS */
      if (pending_call)
      {
        char sym_buf[256];
        const char *sym = resolve_symbol (0, sym_buf, sizeof (sym_buf));
        fprintf (fp_out, "[ CALL] tid=%-4u  0x%012llx: %-35s\n",
            pending_tid,
            (unsigned long long) pending_loc,
            pending_disasm);
        pending_call = 0;
      }

      fprintf (fp_out, "[BLOCK] tid=%-4u  0x%llx ~ 0x%llx\n",
          rec.thread_id,
          (unsigned long long) rec.location,
          (unsigned long long) rec.target);
      continue;
    }

    if (rec.type == 4) /* CALL_ARGS: companion to previous CALL */
    {
      if (pending_call)
      {
        char sym_buf[256];
        char objc_buf[512];
        uint64_t x0 = rec.location;
        uint64_t x1 = rec.target;

        /* Try dladdr on the call target (from disasm) */
        /* Use x0 location from pending_loc to disasm and find BL target */
        uint8_t code[4];
        uint64_t call_target = 0;
        memcpy (code, (void *)(uintptr_t) pending_loc, 4);
        cs_insn *call_insn = NULL;
        size_t n = cs_disasm (cs_handle, code, 4, pending_loc, 1, &call_insn);
        if (n > 0)
        {
          /* For BL, extract immediate target from operand */
          if (call_insn->id == ARM64_INS_BL &&
              call_insn->detail->arm64.op_count > 0)
          {
            call_target = (uint64_t)
                call_insn->detail->arm64.operands[0].imm;
          }
          cs_free (call_insn, n);
        }

        const char *sym = resolve_symbol (call_target, sym_buf,
            sizeof (sym_buf));

        /* Check if this is objc_msgSend */
        objc_buf[0] = '\0';
        if (sym != NULL && strstr (sym, "objc_msgSend") != NULL)
          resolve_objc_call (x0, x1, objc_buf, sizeof (objc_buf));

        if (sym != NULL && objc_buf[0] != '\0')
        {
          fprintf (fp_out,
              "[ CALL] tid=%-4u  0x%012llx: %-35s -> %s  %s\n",
              pending_tid,
              (unsigned long long) pending_loc,
              pending_disasm, sym, objc_buf);
        }
        else if (sym != NULL)
        {
          fprintf (fp_out,
              "[ CALL] tid=%-4u  0x%012llx: %-35s -> %s\n",
              pending_tid,
              (unsigned long long) pending_loc,
              pending_disasm, sym);
        }
        else
        {
          fprintf (fp_out,
              "[ CALL] tid=%-4u  0x%012llx: %s\n",
              pending_tid,
              (unsigned long long) pending_loc,
              pending_disasm);
        }

        pending_call = 0;
      }
      continue;
    }

    /* Flush any pending CALL before processing new record */
    if (pending_call)
    {
      fprintf (fp_out, "[ CALL] tid=%-4u  0x%012llx: %s\n",
          pending_tid,
          (unsigned long long) pending_loc,
          pending_disasm);
      pending_call = 0;
    }

    /* Disassemble instruction */
    char disasm[64] = "???";
    if (rec.location != 0)
    {
      uint8_t code[4];
      memcpy (code, (void *)(uintptr_t) rec.location, 4);

      size_t n = cs_disasm (cs_handle, code, 4, rec.location, 1, &insn);
      if (n > 0)
      {
        snprintf (disasm, sizeof (disasm), "%s %s",
            insn->mnemonic, insn->op_str);
        cs_free (insn, n);
        insn = NULL;
      }
    }

    if (rec.type == 1) /* CALL - defer until we see CALL_ARGS */
    {
      pending_call = 1;
      pending_loc = rec.location;
      pending_tid = rec.thread_id;
      strncpy (pending_disasm, disasm, sizeof (pending_disasm) - 1);
      pending_disasm[sizeof (pending_disasm) - 1] = '\0';
      continue;
    }

    if (rec.type == 2) /* RET */
    {
      char sym_buf[256];
      const char *sym = resolve_symbol (rec.target, sym_buf,
          sizeof (sym_buf));

      if (sym != NULL)
      {
        fprintf (fp_out,
            "[  RET] tid=%-4u  0x%012llx: %-35s -> %s\n",
            rec.thread_id,
            (unsigned long long) rec.location,
            disasm, sym);
      }
      else
      {
        fprintf (fp_out,
            "[  RET] tid=%-4u  0x%012llx: %-35s -> 0x%llx\n",
            rec.thread_id,
            (unsigned long long) rec.location,
            disasm,
            (unsigned long long) rec.target);
      }
      continue;
    }

    /* EXEC */
    fprintf (fp_out, "[ EXEC] tid=%-4u  0x%012llx: %s\n",
        rec.thread_id,
        (unsigned long long) rec.location,
        disasm);
  }

  /* Flush final pending CALL */
  if (pending_call)
  {
    fprintf (fp_out, "[ CALL] tid=%-4u  0x%012llx: %s\n",
        pending_tid,
        (unsigned long long) pending_loc,
        pending_disasm);
  }

  cs_close (&cs_handle);
  fclose (fp_in);
  fclose (fp_out);

  printf ("\n=== Trace Statistics ===\n");
  printf ("  Total:  %llu\n", (unsigned long long) count);
  printf ("  EXEC:   %llu\n", (unsigned long long) type_counts[0]);
  printf ("  CALL:   %llu\n", (unsigned long long) type_counts[1]);
  printf ("  RET:    %llu\n", (unsigned long long) type_counts[2]);
  printf ("  BLOCK:  %llu\n", (unsigned long long) type_counts[3]);
  printf ("========================\n");
  printf ("  Log: %s\n", log_path);
}

/* --- Test workloads --- */

static int fibonacci (int n)
{
  if (n <= 1) return n;
  return fibonacci (n - 1) + fibonacci (n - 2);
}

static void do_string_work (void)
{
  char buf[256];
  for (int i = 0; i < 10; i++)
  {
    snprintf (buf, sizeof (buf), "iteration %d", i);
    strlen (buf);
  }
}

static void do_math_work (void)
{
  volatile double result = 1.0;
  for (int i = 1; i <= 100; i++)
    result *= 1.01;
}

/* --- Main --- */

int main (int argc, char * argv[])
{
  const char *bin_path = "tmp/fridatrace_test.bin";
  const char *log_path = "tmp/fridatrace_test.log";
  GumTraceSession *session;

  printf ("=== FridaTrace Test ===\n\n");

  printf ("[1] Starting trace...\n");
  session = gum_trace_start (NULL, 0, 0, bin_path);
  if (session == NULL)
  {
    fprintf (stderr, "ERROR: Failed to start trace!\n");
    return 1;
  }
  printf ("    Trace started.\n\n");

  printf ("[2] Running workloads...\n");
  printf ("    fibonacci(15) = %d\n", fibonacci (15));
  do_string_work ();
  printf ("    String work done.\n");
  do_math_work ();
  printf ("    Math work done.\n");
  printf ("    fibonacci(20) = %d\n", fibonacci (20));

  printf ("\n[3] Stopping trace...\n");
  guint64 total   = gum_trace_get_record_count (session);
  guint64 dropped = gum_trace_get_dropped_count (session);
  gum_trace_stop (session);
  printf ("    Done. records=%llu dropped=%llu\n",
      (unsigned long long) total, (unsigned long long) dropped);

  printf ("\n[4] Converting trace to log...\n");
  convert_trace_to_log (bin_path, log_path);

  remove (bin_path);

  return 0;
}
