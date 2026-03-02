/*
 * trace_parser - Parse and display GumTrace binary trace files.
 *
 * Usage: trace_parser <trace_file> [--json] [--filter-type call|ret|exec|block]
 *
 * Reads the binary trace file produced by GumTraceRecorder and outputs
 * human-readable (or JSON) instruction trace data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define GUM_TRACE_FILE_HEADER_MAGIC 0x46524954  /* "FRIT" */

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
type_to_string (uint8_t type)
{
  switch (type)
  {
    case 0: return "EXEC";
    case 1: return "CALL";
    case 2: return "RET";
    case 3: return "BLOCK";
    default: return "UNKNOWN";
  }
}

static void
print_usage (const char * prog)
{
  fprintf (stderr, "Usage: %s <trace_file> [options]\n", prog);
  fprintf (stderr, "Options:\n");
  fprintf (stderr, "  --json              Output in JSON format\n");
  fprintf (stderr, "  --filter-type TYPE  Filter by event type "
                   "(exec|call|ret|block)\n");
  fprintf (stderr, "  --stats             Show statistics only\n");
  fprintf (stderr, "  --limit N           Show first N records\n");
}

int
main (int argc, char * argv[])
{
  FILE * fp;
  TraceFileHeader header;
  TraceRecord record;
  int json_mode = 0;
  int stats_mode = 0;
  int filter_type = -1;
  uint64_t limit = 0;
  uint64_t count = 0;
  uint64_t type_counts[4] = {0};
  int i;

  if (argc < 2)
  {
    print_usage (argv[0]);
    return 1;
  }

  for (i = 2; i < argc; i++)
  {
    if (strcmp (argv[i], "--json") == 0)
      json_mode = 1;
    else if (strcmp (argv[i], "--stats") == 0)
      stats_mode = 1;
    else if (strcmp (argv[i], "--limit") == 0 && i + 1 < argc)
      limit = strtoull (argv[++i], NULL, 10);
    else if (strcmp (argv[i], "--filter-type") == 0 && i + 1 < argc)
    {
      i++;
      if (strcmp (argv[i], "exec") == 0) filter_type = 0;
      else if (strcmp (argv[i], "call") == 0) filter_type = 1;
      else if (strcmp (argv[i], "ret") == 0) filter_type = 2;
      else if (strcmp (argv[i], "block") == 0) filter_type = 3;
    }
  }

  fp = fopen (argv[1], "rb");
  if (fp == NULL)
  {
    fprintf (stderr, "Error: Cannot open %s\n", argv[1]);
    return 1;
  }

  if (fread (&header, sizeof (header), 1, fp) != 1)
  {
    fprintf (stderr, "Error: Cannot read file header\n");
    fclose (fp);
    return 1;
  }

  if (header.magic != GUM_TRACE_FILE_HEADER_MAGIC)
  {
    fprintf (stderr, "Error: Invalid trace file (bad magic: 0x%08x)\n",
        header.magic);
    fclose (fp);
    return 1;
  }

  fprintf (stderr, "=== Trace File Info ===\n");
  fprintf (stderr, "Version:     %u\n", header.version);
  fprintf (stderr, "Module:      %s\n", header.module_name);
  fprintf (stderr, "Range:       0x%llx - 0x%llx (size: %llu)\n",
      (unsigned long long) header.range_base,
      (unsigned long long) (header.range_base + header.range_size),
      (unsigned long long) header.range_size);
  fprintf (stderr, "Record size: %u bytes\n", header.record_size);
  fprintf (stderr, "=======================\n\n");

  if (json_mode && !stats_mode)
    printf ("[\n");

  while (fread (&record, sizeof (record), 1, fp) == 1)
  {
    if (filter_type >= 0 && record.type != filter_type)
    {
      if (record.type < 4)
        type_counts[record.type]++;
      count++;
      continue;
    }

    if (record.type < 4)
      type_counts[record.type]++;

    if (!stats_mode)
    {
      if (limit > 0 && count >= limit)
        break;

      if (json_mode)
      {
        printf ("  %s{\"type\":\"%s\",\"thread\":%u,"
                "\"location\":\"0x%llx\",\"target\":\"0x%llx\","
                "\"timestamp\":%llu}",
            count > 0 ? ",\n" : "",
            type_to_string (record.type),
            record.thread_id,
            (unsigned long long) record.location,
            (unsigned long long) record.target,
            (unsigned long long) record.timestamp);
      }
      else
      {
        printf ("[%5s] tid=%u  loc=0x%012llx  target=0x%012llx  ts=%llu\n",
            type_to_string (record.type),
            record.thread_id,
            (unsigned long long) record.location,
            (unsigned long long) record.target,
            (unsigned long long) record.timestamp);
      }
    }

    count++;
  }

  if (json_mode && !stats_mode)
    printf ("\n]\n");

  if (stats_mode)
  {
    printf ("\n=== Trace Statistics ===\n");
    printf ("Total records: %llu\n", (unsigned long long) count);
    printf ("  EXEC:  %llu\n", (unsigned long long) type_counts[0]);
    printf ("  CALL:  %llu\n", (unsigned long long) type_counts[1]);
    printf ("  RET:   %llu\n", (unsigned long long) type_counts[2]);
    printf ("  BLOCK: %llu\n", (unsigned long long) type_counts[3]);
    printf ("========================\n");
  }

  fclose (fp);
  return 0;
}
