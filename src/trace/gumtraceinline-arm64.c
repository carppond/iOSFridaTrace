/*
 * GumTraceInline ARM64 - Minimal inline code generation for trace recording.
 *
 * Generates ARM64 code at block-compile time. The generated code uses a
 * "micro-prolog" that saves only the registers we clobber (~64 bytes of
 * stack), vs GUM_PROLOG_FULL which saves everything (~1256 bytes).
 *
 * Generated sequence per trace point (~15 ARM64 instructions):
 *
 *   ; --- micro prolog (6 insns) ---
 *   stp  x16, x17, [sp, #-64]!    ; save scratch + allocate frame
 *   stp  x0, x1, [sp, #16]        ; save arg regs
 *   stp  x2, lr, [sp, #32]        ; save x2 + link register
 *   mrs  x17, nzcv                 ; read condition flags
 *   str  x17, [sp, #48]           ; store NZCV to frame
 *
 *   ; --- call recording function (~4 insns via put_call_address) ---
 *   ldr  x0, =recorder            ; arg0 (literal pool)
 *   ldr  x1, =address             ; arg1 (literal pool)
 *   ldr  x16, =func               ; target (literal pool)
 *   blr  x16                       ; call
 *
 *   ; --- micro epilog (6 insns) ---
 *   ldr  x17, [sp, #48]           ; load NZCV
 *   msr  nzcv, x17                 ; restore condition flags
 *   ldp  x2, lr, [sp, #32]        ; restore x2 + lr
 *   ldp  x0, x1, [sp, #16]        ; restore arg regs
 *   ldp  x16, x17, [sp], #64      ; restore scratch + deallocate
 *
 * Registers saved: X0, X1, X2, X16, X17, LR, NZCV (64 bytes stack)
 * vs PROLOG_FULL:   X0-X28, Q0-Q31, LR, NZCV       (~1256 bytes stack)
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gumtraceinline-arm64.h"

/*
 * Micro-prolog: 64-byte stack frame, 16-byte aligned.
 *
 * Frame layout:
 *   [SP+48]: NZCV (stored as 64-bit)
 *   [SP+32]: X2,  LR
 *   [SP+16]: X0,  X1
 *   [SP+ 0]: X16, X17
 *   (SP+56..63 unused padding for alignment)
 */
static void
gum_trace_inline_write_micro_prolog (GumArm64Writer * cw)
{
  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X16, ARM64_REG_X17,
      ARM64_REG_SP, -64, GUM_INDEX_PRE_ADJUST);

  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X0, ARM64_REG_X1,
      ARM64_REG_SP, 16, GUM_INDEX_SIGNED_OFFSET);

  gum_arm64_writer_put_stp_reg_reg_reg_offset (cw,
      ARM64_REG_X2, ARM64_REG_LR,
      ARM64_REG_SP, 32, GUM_INDEX_SIGNED_OFFSET);

  gum_arm64_writer_put_mov_reg_nzcv (cw, ARM64_REG_X17);

  gum_arm64_writer_put_str_reg_reg_offset (cw,
      ARM64_REG_X17, ARM64_REG_SP, 48);
}

/*
 * Micro-epilog: restore everything in reverse order.
 */
static void
gum_trace_inline_write_micro_epilog (GumArm64Writer * cw)
{
  gum_arm64_writer_put_ldr_reg_reg_offset (cw,
      ARM64_REG_X17, ARM64_REG_SP, 48);

  gum_arm64_writer_put_mov_nzcv_reg (cw, ARM64_REG_X17);

  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X2, ARM64_REG_LR,
      ARM64_REG_SP, 32, GUM_INDEX_SIGNED_OFFSET);

  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X0, ARM64_REG_X1,
      ARM64_REG_SP, 16, GUM_INDEX_SIGNED_OFFSET);

  gum_arm64_writer_put_ldp_reg_reg_reg_offset (cw,
      ARM64_REG_X16, ARM64_REG_X17,
      ARM64_REG_SP, 64, GUM_INDEX_POST_ADJUST);
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
