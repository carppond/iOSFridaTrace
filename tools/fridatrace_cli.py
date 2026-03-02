#!/usr/bin/env python3
"""
fridatrace_cli.py - FridaTrace command-line controller.

Connect to an iOS app via USB and start instruction-level tracing.

Usage:
    python3 fridatrace_cli.py -n AppName -m ModuleName
    python3 fridatrace_cli.py -n AppName -m ModuleName -s 0x1000 -e 0x5000
    python3 fridatrace_cli.py -p 1234 -m ModuleName
    python3 fridatrace_cli.py -f com.example.app -m ModuleName

Examples:
    # Trace entire module in running app:
    python3 fridatrace_cli.py -n Safari -m WebKit

    # Trace specific range:
    python3 fridatrace_cli.py -n MyApp -m MyApp -s 0x4000 -e 0x8000

    # Spawn and trace:
    python3 fridatrace_cli.py -f com.example.app -m MyApp --spawn

    # Save output:
    python3 fridatrace_cli.py -n MyApp -m MyApp -o trace.log
"""

import argparse
import os
import signal
import sys
import time

try:
    import frida
except ImportError:
    print("Error: frida not installed. Run: pip3 install frida-tools")
    sys.exit(1)


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
JS_PATH = os.path.join(SCRIPT_DIR, "fridatrace.js")


def on_message(message, data):
    if message["type"] == "send":
        print(f"[device] {message['payload']}")
    elif message["type"] == "error":
        print(f"[error] {message['stack']}")


def main():
    parser = argparse.ArgumentParser(
        description="FridaTrace - iOS instruction-level tracing via Frida")

    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("-n", "--name", help="App name to attach to")
    group.add_argument("-p", "--pid", type=int, help="Process ID to attach to")
    group.add_argument("-f", "--spawn", help="Bundle ID to spawn")

    parser.add_argument("-m", "--module", help="Module name to trace")
    parser.add_argument("-s", "--start", help="Start offset (hex, e.g. 0x1000)")
    parser.add_argument("-e", "--end", help="End offset (hex, e.g. 0x5000)")
    parser.add_argument("-o", "--output", help="Output log file path on device")
    parser.add_argument("-t", "--time", type=int, default=0,
                        help="Trace duration in seconds (0 = until Ctrl+C)")
    parser.add_argument("--usb", action="store_true", default=True,
                        help="Use USB connection (default)")
    parser.add_argument("--list-modules", action="store_true",
                        help="List modules and exit")

    args = parser.parse_args()

    # Connect to device
    print("[*] Connecting to device...")
    device = frida.get_usb_device(timeout=10)
    print(f"[*] Device: {device.name}")

    # Attach or spawn
    if args.spawn:
        print(f"[*] Spawning {args.spawn}...")
        pid = device.spawn([args.spawn])
        session = device.attach(pid)
        device.resume(pid)
        print(f"[*] Spawned and attached to PID {pid}")
    elif args.pid:
        session = device.attach(args.pid)
        print(f"[*] Attached to PID {args.pid}")
    else:
        session = device.attach(args.name)
        print(f"[*] Attached to {args.name}")

    # Load script
    with open(JS_PATH, "r") as f:
        js_code = f.read()

    script = session.create_script(js_code)
    script.on("message", on_message)
    script.load()
    api = script.exports_sync

    # List modules mode
    if args.list_modules:
        modules = api.list_modules()
        print(f"\n{'Base':<20} {'Size':<12} Name")
        print("─" * 60)
        for m in modules:
            print(f"{m['base']:<20} 0x{m['size']:08x}   {m['name']}")
        session.detach()
        return

    # Start tracing
    if args.module:
        start_off = int(args.start, 16) if args.start else None
        end_off = int(args.end, 16) if args.end else None

        if start_off is not None and end_off is not None:
            api.trace(args.module, start_off, end_off)
        elif start_off is not None:
            api.trace(args.module, start_off)
        else:
            api.trace(args.module)
    else:
        print("[!] No module specified. Use --list-modules to see available modules.")
        print("[!] Then use -m <module> to start tracing.")
        # Enter interactive mode
        print("[*] Entering interactive mode. Use Ctrl+C to exit.")
        try:
            sys.stdin.read()
        except KeyboardInterrupt:
            pass
        session.detach()
        return

    # Wait for tracing
    print(f"[*] Tracing... {'Ctrl+C to stop' if args.time == 0 else f'{args.time}s'}")

    def handle_sigint(sig, frame):
        pass

    signal.signal(signal.SIGINT, handle_sigint)

    try:
        if args.time > 0:
            time.sleep(args.time)
        else:
            signal.pause()
    except:
        pass

    # Stop and collect
    print("\n[*] Stopping trace...")
    api.stop()

    stats = api.stats()
    print(f"[*] Results: total={stats['total']} call={stats['call']} "
          f"ret={stats['ret']} block={stats['block']}")

    # Save if requested
    if args.output:
        api.save(args.output)
    else:
        # Show last records
        records = api.records(30)
        print(f"\n[*] Last {len(records)} records:")
        for r in records:
            t = r.get("type", "?")
            addr0 = r.get("0", "")
            addr1 = r.get("1", "")
            if t in ("call", "ret"):
                print(f"  [{t:>4}] {addr0} -> {addr1}")
            else:
                print(f"  [{t:>4}] {addr0}")

    session.detach()
    print("[*] Done.")


if __name__ == "__main__":
    main()
