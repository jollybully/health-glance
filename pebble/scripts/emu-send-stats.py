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
from libpebble2.services.appmessage import AppMessageService, ByteArray, CString, Uint32
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
    # live HR (companion BLE strap reader) and the detail-screen series
    "HR_LIVE": 10041, "HR_LIVE_TS": 10042, "HR_LIVE_BATT": 10043,
    "HR_LIVE_STATE": 10044, "HR_LIVE_MIN": 10045, "HR_LIVE_MAX": 10046,
    "STEPS_SERIES": 10047, "SLEEP_SERIES": 10048, "HR_SERIES": 10049,
    "WO_SERIES": 10050, "WO_HIST": 10051, "SERIES_DAY": 10052,
    "DAY_STEPS": 10053, "DAY_SLEEP_MIN": 10054, "DAY_DEEP": 10055,
    "DAY_REM": 10056, "DAY_HR_MIN": 10057, "DAY_HR_MAX": 10058,
}

# 24 hourly slots, sent as byte arrays so six keys cover four charts.
STEPS_HOURLY = [0, 0, 0, 0, 0, 0, 80, 180, 320, 520, 780, 1240,
                960, 680, 860, 600, 420, 500, 720, 380, 260, 120, 40, 0]
STEPS_BYTES = bytes(min(255, v // 100) for v in STEPS_HOURLY)
# 0 deep, 1 light, 2 rem, 3 awake
SLEEP_BYTES = bytes([3, 2, 1, 0, 0, 1, 2, 0, 1, 1, 0, 2,
                     1, 0, 0, 1, 2, 1, 0, 0, 1, 2, 3, 3])
HR_BYTES = bytes([72, 68, 66, 64, 70, 78, 96, 112, 104, 92, 88, 121,
                  148, 108, 96, 104, 118, 132, 106, 94, 86, 80, 76, 70])
WO_BYTES = bytes([0, 42, 31, 62, 0, 31, 0])  # minutes per day, Mon..Sun
# 7 sessions x 6 bytes: [dur_secs LE2, dist_km*10, avg_hr, days_ago, sport]
WO_HIST_BYTES = bytes([
    1875 & 0xFF, 1875 >> 8, 43, 137, 0, 0,   # 31:15 walk today
    2538 & 0xFF, 2538 >> 8, 61, 148, 1, 1,   # 42:18 run yesterday
    3732 & 0xFF, 3732 >> 8, 180, 121, 3, 2,  # 1:02:12 ride Tuesday
    1685 & 0xFF, 1685 >> 8, 38, 132, 5, 0,
    3310 & 0xFF, 3310 >> 8, 92, 141, 6, 1,
    2100 & 0xFF, 2100 >> 8, 51, 128, 8, 0,
    2880 & 0xFF, 2880 >> 8, 74, 136, 10, 2,
])

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
# Mirror the companion exactly: the cards' scalars go first, then the detail
# screens' series as their own message (so a byte-array problem cannot take the
# cards down with it).
def day_steps(day):
    """Seven days of plausible step days, each with a different shape."""
    base = [0, 0, 0, 0, 0, 0, 40, 120, 260, 410, 300, 520,
            680, 540, 610, 430, 360, 480, 620, 300, 180, 90, 30, 0]
    if day == 0:
        vals = STEPS_HOURLY
    elif day == 1:
        vals = [v * 3 // 4 for v in base]           # quieter day
    elif day == 2:
        vals = [v * 5 // 4 for v in base]           # busier day
    elif day == 3:
        vals = [base[i] if i >= 12 else 0 for i in range(24)]  # late start
    elif day == 4:
        vals = [0] * 24                             # a day off: no bars at all
    else:
        vals = [v // 2 for v in base]
    return bytes(min(255, v // 100) for v in vals)


def day_hr(day):
    shift = day * 4
    return bytes(max(45, min(190, v - shift + (day % 2) * 6)) for v in HR_BYTES)


def day_sleep(day):
    rotate = day % 5
    return bytes(SLEEP_BYTES[rotate:] + SLEEP_BYTES[:rotate])


PAYLOAD.update({
    K["HR_LIVE"]: Uint32(int(os.environ.get("EMU_BPM", 132))),
    K["HR_LIVE_TS"]: Uint32(int(time.time())),
    K["HR_LIVE_BATT"]: Uint32(84),
    K["HR_LIVE_STATE"]: Uint32(3),
    K["HR_LIVE_MIN"]: Uint32(61),
    K["HR_LIVE_MAX"]: Uint32(148),
})

SERIES_PAYLOAD = {
    K["STEPS_SERIES"]: ByteArray(STEPS_BYTES),
    K["SLEEP_SERIES"]: ByteArray(SLEEP_BYTES),
    K["HR_SERIES"]: ByteArray(HR_BYTES),
    K["WO_SERIES"]: ByteArray(WO_BYTES),
    K["WO_HIST"]: ByteArray(WO_HIST_BYTES),
    K["SERIES_DAY"]: Uint32(0),
}


if "--live-only" in sys.argv:
    # exactly what HeartRateService sends: six keys, nothing else
    PAYLOAD = {
        K["HR_LIVE"]: Uint32(int(os.environ.get("EMU_BPM", 131))),
        K["HR_LIVE_TS"]: Uint32(int(time.time())),
        K["HR_LIVE_BATT"]: Uint32(84),
        K["HR_LIVE_STATE"]: Uint32(3),
        K["HR_LIVE_MIN"]: Uint32(61),
        K["HR_LIVE_MAX"]: Uint32(148),
    }
    SERIES_PAYLOAD = {}

SERIES_KEYS = {"STEPS_SERIES", "SLEEP_SERIES", "HR_SERIES", "WO_SERIES", "WO_HIST",
               "HR_LIVE", "HR_LIVE_TS", "HR_LIVE_BATT", "HR_LIVE_STATE",
               "HR_LIVE_MIN", "HR_LIVE_MAX", "SERIES_DAY"}

if "--wo-2" in sys.argv:
    # mirrors the real companion: two sessions then zero padding to 42 bytes
    PAYLOAD[K["WO_HIST"]] = ByteArray(WO_HIST_BYTES[:12] + bytes(30))

if "--no-series" in sys.argv:
    # prove the detail screens degrade honestly when only summary keys arrive
    PAYLOAD = {k: v for k, v in PAYLOAD.items()
               if k not in {K[n] for n in SERIES_KEYS}}


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
            if K["SERIES_DAY"] in data:
                # mirror the companion: answer with that day's own charts
                day = int(data[K["SERIES_DAY"]])
                steps = day_steps(day)
                hr = day_hr(day)
                sleep = day_sleep(day)
                print("day %d steps=%d hr=%d" % (day, sum(steps), max(hr)), flush=True)
                service.send_message(target, {
                    K["SERIES_DAY"]: Uint32(day),
                    K["STEPS_SERIES"]: ByteArray(steps),
                    K["HR_SERIES"]: ByteArray(hr),
                    K["SLEEP_SERIES"]: ByteArray(sleep),
                    # totals, exactly as the companion now sends them
                    K["DAY_STEPS"]: Uint32(sum(steps) * 100),
                    K["DAY_SLEEP_MIN"]: Uint32(372 + day * 9),
                    K["DAY_DEEP"]: Uint32(58 + day * 3),
                    K["DAY_REM"]: Uint32(84 + day * 4),
                    K["DAY_HR_MIN"]: Uint32(min(v for v in hr if v > 0)),
                    K["DAY_HR_MAX"]: Uint32(max(hr)),
                })
                print("answered SERIES_DAY %d" % day, flush=True)
            if K["STATS_REQ"] in data:
                service.send_message(target, PAYLOAD)
                time.sleep(0.25)
                if SERIES_PAYLOAD:
                    service.send_message(target, SERIES_PAYLOAD)
                print("answered STATS_REQ with %d keys + series" % len(PAYLOAD), flush=True)

        service.register_handler("appmessage", on_message)
        print("listening %.0fs for STATS_REQ -> %s" % (listen, target), flush=True)
        time.sleep(listen)
        print("messages from watch:", len(seen))
        return

    service.send_message(target, PAYLOAD)
    time.sleep(0.25)
    if SERIES_PAYLOAD:
        service.send_message(target, SERIES_PAYLOAD)
    time.sleep(1.5)
    print("pushed %d keys + %d series keys -> %s" % (len(PAYLOAD), len(SERIES_PAYLOAD), target))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:  # noqa: BLE001
        print("emu-send-stats:", exc, file=sys.stderr)
        sys.exit(1)
