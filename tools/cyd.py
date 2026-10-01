#!/usr/bin/env python3
"""Drive the ObCYDian serial debug console.

Examples:
  tools/cyd.py kbd              # type on the device from this terminal (Ctrl+] quits)
  tools/cyd.py "open /Welcome.md" shot:welcome.png
  tools/cyd.py "tap 100 80" wait:1 shot:after.png
  tools/cyd.py log:5            # just print serial output for 5 s

Arguments run in order: plain strings are sent as console commands,
"shot:FILE.png" saves a screenshot, "wait:SECONDS" pauses while echoing output,
"log:SECONDS" is the same as wait.
"""
import sys
import time

import serial
from PIL import Image

PORT = "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
BAUD = 921600


def open_port():
    s = serial.Serial()
    s.port = PORT
    s.baudrate = BAUD
    s.timeout = 0.2
    # Leave DTR and RTS both asserted: the auto-reset circuit only pulls EN low when
    # RTS is asserted without DTR, so toggling either one individually resets the board.
    s.open()
    return s


def drain(s, seconds):
    end = time.time() + seconds
    while time.time() < end:
        d = s.read(4096)
        if d:
            sys.stdout.write(d.decode(errors="replace"))
            sys.stdout.flush()


def shot(s, path):
    s.reset_input_buffer()
    s.write(b"shot\n")
    buf = b""
    end = time.time() + 5
    while b"SHOT " not in buf or b"\n" not in buf[buf.index(b"SHOT "):]:
        buf += s.read(256)
        if time.time() > end:
            raise SystemExit("no screenshot header received")
    start = buf.index(b"SHOT ")
    header_end = buf.index(b"\n", start)
    w, h = map(int, buf[start + 5:header_end].split())
    data = buf[header_end + 1:]
    need = w * h * 3
    s.timeout = 10
    while len(data) < need:
        chunk = s.read(need - len(data))
        if not chunk:
            raise SystemExit(f"screenshot truncated ({len(data)}/{need} bytes)")
        data += chunk
    s.timeout = 0.2
    Image.frombytes("RGB", (w, h), data[:need]).save(path)
    print(f"saved {path} ({w}x{h})")


def keyboard(s):
    """Forward this terminal's keystrokes to the device until Ctrl+]."""
    import os
    import select
    import termios
    import tty

    s.write(b"kbd\n")
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    print("Typing on the CYD. Ctrl+] to quit.\r")
    try:
        tty.setraw(fd)
        while True:
            r, _, _ = select.select([fd, s.fileno()], [], [], 0.05)
            if fd in r:
                data = os.read(fd, 64)
                s.write(data)
                if b"\x1d" in data:
                    break
            if s.fileno() in r:
                out = s.read(4096).decode(errors="replace").replace("\n", "\r\n")
                sys.stdout.write(out)
                sys.stdout.flush()
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        print()


def main():
    s = open_port()
    time.sleep(0.1)
    for arg in sys.argv[1:]:
        if arg == "kbd":
            keyboard(s)
        elif arg.startswith("shot:"):
            shot(s, arg[5:])
        elif arg.startswith(("wait:", "log:")):
            drain(s, float(arg.split(":", 1)[1]))
        else:
            s.write((arg + "\n").encode())
            drain(s, 0.6)


if __name__ == "__main__":
    main()
