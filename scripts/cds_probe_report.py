#!/usr/bin/env python3
"""Summarise a car data (CDS) probe log pulled from the NBT (PLAN-diagnostics.md, phase 1).

Usage: python3 scripts/cds_probe_report.py <path to nbt-aa-cds-probe.log | car-logs folder>

The log is written by libNbtAndroidAuto.so when the cds-probe flag file exists. Each line is
"<monotonic s.ms> <name> <get|chg|start|subscribed|off|died-at> <json>".
Prints a Markdown table: which properties answered, how often they changed, first and last value, and which
names never answered (they probably do not exist on this car).
"""
import os
import sys


def find_log(path):
    if os.path.isfile(path):
        return path
    for root, _dirs, files in os.walk(path):
        if "nbt-aa-cds-probe.log" in files:
            return os.path.join(root, "nbt-aa-cds-probe.log")
    sys.exit(f"No nbt-aa-cds-probe.log under {path}")


def short(text, size=70):
    text = text.replace("|", "\\|")
    return text if len(text) <= size else text[: size - 3] + "..."


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    log = find_log(sys.argv[1])
    names, notes, props = [], [], {}
    with open(log, encoding="utf-8", errors="replace") as f:
        for line in f:
            parts = line.rstrip("\n").split(" ", 3)
            if len(parts) < 3:
                continue
            t, name, kind = parts[0], parts[1], parts[2]
            value = parts[3] if len(parts) > 3 else ""
            if name == "probe":
                notes.append(f"{t} {kind} {value}")
                continue
            if name not in props:
                names.append(name)
                props[name] = {"get": None, "changes": 0, "first": None, "last": None, "t0": t, "t1": t}
            p = props[name]
            p["t1"] = t
            if kind == "get":
                p["get"] = value
            else:
                p["changes"] += 1
            if p["first"] is None:
                p["first"] = value
            p["last"] = value

    print(f"# CDS probe report\n\nLog: `{log}`\n")
    for n in notes:
        print(f"- probe: {n}")
    print("\n| Property | Read once | Changes | First value | Last value |")
    print("|---|---|---|---|---|")
    for name in sorted(names):
        p = props[name]
        got = "yes" if p["get"] is not None else "no"
        print(f"| `{name}` | {got} | {p['changes']} | `{short(p['first'] or '')}` | `{short(p['last'] or '')}` |")
    print(f"\n{len(names)} properties answered.")
    print("Names in the probe list that never answered are missing from this table: they probably do not exist on "
          "this car, or only send a value when something on the car changes.")


if __name__ == "__main__":
    main()
