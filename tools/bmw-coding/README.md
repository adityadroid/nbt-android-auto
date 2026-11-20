# bmw-coding — our own BMW coding tool (read-only for now)

A dependency-free tool to view and (later) change the car's coding over the ENET cable, built
so we never depend on E-Sys or any paid app. Pure Python stdlib; the GUI is a local web page.

**Status: read-only.** It reads coding and decodes it into a browsable list, and saves a full
backup every time. Writing is deliberately disabled until the coding signer is built and proven
(see "Writing", below). It never changes the car yet.

## Why this is possible
On a 2014 F-series car the coding is signed **offline** (no BMW server). Each module has a
coding-description file (a "CAFD") that says where every setting lives (datagroup address +
byte + bit mask). We already have:
- the ENET/HSFZ transport and security-access login (`../nbt-unlock/`),
- this car's PSdZData (the CAFDs), with the NBT one decoded at `.local/psdz/cafd_00000ded.xml`.

## Run
```bash
./.venv/bin/python tools/bmw-coding/server.py
```
Open the printed URL (http://127.0.0.1:8733). Car at `169.254.199.99` (set `NBT_HOST` to change).

- **Read from car**: pick a module, click *Read from car*. It reads the module's CAFD id from
  its own SVK, loads the matching CAFD, reads every USER datagroup, decodes it, and saves a
  backup to `.local/coding-backups/`.
- **Load CAFD** (offline): parse a decoded CAFD by id (e.g. `00000DED`) with no car connected —
  useful to browse what a module can do.
- Filters: search, *codeable only*, *changed from default*, *named only*.

## Pieces
- `caf.py` — parse a decoded CAFD XML into datagroups → functions (location, mask, default, options).
- `ecu.py` — read a module's coding over ENET (UDS `22 <datagroup>`); module→address map; write is locked.
- `decode.py` — join CAF + car bytes + names; find CAFD XML; save backups.
- `names.py` + `names/<CAFD>.json` — friendly-name overlay (BMW strips names to "."; we fill them in).
- `server.py` + `web/index.html` — the local web GUI.

## Names
BMW ships CAFDs with function names stripped to ".", so a setting is known by its location
(`datagroup:start:end:mask`). As we confirm what each one does (from the car or community coding
sheets), we add it to `names/<CAFD_ID>.json` and it shows up named in the GUI. No rebuild needed.

## Other modules (CAS, FRM, KOMBI)
Only the NBT CAFD is decoded so far. To read CAS/FRM/KOMBI, decode their CAFDs from PSdZData into
`.local/psdz/` first (next task). `Read from car` will tell you which CAFD id a module needs.

## Writing (not built yet — the plan)
1. Read + back up (done here).
2. Prove the signer offline: re-sign the **unchanged** coding we read and check it matches the
   signature already in the car (datagroup at `ncdSignAddress`, `3007` on the NBT). No car write.
3. First write on a low-risk module (FRM), with a battery charger on, then read back and verify.
4. Only then CAS (keys/immobiliser — a bad write can stop the car starting).

Never code `HU_NBT` while the Android Auto mod is installed.
