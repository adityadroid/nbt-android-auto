#!/usr/bin/env python3
"""Car Health verdicts from a CDS probe log (PLAN-diagnostics.md, phase 2).

    python3 scripts/car_health.py <nbt-aa-cds-probe.log | car-logs folder>

Reads the probe log (what scripts/cds_probe_report.py summarises), pulls the latest value of each health-relevant CDS
property, and turns it into a plain OK / watch / act verdict. The property NAMES are still candidates until the probe
drive confirms them, so each rule lists several aliases; a rule only fires for a name that actually answered. Values the
drive confirms but no rule covers are listed at the end, so rules can be added. Thresholds are for an F10 530d (diesel)
and are marked to confirm once we see real ranges. Read-only: parses a local file, never touches the car.
"""
import json
import os
import re
import sys

OK, WATCH, ACT, UNKNOWN = "OK", "watch", "act", "?"


def _num(v):
    """First number in a CDS value (JSON like {"engine.temperature":{"value":90}} or a bare number/string)."""
    m = re.search(r"-?\d+(?:\.\d+)?", str(v))
    return float(m.group()) if m else None


def band(value, ok_hi=None, watch_hi=None, ok_lo=None, watch_lo=None):
    """Verdict from thresholds: within ok -> OK, within watch -> watch, past watch -> act."""
    if value is None:
        return UNKNOWN
    if ok_lo is not None and value < ok_lo:
        return WATCH if (watch_lo is None or value >= watch_lo) else ACT
    if ok_hi is not None and value > ok_hi:
        return WATCH if (watch_hi is None or value <= watch_hi) else ACT
    return OK


# Each rule: friendly label, candidate CDS names, and a function value->verdict. Thresholds: confirm after the drive.
RULES = [
    ("Coolant temperature", ["engine.temperature", "engine.coolantTemperature", "engine.temp"],
     lambda v: band(v, ok_hi=105, watch_hi=112, ok_lo=70, watch_lo=50), "°C, normal ~85–105 warm"),
    ("Oil temperature", ["engine.oilTemperature", "engine.oilTemp"],
     lambda v: band(v, ok_hi=125, watch_hi=140), "°C"),
    ("Oil level", ["engine.oilLevel", "sensors.oilLevel"],
     lambda v: band(v, ok_lo=15, watch_lo=5), "electronic dipstick %/level"),
    ("Battery voltage", ["sensors.battery", "sensors.batteryVoltage", "electrical.batteryVoltage"],
     lambda v: band(v, ok_lo=12.2, watch_lo=11.8, ok_hi=14.8, watch_hi=15.2), "V (engine off ~12.4–12.7)"),
    ("Fuel range", ["driving.range", "sensors.fuelRange", "driving.remainingRange"],
     lambda v: band(v, ok_lo=50, watch_lo=20), "km remaining"),
    ("Fuel level", ["sensors.fuel", "driving.fuelLevel"],
     lambda v: band(v, ok_lo=15, watch_lo=8), "% tank"),
    ("Engine RPM", ["engine.RPMSpeed", "engine.rpm", "driving.rpm"],
     lambda v: band(v, ok_hi=4500, watch_hi=5200), "idle ~750"),
]
# Properties that are a message/flag rather than a number: any non-empty / non-zero content is worth surfacing.
FLAGS = [
    ("Check Control message", ["driving.checkControl", "status.checkControl", "cc.message"]),
    ("Service due (CBS)", ["status.cbs", "service.cbs", "maintenance.cbs"]),
    ("Brake wear", ["sensors.brakePadWear", "status.brakeWear"]),
]


def latest_values(log_path):
    """{property_name: last value string} from the probe log."""
    values = {}
    with open(log_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.rstrip("\n").split(" ", 3)
            if len(parts) < 3 or parts[2] in ("off", "died-at", "start"):
                continue
            name, value = parts[1], parts[3] if len(parts) > 3 else ""
            if value and value not in ("{}", '""'):
                values[name] = value
    return values


def find_log(path):
    if os.path.isfile(path):
        return path
    for root, _d, files in os.walk(path):
        if "nbt-aa-cds-probe.log" in files:
            return os.path.join(root, "nbt-aa-cds-probe.log")
    sys.exit("No nbt-aa-cds-probe.log under %s (run the probe on a drive first)" % path)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    values = latest_values(find_log(sys.argv[1]))
    if not values:
        sys.exit("The probe log has no values yet. Open Android Auto with the probe on, then drive.")
    used = set()
    rows = []
    for label, names, verdict, unit in RULES:
        name = next((n for n in names if n in values), None)
        if not name:
            rows.append((UNKNOWN, label, "— (no matching property answered)"))
            continue
        used.add(name)
        v = _num(values[name])
        rows.append((verdict(v), label, "%s = %s  [%s]  %s" % (name, values[name][:40], v, unit)))
    for label, names in FLAGS:
        name = next((n for n in names if n in values), None)
        if name:
            used.add(name)
            v = values[name]
            active = bool(_num(v)) or any(c.isalpha() for c in re.sub(r'[A-Za-z.]+":', "", v))
            rows.append((ACT if active else OK, label, "%s = %s" % (name, v[:50])))

    order = {ACT: 0, WATCH: 1, UNKNOWN: 2, OK: 3}
    rows.sort(key=lambda r: order[r[0]])
    mark = {OK: "OK  ", WATCH: "WATCH", ACT: "ACT ", UNKNOWN: "?   "}
    print("# Car Health\n")
    for verdict, label, detail in rows:
        print("- [%s] %-22s %s" % (mark[verdict], label, detail))
    extra = sorted(set(values) - used)
    if extra:
        print("\n## Confirmed properties with no health rule yet (add to RULES):")
        for n in extra:
            print("  %-34s %s" % (n, values[n][:50]))
    acts = [r for r in rows if r[0] == ACT]
    watches = [r for r in rows if r[0] == WATCH]
    print("\nVerdict: %s" % ("ACT — " + ", ".join(r[1] for r in acts) if acts
                             else "watch — " + ", ".join(r[1] for r in watches) if watches
                             else "all OK"))


if __name__ == "__main__":
    main()
