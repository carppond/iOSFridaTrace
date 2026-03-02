"""
lldb_jit_handler.py - Handle frida-gum's BRK #1337 JIT requests via LLDB.

When frida-gum needs executable memory on non-jailbroken iOS, it emits:
    BRK #1337
with registers:
    x1 = 1337 (magic)
    x2 = 1337 (magic)
    x3 = operation type (3 = page plan)
    x4 = buffer length
    x5 = buffer data pointer

This script catches the EXC_BREAKPOINT, reads the page plan, uses LLDB's
privileged task port to call mach_vm_protect, and resumes execution.

Usage in Xcode:
  1. In Xcode: Debug > Breakpoint Navigator > Create Symbolic Breakpoint
     - Symbol: (leave empty)
     - Or: Add to .lldbinit

  2. In LLDB console:
     (lldb) command script import /path/to/lldb_jit_handler.py

  3. Or add to ~/.lldbinit:
     command script import /path/to/lldb_jit_handler.py
"""

import lldb
import struct


MAGIC = 1337
PAGE_SIZE = 16384  # iOS ARM64 uses 16KB pages
VM_PROT_READ = 1
VM_PROT_EXECUTE = 4
VM_PROT_READ_EXECUTE = VM_PROT_READ | VM_PROT_EXECUTE

g_handler_installed = False
g_handled_count = 0


def read_uint32(process, addr):
    error = lldb.SBError()
    data = process.ReadMemory(addr, 4, error)
    if error.Fail():
        return None
    return struct.unpack('<I', data)[0]


def read_uint64(process, addr):
    error = lldb.SBError()
    data = process.ReadMemory(addr, 8, error)
    if error.Fail():
        return None
    return struct.unpack('<Q', data)[0]


def read_bytes(process, addr, size):
    error = lldb.SBError()
    data = process.ReadMemory(addr, size, error)
    if error.Fail():
        return None
    return data


def get_register(frame, name):
    reg = frame.FindRegister(name)
    if reg.IsValid():
        return reg.GetValueAsUnsigned()
    return None


def set_register(frame, name, value):
    reg = frame.FindRegister(name)
    if reg.IsValid():
        reg.SetValueFromCString(hex(value))
        return True
    return False


def mprotect_via_lldb(process, addr, size, prot):
    """
    Call mach_vm_protect via expression evaluation.
    LLDB's expression evaluator runs in the target but LLDB can also
    use its debug port privileges for memory operations.
    """
    # Try using mach_vm_protect through LLDB expression evaluation
    expr = (
        f'(int)mach_vm_protect('
        f'(unsigned int)mach_task_self(), '
        f'(unsigned long long){addr:#x}, '
        f'(unsigned long long){size:#x}, '
        f'(int)0, '
        f'(int){prot})'
    )

    options = lldb.SBExpressionOptions()
    options.SetTimeoutInMicroSeconds(5000000)  # 5 seconds
    options.SetUnwindOnError(True)

    result = process.GetSelectedThread().GetSelectedFrame().EvaluateExpression(
        expr, options)

    if result.IsValid() and result.GetError().Success():
        ret = result.GetValueAsSigned()
        return ret == 0  # KERN_SUCCESS
    return False


def handle_page_plan(process, frame, buf_addr, buf_len):
    """
    Parse the page plan buffer and apply mach_vm_protect for each block.

    Page plan format:
      uint32 n_blocks
      For each block:
        uint64 start_address
        uint32 n_pages
        uint8[] page_bytes (n_pages bytes, content for each page)
    """
    global g_handled_count

    data = read_bytes(process, buf_addr, buf_len)
    if data is None:
        print("[JIT Handler] ERROR: Failed to read page plan buffer")
        return False

    offset = 0
    n_blocks = struct.unpack_from('<I', data, offset)[0]
    offset += 4

    success = True
    for i in range(n_blocks):
        if offset + 12 > len(data):
            print(f"[JIT Handler] ERROR: Buffer too short at block {i}")
            return False

        start = struct.unpack_from('<Q', data, offset)[0]
        offset += 8
        n_pages = struct.unpack_from('<I', data, offset)[0]
        offset += 4

        # Skip page content bytes
        offset += n_pages

        size = n_pages * PAGE_SIZE

        if not mprotect_via_lldb(process, start, size, VM_PROT_READ_EXECUTE):
            print(f"[JIT Handler] ERROR: mach_vm_protect failed for "
                  f"0x{start:x} size=0x{size:x}")
            success = False
        else:
            g_handled_count += 1

    return success


