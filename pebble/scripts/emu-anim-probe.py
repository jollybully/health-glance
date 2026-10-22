#!/usr/bin/env python3
"""Probe the deck-push animation: press a button, then rapid-fire screendumps.

Proves the two full-screen layers really slide (a mid-flight frame contains a
black seam band at neither edge) rather than snapping.

Run with the pebble-tool python:
  ~/.local/share/uv/tools/pebble-tool/bin/python scripts/emu-anim-probe.py down
"""
import os
import re
import socket
import subprocess
import sys
import time

from libpebble2.communication import PebbleConnection
from libpebble2.communication.transports.qemu.protocol import QemuButton
from pebble_tool.commands.base import PebbleTransportEmulator
from pebble_tool.commands.emucontrol import send_data_to_qemu

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

BUTTONS = {
    "back": QemuButton.Button.Back,
    "up": QemuButton.Button.Up,
    "select": QemuButton.Button.Select,
    "down": QemuButton.Button.Down,
}

FRAMES = int(os.environ.get("FRAMES", "12"))


class _Args:
    emulator = os.environ.get("PEBBLE_EMULATOR", "emery")
    sdk = None
    vnc = False


def monitor_port():
    out = subprocess.check_output(["ps", "aux"], text=True)
    for line in out.splitlines():
        if "qemu-pebble" in line and "monitor tcp::" in line:
            match = re.search(r"monitor tcp::(\d+)", line)
            if match:
                return int(match.group(1))
    raise SystemExit("no qemu monitor socket")


def screendump(sock, path):
    sock.sendall(("screendump %s\n" % path).encode())
    for _ in range(40):
        if os.path.exists(path) and os.path.getsize(path) > 1000:
            with open(path, "rb") as fh:
                if fh.read(2) == b"P6":
                    return True
        time.sleep(0.005)
    return False


def classify(path):
    from PIL import Image

    im = Image.open(path).convert("RGB")
    w, h = im.size
    bg = im.getpixel((5, 220))
    cap_rows, seam_rows = [], []
    for y in range(h):
        row = [im.getpixel((x, y)) for x in (3, 100, 196)]
        if row[0] == row[1] == row[2] == (0, 0, 0):
            seam_rows.append(y)
        elif row[0] == row[1] == row[2] and row[0] not in (bg, (255, 255, 255)):
            cap_rows.append(y)
    caps = []
    if cap_rows:
        start = prev = cap_rows[0]
        for y in cap_rows[1:]:
            if y == prev + 1:
                prev = y
            else:
                caps.append((start, prev))
                start = prev = y
        caps.append((start, prev))
    return caps, (min(seam_rows), max(seam_rows)) if seam_rows else None


def main():
    button = (sys.argv[1] if len(sys.argv) > 1 else "down").lower()
    transport = PebbleTransportEmulator.get_transport(_Args())
    conn = PebbleConnection(transport)
    conn.connect()
    conn.run_async()
    PebbleTransportEmulator.post_connect(conn)

    port = monitor_port()
    sock = socket.create_connection(("localhost", port), timeout=3)
    sock.settimeout(3)
    sock.recv(65535)  # banner

    state = BUTTONS[button]
    send_data_to_qemu(conn.transport, QemuButton(state=state))
    t0 = time.time()
    frames = []
    for i in range(FRAMES):
        path = "/tmp/hg-anim-%02d.ppm" % i
        if os.path.exists(path):
            os.remove(path)
        ok = screendump(sock, path)
        time.sleep(0.02)  # let qemu refresh the surface between dumps
        ts = (time.time() - t0) * 1000
        if ok:
            caps, seam = classify(path)
            frames.append((i, ts, caps, seam))
        else:
            frames.append((i, ts, None, None))
    send_data_to_qemu(conn.transport, QemuButton(state=0))
    sock.close()

    for i, ts, caps, seam in frames:
        print("frame %02d  t=%+6.0fms  caps=%s  seam=%s" % (i, ts, caps, seam))
    moving = [f for f in frames if f[2] and (len(f[2]) > 1 or f[3])]
    print("\nmid-flight frames:", len(moving))
    for f in moving:
        print("  -> /tmp/hg-anim-%02d.ppm caps=%s seam=%s" % (f[0], f[2], f[3]))


if __name__ == "__main__":
    main()
