# Car session: coding + diagnostics (next drive)

One ENET cable does all of it. Keep the engine running or a charger on the battery — a coding write must not lose power.
Gateway is at `169.254.101.24` (the probe/reset tools find it on the ENET adapter now).

## A. Android Auto + Car Health probe (the drive)

1. Connect the cable, wait ~20 s for the Mac's adapter to get a 169.254 address.
2. Install the latest build (probe-start fix + mic/sound defaults baked in):
   ```bash
   cd ~/experiment/bmw && ./flash-aa-open.sh --install-only
   ```
   Then reboot the head unit: `python3 scripts/nbt_reset.py`
3. Turn the car-data probe on and open Android Auto once:
   ```bash
   scripts/cds-probe.sh on
   ```
4. Drive ~15 min: engine off 2 min, idle 5 min, short drive. Use lights, wipers, A/C, reverse gear.
5. Before switching off (the probe log is in RAM):
   ```bash
   python3 scripts/nbt_pull_logs.py
   python3 scripts/car_health.py .local/car-logs/<newest>
   scripts/cds-probe.sh off
   ```
   `car_health.py` prints OK / watch / act per value and lists any property with no rule yet (tell me those; I add rules).

## B. Coding — full reads + verify the signers (parked, engine on or charger)

The coding tool lives in `~/experiment/nbt-coding`. The CAFDs are decrypted; keys are recovered and git-ignored.

1. Start the GUI: `cd ~/experiment/nbt-coding && python3 -m nbtcoding`
2. For **FRM** and **KOMBI**: pick the module, **Read from car** (this now reads every datagroup the CAFD needs).
3. Verify each signer against the car:
   ```bash
   python3 -m nbtcoding.sign
   ```
   FRM and KOMBI should both print `signature VERIFIED against the car` (NBT already does). If one does not, stop and
   tell me — do not write that module.

## C. Find the 5 wishlist settings (read → toggle → read → diff)

BMW ships every setting unnamed, so we locate each one by the change it makes. Nothing here writes.

For each feature (mirror fold on lock, auto start/stop default, lock confirm light+horn, welcome lights, needle sweep):

1. Read the module (GUI **Read from car**, or `python3 -m nbtcoding.probe`) — the "before".
2. Toggle the feature in the car: iDrive menu, key fob, or the physical control.
3. Read again — the "after".
4. Diff:
   ```bash
   python3 -m nbtcoding.diff <before.json> <after.json> <MODULE>
   ```
   It prints the exact setting(s) that changed, with location and old → new. That is the feature.
5. Tell me the location; I add the name so it shows up labelled.

Modules: mirror fold / start-stop / lock confirm → CAS; welcome lights → FRM; needle sweep → KOMBI.
(CAS uses AES-128-CMAC, not RSA — now implemented and NIST-proven. CAS becomes writable once a full CAS read reproduces its 16-byte tag, same as FRM/KOMBI. Still last, being the immobiliser.)

## D. First real write — FRM only, carefully (after B verified FRM)

Only once FRM's signer verified (step B) and a backup exists. FRM (lights) is the safe first write; CAS (AES-CMAC, now implemented) is last, being the immobiliser. The tool keeps a full backup on every read; the write is gated (`write.commit`, owner unlock string).
We do this together, one setting, read back, confirm the effect, keep the backup to revert.

## Safety

- Engine running or charger on for any write.
- One setting at a time, read back after, keep the backup.
- Never code HU_NBT (the Android Auto mod lives there).
- A head-unit hang: hold the volume knob ~30 s (harmless).
