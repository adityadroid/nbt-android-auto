"""Join a parsed CAF, the coding bytes read from the car, and the friendly-name overlay into
a flat list of features for the GUI. Read-only.

Also finds the decoded CAFD XML for a given CAFD id under PSdZData, and saves a full backup of
the raw coding bytes (so any change can be reverted).
"""
from __future__ import annotations

import glob
import json
import os
import time
from dataclasses import asdict, dataclass

import caf as caf_mod
import names as names_mod

HERE = os.path.dirname(__file__)
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
# where decoded CAFD XMLs live (ours, and anything we decode later)
CAFD_SEARCH = [
    os.path.join(REPO, ".local", "psdz"),
    os.path.join(REPO, ".local", "psdz", "cafd"),
]
BACKUP_DIR = os.path.join(REPO, ".local", "coding-backups")


@dataclass
class Feature:
    key: str
    datagroup: str
    start: int
    end: int
    mask: int
    name: str
    default: list[int]
    options: list[list[int]]
    value_labels: dict
    current: list[int] | None       # raw masked bytes read from the car (None if not read)
    current_scalar: int | None
    is_default: bool | None
    codeable: bool                   # more than one known option


def find_cafd_xml(cafd_id: str) -> str | None:
    cafd_id = cafd_id.lower()
    for base in CAFD_SEARCH:
        for pat in ("cafd_%s.xml" % cafd_id, "cafd_%s*.xml" % cafd_id, "*%s*.xml" % cafd_id):
            hits = glob.glob(os.path.join(base, pat))
            if hits:
                return hits[0]
    return None


def build_features(c: caf_mod.Caf, coding: dict | None, overlay: dict) -> list[Feature]:
    """coding: {datagroup_addr: bytes} read from the car, or None for an offline view."""
    fns = overlay.get("functions", {})
    out: list[Feature] = []
    for f in c.all_funcs():
        key = f.key()
        meta = fns.get(key, {})
        cur = cur_scalar = is_def = None
        if coding is not None and f.dg_addr in coding:
            dg_bytes = coding[f.dg_addr]
            cur = f.decode(dg_bytes)
            cur_scalar = f.scalar(dg_bytes)
            is_def = bool(f.default) and cur == [d & f.mask for d in f.default]
        out.append(Feature(
            key=key, datagroup=f.dg_addr, start=f.start, end=f.end, mask=f.mask,
            name=meta.get("name", ""), default=f.default, options=f.values,
            value_labels=meta.get("values", {}),
            current=cur, current_scalar=cur_scalar, is_default=is_def,
            codeable=len(f.values) > 1,
        ))
    return out


def save_backup(module: str, cafd_id: str, coding: dict, vin: str = "") -> str:
    os.makedirs(BACKUP_DIR, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    path = os.path.join(BACKUP_DIR, "%s-%s-%s.json" % (module, cafd_id, stamp))
    blob = {
        "module": module, "cafd_id": cafd_id, "vin": vin, "saved": stamp,
        "datagroups": {a: b.hex() for a, b in coding.items()},
    }
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(blob, fh, indent=2)
    return path


def features_to_json(feats: list[Feature]) -> list[dict]:
    return [asdict(f) for f in feats]
