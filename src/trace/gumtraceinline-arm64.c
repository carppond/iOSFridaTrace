/*
 * GumTraceInline ARM64 - Inline code generation for trace recording.
 *
 * Generates ARM64 code at block-compile time. The generated code saves all
 * caller-saved integer registers before calling the C recording function,
 * ensuring the traced code's register state is fully preserved.
 *
 * ARM64 ABI caller-saved (volatile) registers:
 *   X0-X18, LR (X30), NZCV flags
 *   (X19-X28 are callee-saved, safe across function calls)
 *
 * Stack frame: 176 bytes (16-byte aligned), saves 20 regs + NZCV:
 *
 *   [SP+160]: NZCV
 *   [SP+144]: X18, LR
 *   [SP+128]: X14, X15
 *   [SP+112]: X12, X13
 *   [SP+ 96]: X10, X11
 *   [SP+ 80]: X8,  X9
 *   [SP+ 64]: X6,  X7
 *   [SP+ 48]: X4,  X5
 *   [SP+ 32]: X2,  X3
 *   [SP+ 16]: X0,  X1
 *   [SP+  0]: X16, X17
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gumtraceinline-arm64.h"

#define TRACE_FRAME_SIZE 176

static void
gum_trace_inline_write_micro_prolog (GumArm64Writer * cw)
{
  /* Allocate frame + save X16, X17 */
  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X16, ARM64_REG_X17,
      ARM64_REG_SP, -(gint)TRACE_FRAME_SIZE, GUM_INDEX_PRE_ADJUST);

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
}

static void
gum_trace_inline_write_micro_epilog (GumArm64Writer * cw)
{
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

  /* Restore X16, X17 + deallocate frame */
  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X16, ARM64_REG_X17,
      ARM64_REG_SP, TRACE_FRAME_SIZE, GUM_INDEX_POST_ADJUST);
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

  gum_arm64_writer_put_call_address_with_arguments (cw,
      GUM_ADDRESS (gum_trace_recorder_record_call), 2,
      GUM_ARG_ADDRESS, GUM_ADDRESS (recorder),
      GUM_ARG_ADDRESS, GUM_ADDRESS (call_site));

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
