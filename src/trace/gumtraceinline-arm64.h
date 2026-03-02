/*
 * GumTraceInline ARM64 - Inline code generation for trace recording.
 *
 * Generates ARM64 instruction sequences that record trace events directly
 * into the GumTraceRecorder ring buffer. Saves all caller-saved integer
 * registers (X0-X18, LR, NZCV) and all 32 NEON/FP Q registers to ensure
 * complete register state preservation across trace recording calls.
 *
 * Stack frame: 688 bytes (176 integer + 512 NEON/FP)
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __GUM_TRACE_INLINE_ARM64_H__
#define __GUM_TRACE_INLINE_ARM64_H__

#include <gum/arch-arm64/gumarm64writer.h>
#include "gumtracerecorder.h"

G_BEGIN_DECLS

/*
 * Emit inline ARM64 code to record a BLOCK event.
 * Called once at the beginning of each basic block.
 *
 * @cw:       The ARM64 code writer (writes into Stalker's code slab)
 * @recorder: Pointer to the trace recorder instance
 * @block_start: The real start address of this basic block
 * @block_end:   The real end address of this basic block (can be NULL)
 */
void gum_trace_inline_emit_block (GumArm64Writer * cw,
    GumTraceRecorder * recorder, gpointer block_start, gpointer block_end);

/*
 * Emit inline ARM64 code to record an EXEC event.
 * Called for each instruction when full instruction tracing is enabled.
 *
 * @cw:       The ARM64 code writer
 * @recorder: Pointer to the trace recorder instance
 * @insn_addr: The real address of the instruction being traced
 */
void gum_trace_inline_emit_exec (GumArm64Writer * cw,
    GumTraceRecorder * recorder, gpointer insn_addr);

/*
 * Emit inline ARM64 code to record a CALL event.
 * Called before BL/BLR/BLRAA etc. instructions.
 *
 * @cw:        The ARM64 code writer
 * @recorder:  Pointer to the trace recorder instance
 * @call_site: The address of the call instruction
 */
void gum_trace_inline_emit_call (GumArm64Writer * cw,
    GumTraceRecorder * recorder, gpointer call_site);

/*
 * Emit inline ARM64 code to record a RET event.
 * Called before RET/RETAA/RETAB instructions.
 *
 * @cw:       The ARM64 code writer
 * @recorder: Pointer to the trace recorder instance
 * @ret_addr: The address of the return instruction
 */
void gum_trace_inline_emit_ret (GumArm64Writer * cw,
    GumTraceRecorder * recorder, gpointer ret_addr);

G_END_DECLS

#endif
