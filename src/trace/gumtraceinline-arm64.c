/*
 * GumTraceInline ARM64 - Inline code generation for trace recording.
 *
 * Generates ARM64 code at block-compile time. The generated code saves all
 * caller-saved registers (integer + NEON/FP) before calling the C recording
 * function, ensuring the traced code's register state is fully preserved.
 *
 * ARM64 ABI caller-saved (volatile) registers:
 *   Integer: X0-X18, LR (X30), NZCV flags
 *   NEON/FP: V0-V7 (params), V16-V31 (scratch)
 *            V8-V15 upper 64 bits also volatile
 *   (X19-X28 callee-saved; D8-D15 lower 64 bits callee-saved)
 *
 * We save ALL 32 Q registers for maximum safety, matching Stalker's
 * PROLOG_FULL approach. This ensures correctness even when the traced
 * code uses NEON/SIMD operations on any V register.
 *
 * Stack frame: 688 bytes (16-byte aligned):
 *
 *   --- Integer registers (176 bytes) ---
 *   [SP+  0]: X16, X17
 *   [SP+ 16]: X0,  X1
 *   [SP+ 32]: X2,  X3
 *   [SP+ 48]: X4,  X5
 *   [SP+ 64]: X6,  X7
 *   [SP+ 80]: X8,  X9
 *   [SP+ 96]: X10, X11
 *   [SP+112]: X12, X13
 *   [SP+128]: X14, X15
 *   [SP+144]: X18, LR
 *   [SP+160]: NZCV
 *   --- NEON/FP registers (512 bytes) ---
 *   [SP+176]: Q0,  Q1
 *   [SP+208]: Q2,  Q3
 *   [SP+240]: Q4,  Q5
 *   [SP+272]: Q6,  Q7
 *   [SP+304]: Q8,  Q9
 *   [SP+336]: Q10, Q11
 *   [SP+368]: Q12, Q13
 *   [SP+400]: Q14, Q15
 *   [SP+432]: Q16, Q17
 *   [SP+464]: Q18, Q19
 *   [SP+496]: Q20, Q21
 *   [SP+528]: Q22, Q23
 *   [SP+560]: Q24, Q25
 *   [SP+592]: Q26, Q27
 *   [SP+624]: Q28, Q29
 *   [SP+656]: Q30, Q31
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gumtraceinline-arm64.h"

#define TRACE_FRAME_SIZE 688

static void
gum_trace_inline_write_micro_prolog (GumArm64Writer * cw)
{
  /* Allocate frame (sub sp, sp, #688) + save X16, X17 */
  gum_arm64_writer_put_sub_reg_reg_imm (cw,
      ARM64_REG_SP, ARM64_REG_SP, TRACE_FRAME_SIZE);
  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X16, ARM64_REG_X17,
      ARM64_REG_SP, 0, GUM_INDEX_SIGNED_OFFSET);

  /* Save all caller-saved integer registers */
  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X0, ARM64_REG_X1,
      ARM64_REG_SP, 16, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X2, ARM64_REG_X3,
      ARM64_REG_SP, 32, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X4, ARM64_REG_X5,
      ARM64_REG_SP, 48, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X6, ARM64_REG_X7,
      ARM64_REG_SP, 64, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X8, ARM64_REG_X9,
      ARM64_REG_SP, 80, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X10, ARM64_REG_X11,
      ARM64_REG_SP, 96, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X12, ARM64_REG_X13,
      ARM64_REG_SP, 112, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X14, ARM64_REG_X15,
      ARM64_REG_SP, 128, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X18, ARM64_REG_LR,
      ARM64_REG_SP, 144, GUM_INDEX_SIGNED_OFFSET);

  /* Save NZCV condition flags */
  gum_arm64_writer_put_mov_reg_nzcv (cw, ARM64_REG_X17);
  gum_arm64_writer_put_str_reg_reg_offset (cw,
      ARM64_REG_X17, ARM64_REG_SP, 160);

  /* Save all NEON/FP registers (Q0-Q31, 128-bit each) */
  {
    gint i;
    for (i = 0; i < 32; i += 2)
    {
      gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
          ARM64_REG_Q0 + i, ARM64_REG_Q0 + i + 1,
          ARM64_REG_SP, 176 + i * 16, GUM_INDEX_SIGNED_OFFSET);
    }
  }
}

