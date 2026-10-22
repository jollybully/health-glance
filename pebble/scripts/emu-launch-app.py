#!/usr/bin/env python3
"""Launch the current Pebble project app on a running emulator."""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

from libpebble2.communication import PebbleConnection
from libpebble2.protocol.apps import AppRunState, AppRunStateStart
from pebble_tool.commands.base import PebbleTransportEmulator
from pebble_tool.sdk.project import PebbleProject


class _Args:
    emulator = os.environ.get("PEBBLE_EMULATOR", "emery")
    sdk = None
    vnc = False


def main():
    transport = PebbleTransportEmulator.get_transport(_Args())
    conn = PebbleConnection(transport)
    conn.connect()
    conn.run_async()
    PebbleTransportEmulator.post_connect(conn)
    project = PebbleProject(ROOT)
    conn.send_packet(AppRunState(data=AppRunStateStart(uuid=project.uuid)))
    print("launched", project.long_name)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("emu-launch-app:", e, file=sys.stderr)
        sys.exit(1)
