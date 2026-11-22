#!/usr/bin/env python3
"""Fake HSFZ gateway for offline tests of nbt_health: answers OBD 01, UDS 22 and 19 02 FF from a saved car capture
(nbt-coding backups: diag-*.json, extra-*.json). Listens on 127.0.0.1:6801. Read-only by nature: anything else gets
a negative response.

    python3 scripts/fake_gateway.py [--engine-off]
"""
import glob
import json
import os
import socket
import struct
import sys
import threading

B = os.path.expanduser("~/experiment/nbt-coding/backups")
diag = json.load(open(sorted(glob.glob(B + "/diag-*.json"))[-1]))
extra = json.load(open(sorted(glob.glob(B + "/extra-*.json"))[-1]))
off = "--engine-off" in sys.argv
values = extra["engine_off"]["values"] if off else extra["samples"][-1]["values"]
obd = extra["engine_off"]["obd"] if off else {k: v for k, v in diag["obd"].items() if not k.startswith("supported")}
faults = {int(k.split()[0], 16): bytes.fromhex(v["raw"]) for k, v in diag["faults"].items()}
odo = None
for k, v in diag["dids"].items():
    pass


def answer(tgt, req):
    if req[0] == 0x01 and tgt == 0x12:
        raw = obd.get("%02X" % req[1], "")
        return bytes.fromhex(raw) if raw else bytes([0x7F, 0x01, 0x31])
    if req[0] == 0x22:
        did = (req[1] << 8) | req[2]
        if tgt == 0x60 and did == 0xD10D:
            return bytes.fromhex("62d10d00013f9900013f99")
        v = values.get("%02X:%04X" % (tgt, did))
        return bytes([0x62, req[1], req[2]]) + bytes.fromhex(v) if v else bytes([0x7F, 0x22, 0x31])
    if req[:3] == b"\x19\x02\xff":
        return faults.get(tgt, bytes([0x7F, 0x19, 0x11]))
    return bytes([0x7F, req[0], 0x11])


def serve(c):
    try:
        while True:
            hdr = c.recv(6, socket.MSG_WAITALL)
            if len(hdr) < 6:
                return
            n, ctrl = struct.unpack(">IH", hdr)
            p = c.recv(n, socket.MSG_WAITALL)
            src, tgt, req = p[0], p[1], p[2:]
            c.sendall(struct.pack(">IH", n, 0x0002) + p)                  # ack echo
            resp = bytes([tgt, src]) + answer(tgt, req)
            c.sendall(struct.pack(">IH", len(resp), 0x0001) + resp)
    finally:
        c.close()


s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 6801))
s.listen(4)
print("fake gateway on 127.0.0.1:6801 (%s)" % ("engine off" if off else "idle"), flush=True)
while True:
    conn, _ = s.accept()
    threading.Thread(target=serve, args=(conn,), daemon=True).start()
