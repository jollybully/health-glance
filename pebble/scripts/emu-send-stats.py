#!/usr/bin/env python3
"""Stand in for the Android companion in the emulator.

Mirrors the Kotlin sender exactly: every number is PebbleDictionaryItem.UInt32
(so tuples arrive as TUPLE_UINT — an Int32 payload hides parser bugs), strings
are Text, and UPDATED/SLEEP_START/SLEEP_END are already short "HH:MM" strings.
With --listen it also answers the watch's STATS_REQ, which proves the
watch -> phone direction too.

  ~/.local/share/uv/tools/pebble-tool/bin/python scripts/emu-send-stats.py
  ~/.local/share/uv/tools/pebble-tool/bin/python scripts/emu-send-stats.py --listen 8
"""
import os
import sys
import time
import uuid

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

from libpebble2.communication import PebbleConnection
from libpebble2.services.appmessage import AppMessageService, CString, Uint32
from pebble_tool.commands.base import PebbleTransportEmulator
from pebble_tool.sdk.project import PebbleProject

K = {
    "STATS_REQ": 10000, "STATS_ERR": 10002, "STEPS": 10004, "STEP_GOAL": 10005,
    "DIST_M": 10006, "CALORIES": 10007, "ACTIVE_MIN": 10008, "RESTING_HR": 10009,
    "SLEEP_MIN": 10010, "DEEP": 10011, "LIGHT": 10012, "REM": 10013,
    "AWAKE": 10014, "UPDATED": 10016, "SLEEP_START": 10017, "SLEEP_END": 10018,
    "HR_MIN": 10019, "HR_MAX": 10020, "WO_DUR_S": 10027, "WO_CAL": 10028,
    "WO_AVG_HR": 10029, "WO_DIST_M": 10031, "WO_TITLE": 10034,
    "WO_ZONE_LOW": 10035, "WO_ZONE_MID": 10036, "WO_ZONE_HIGH": 10037,
}

# Deliberately different from the DEMO fixture so a screenshot proves live data.
RAW = {
    "STEPS": 8240, "STEP_GOAL": 10000, "DIST_M": 7100, "CALORIES": 2508,
    "ACTIVE_MIN": 141, "RESTING_HR": 61,
    "SLEEP_MIN": 402, "DEEP": 61, "LIGHT": 198, "REM": 88, "AWAKE": 21,
    "UPDATED": "09:41", "SLEEP_START": "23:47", "SLEEP_END": "06:29",
    "HR_MIN": 49, "HR_MAX": 152,
    "WO_DUR_S": 1875, "WO_CAL": 297, "WO_AVG_HR": 137, "WO_DIST_M": 4300,
    "WO_TITLE": "walk",
    "WO_ZONE_LOW": 30, "WO_ZONE_MID": 50, "WO_ZONE_HIGH": 20,
}

PAYLOAD = {
    K[name]: (CString(value) if isinstance(value, str) else Uint32(value))
    for name, value in RAW.items()
}


class _Args:
    emulator = os.environ.get("PEBBLE_EMULATOR", "emery")
    sdk = None
    vnc = False


def main():
    listen = 0.0
    if "--listen" in sys.argv:
        listen = float(sys.argv[sys.argv.index("--listen") + 1])

    transport = PebbleTransportEmulator.get_transport(_Args())
    conn = PebbleConnection(transport)
    conn.connect()
    conn.run_async()
    PebbleTransportEmulator.post_connect(conn)
    project = PebbleProject(ROOT)
    target = uuid.UUID(str(project.uuid))
    service = AppMessageService(conn)

    if listen:
        seen = []

        def on_message(tid, app_uuid, data):
            seen.append(data)
            print("watch -> phone:", dict(data), flush=True)
            if K["STATS_REQ"] in data:
                service.send_message(target, PAYLOAD)
                print("answered STATS_REQ with %d keys" % len(PAYLOAD), flush=True)

        service.register_handler("appmessage", on_message)
        print("listening %.0fs for STATS_REQ -> %s" % (listen, target), flush=True)
        time.sleep(listen)
        print("messages from watch:", len(seen))
        return

    service.send_message(target, PAYLOAD)
    time.sleep(1.5)
    print("pushed %d keys -> %s" % (len(PAYLOAD), target))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:  # noqa: BLE001
        print("emu-send-stats:", exc, file=sys.stderr)
        sys.exit(1)