def stop_handler(event):
    """
    Called by LLDB when the process stops.
    Check if it's a BRK #1337 and handle it.
    """
    process = lldb.debugger.GetSelectedTarget().GetProcess()
    if not process.IsValid():
        return

    thread = process.GetSelectedThread()
    if not thread.IsValid():
        return

    # Check stop reason
    if thread.GetStopReason() != lldb.eStopReasonException:
        return

    frame = thread.GetFrameAtIndex(0)
    if not frame.IsValid():
        return

    # Read the instruction at PC to verify it's BRK #1337
    pc = frame.GetPC()
    error = lldb.SBError()
    insn_bytes = process.ReadMemory(pc, 4, error)
    if error.Fail():
        return

    insn = struct.unpack('<I', insn_bytes)[0]
    # BRK #1337 encoding: 0xD4207CA0 + (1337 << 5) = 0xD42A7420
    # Actually: BRK #imm16 => 0xD4200000 | (imm16 << 5)
    # BRK #1337 = 0xD4200000 | (1337 << 5) = 0xD4200000 | 0xA720 = 0xD420A720
    expected_brk = 0xD4200000 | (MAGIC << 5)
    if insn != expected_brk:
        return

    # Verify magic registers
    x1 = get_register(frame, 'x1')
    x2 = get_register(frame, 'x2')
    x3 = get_register(frame, 'x3')

    if x1 != MAGIC or x2 != MAGIC:
        return

    x4 = get_register(frame, 'x4')  # buffer length
    x5 = get_register(frame, 'x5')  # buffer data pointer

    if x3 == 3:  # Page plan operation
        success = handle_page_plan(process, frame, x5, x4)
    else:
        print(f"[JIT Handler] Unknown operation type: {x3}")
        success = False

    # Set result register
    if success:
        set_register(frame, 'x0', 0x1337)
    else:
        set_register(frame, 'x0', 0)

    # Skip BRK instruction (advance PC by 4 bytes)
    thread.SetSelectedFrame(0)
    frame.SetPC(pc + 4)

    # Continue execution
    process.Continue()


def install_stop_hook(debugger, command, result, internal_dict):
    """Install the BRK #1337 handler as an LLDB stop hook."""
    global g_handler_installed

    target = debugger.GetSelectedTarget()
    if not target.IsValid():
        result.AppendMessage("[JIT Handler] No target selected")
        return

    # Add a stop hook using a Python function
    # We use process.GetBroadcaster() approach
    debugger.HandleCommand(
        'process handle SIGBUS -n false -p true -s false')
    debugger.HandleCommand(
        'process handle SIGTRAP -n false -p true -s false')

    # Install a stop hook
    debugger.HandleCommand(
        'target stop-hook add -P lldb_jit_handler.StopHook')

    g_handler_installed = True
    result.AppendMessage(
        "[JIT Handler] Installed. BRK #1337 will be handled automatically.")


def status(debugger, command, result, internal_dict):
    """Show handler status."""
    global g_handler_installed, g_handled_count
    result.AppendMessage(
        f"[JIT Handler] Installed: {g_handler_installed}, "
        f"Handled: {g_handled_count} page plans")


class StopHook:
    """LLDB StopHook class for handling BRK #1337."""

    def __init__(self, target, extra_args, internal_dict):
        self.target = target

    def handle_stop(self, exe_ctx, stream):
        """
        Called when the process stops.
        Returns True to let LLDB stop, False to auto-continue.
        """
        thread = exe_ctx.GetThread()
        if not thread.IsValid():
            return True

        if thread.GetStopReason() != lldb.eStopReasonException:
            return True

        frame = thread.GetFrameAtIndex(0)
        if not frame.IsValid():
            return True

        process = exe_ctx.GetProcess()
        pc = frame.GetPC()

        # Read instruction at PC
        error = lldb.SBError()
        insn_bytes = process.ReadMemory(pc, 4, error)
        if error.Fail():
            return True

        insn = struct.unpack('<I', insn_bytes)[0]
        expected_brk = 0xD4200000 | (MAGIC << 5)
        if insn != expected_brk:
            return True  # Not our BRK, let LLDB handle normally

        # Verify magic values
        x1 = get_register(frame, 'x1')
        x2 = get_register(frame, 'x2')
        if x1 != MAGIC or x2 != MAGIC:
            return True

        x3 = get_register(frame, 'x3')
        x4 = get_register(frame, 'x4')
        x5 = get_register(frame, 'x5')

        stream.Print(f"[JIT Handler] Caught BRK #1337: "
                     f"op={x3}, buf_len={x4}, buf=0x{x5:x}\n")

        if x3 == 3:
            success = handle_page_plan(process, frame, x5, x4)
        else:
            stream.Print(f"[JIT Handler] Unknown op type: {x3}\n")
            success = False

        if success:
            set_register(frame, 'x0', 0x1337)
            stream.Print("[JIT Handler] Page plan applied successfully\n")
        else:
            set_register(frame, 'x0', 0)
            stream.Print("[JIT Handler] Page plan FAILED\n")

        # Skip BRK instruction
        frame.SetPC(pc + 4)

        return False  # Auto-continue


def __lldb_init_module(debugger, internal_dict):
    """Called when the module is imported into LLDB."""
    debugger.HandleCommand(
        'command script add -f lldb_jit_handler.install_stop_hook '
        'jit_handler_install')
    debugger.HandleCommand(
        'command script add -f lldb_jit_handler.status '
        'jit_handler_status')

    print("[JIT Handler] Loaded. Use 'jit_handler_install' to activate.")
    print("[JIT Handler] Or it will auto-activate on next run.")

    # Auto-install
    debugger.HandleCommand(
        'target stop-hook add -P lldb_jit_handler.StopHook')
    print("[JIT Handler] Auto-installed stop hook for BRK #1337.")