static void
gum_trace_inline_write_micro_epilog (GumArm64Writer * cw)
{
  /* Restore all NEON/FP registers (Q0-Q31) */
  {
    gint i;
    for (i = 0; i < 32; i += 2)
    {
      gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
          ARM64_REG_Q0 + i, ARM64_REG_Q0 + i + 1,
          ARM64_REG_SP, 176 + i * 16, GUM_INDEX_SIGNED_OFFSET);
    }
  }

  /* Restore NZCV */
  gum_arm64_writer_put_ldr_reg_reg_offset (cw,
      ARM64_REG_X17, ARM64_REG_SP, 160);
  gum_arm64_writer_put_mov_nzcv_reg (cw, ARM64_REG_X17);

  /* Restore all caller-saved integer registers (reverse order) */
  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X18, ARM64_REG_LR,
      ARM64_REG_SP, 144, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X14, ARM64_REG_X15,
      ARM64_REG_SP, 128, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X12, ARM64_REG_X13,
      ARM64_REG_SP, 112, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X10, ARM64_REG_X11,
      ARM64_REG_SP, 96, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X8, ARM64_REG_X9,
      ARM64_REG_SP, 80, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X6, ARM64_REG_X7,
      ARM64_REG_SP, 64, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X4, ARM64_REG_X5,
      ARM64_REG_SP, 48, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X2, ARM64_REG_X3,
      ARM64_REG_SP, 32, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X0, ARM64_REG_X1,
      ARM64_REG_SP, 16, GUM_INDEX_SIGNED_OFFSET);

  /* Restore X16, X17 + deallocate frame (add sp, sp, #688) */
  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X16, ARM64_REG_X17,
      ARM64_REG_SP, 0, GUM_INDEX_SIGNED_OFFSET);
  gum_arm64_writer_put_add_reg_reg_imm (cw,
      ARM64_REG_SP, ARM64_REG_SP, TRACE_FRAME_SIZE);
}

/* --- Public API --- */

void
gum_trace_inline_emit_block (GumArm64Writer * cw,
                             GumTraceRecorder * recorder,
                             gpointer           block_start,
                             gpointer           block_end)
{
  gum_trace_inline_write_micro_prolog (cw);

  gum_arm64_writer_put_call_address_with_arguments (cw,
      GUM_ADDRESS (gum_trace_recorder_record_block), 3,
      GUM_ARG_ADDRESS, GUM_ADDRESS (recorder),
      GUM_ARG_ADDRESS, GUM_ADDRESS (block_start),
      GUM_ARG_ADDRESS, GUM_ADDRESS (block_end));

  gum_trace_inline_write_micro_epilog (cw);
}

void
gum_trace_inline_emit_exec (GumArm64Writer * cw,
                            GumTraceRecorder * recorder,
                            gpointer           insn_addr)
{
  gum_trace_inline_write_micro_prolog (cw);

  gum_arm64_writer_put_call_address_with_arguments (cw,
      GUM_ADDRESS (gum_trace_recorder_record_exec), 2,
      GUM_ARG_ADDRESS, GUM_ADDRESS (recorder),
      GUM_ARG_ADDRESS, GUM_ADDRESS (insn_addr));

  gum_trace_inline_write_micro_epilog (cw);
}

void
gum_trace_inline_emit_call (GumArm64Writer * cw,
                            GumTraceRecorder * recorder,
                            gpointer           call_site)
{
  gum_trace_inline_write_micro_prolog (cw);

  /* Load original X0, X1 from stack (saved by prolog at [SP+16], [SP+24]) */
  gum_arm64_writer_put_ldr_reg_reg_offset (cw,
      ARM64_REG_X2, ARM64_REG_SP, 16);
  gum_arm64_writer_put_ldr_reg_reg_offset (cw,
      ARM64_REG_X3, ARM64_REG_SP, 24);

  /* record_call_ex(recorder, call_site, orig_x0, orig_x1) */
  gum_arm64_writer_put_call_address_with_arguments (cw,
      GUM_ADDRESS (gum_trace_recorder_record_call_ex), 4,
      GUM_ARG_ADDRESS, GUM_ADDRESS (recorder),
      GUM_ARG_ADDRESS, GUM_ADDRESS (call_site),
      GUM_ARG_REGISTER, ARM64_REG_X2,
      GUM_ARG_REGISTER, ARM64_REG_X3);

  gum_trace_inline_write_micro_epilog (cw);
}

void
gum_trace_inline_emit_ret (GumArm64Writer * cw,
                           GumTraceRecorder * recorder,
                           gpointer           ret_addr)
{
  gum_trace_inline_write_micro_prolog (cw);

  gum_arm64_writer_put_call_address_with_arguments (cw,
      GUM_ADDRESS (gum_trace_recorder_record_ret), 2,
      GUM_ARG_ADDRESS, GUM_ADDRESS (recorder),
      GUM_ARG_ADDRESS, GUM_ADDRESS (ret_addr));

  gum_trace_inline_write_micro_epilog (cw);
}
