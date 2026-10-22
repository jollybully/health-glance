#!/usr/bin/env python3
"""Push a Health Glance stats AppMessage into the running emulator app.

Stands in for the Android companion so the live (HG_DEMO=0) build can be
verified without a phone: same integer keys the Kotlin side sends, workout
title in mixed case to prove the watch upper-cases it.

Run with the pebble-tool python: ~/.local/share/uv/tools/pebble-tool/bin/python
"""
import os
import sys
import uuid

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

from libpebble2.communication import PebbleConnection
from libpebble2.services.appmessage import AppMessageService, CString, Int32
from pebble_tool.commands.base import PebbleTransportEmulator
from pebble_tool.sdk.project import PebbleProject

K = {
    "STEPS": 10004, "DIST_M": 10006, "CALORIES": 10007, "RESTING_HR": 10009,
    "SLEEP_MIN": 10010, "DEEP": 10011, "LIGHT": 10012, "REM": 10013,
    "AWAKE": 10014, "UPDATED": 10016, "SLEEP_START": 10017, "SLEEP_END": 10018,
    "HR_MIN": 10019, "HR_MAX": 10020, "WO_DUR_S": 10027, "WO_CAL": 10028,
    "WO_AVG_HR": 10029, "WO_DIST_M": 10031, "WO_TITLE": 10034,
    "WO_ZONE_LOW": 10035, "WO_ZONE_MID": 10036, "WO_ZONE_HIGH": 10037,
}

# Deliberately different from the DEMO fixture so a screenshot proves live data.
RAW = {
    K["STEPS"]: 8240, K["DIST_M"]: 7100, K["CALORIES"]: 2508, K["RESTING_HR"]: 61,
    K["SLEEP_MIN"]: 402, K["DEEP"]: 61, K["LIGHT"]: 198, K["REM"]: 88, K["AWAKE"]: 21,
    K["UPDATED"]: "2026-10-22T09:41:00Z",
    K["SLEEP_START"]: "2026-10-21T23:47:00Z",
    K["SLEEP_END"]: "2026-10-22T06:29:00Z",
    K["HR_MIN"]: 49, K["HR_MAX"]: 152,
    K["WO_DUR_S"]: 1875, K["WO_CAL"]: 297, K["WO_AVG_HR"]: 137, K["WO_DIST_M"]: 4300,
    K["WO_TITLE"]: "walk",
    K["WO_ZONE_LOW"]: 30, K["WO_ZONE_MID"]: 50, K["WO_ZONE_HIGH"]: 20,
}

# libpebble2 needs every value wrapped in an AppMessage type (Int32 / CString).
PAYLOAD = {
    k: (CString(v) if isinstance(v, str) else Int32(v)) for k, v in RAW.items()
}


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
    service = AppMessageService(conn)
    target = uuid.UUID(project.uuid if isinstance(project.uuid, str) else str(project.uuid))
    service.send_message(target, PAYLOAD)
    import time
    time.sleep(1.5)
    print("sent %d keys -> %s" % (len(PAYLOAD), target))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:  # noqa: BLE001
        print("emu-send-stats:", exc, file=sys.stderr)
        sys.exit(1)
