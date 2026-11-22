# Plan: A/C before getting in (key fob)

Goal (Adi, 2026-11-22): press and hold a key fob button so the cabin is cool before getting in.

## What the car has (full coding read, 2026-11-22)

- Climate (IHKA, 0x78): automatic A/C with an **engine-driven compressor** (`KOMPRESSOR_TYP = r134a_gross`; hybrids
  have `EKK`, an electric one). So with the engine off there is no cold air, only ventilation.
  `STANDLUEFTEN` (parked ventilation) is already **on**; `STANDKUEHLEN` (parked cooling) is off;
  `Vorkonditionierung Klima` is off. No parked heater (option 536 absent, `STANDHEIZUNG` off).
- Key fob: 4 buttons. `BELEGUNG_DER_4_IDG-TASTE` (the headlight button) = `heimleuchten`; it can be
  `standkuehlen` or `panikmode` instead. `ZEIT_REMOTE_KEY_3RD_BUTTON = 0x05`, `REMOTE_KEY_SPECIAL_FCT` off.
- Remote engine start exists in the coding but is off: CAS `MOTORFERNSTART_GENERELL`, `MOTORFERNSTART_CAN`;
  FZD `Motorfernstart`. The DME's coding has **no** remote-start setting, so whether the diesel software accepts a
  remote start is unknown.
- Every module involved can now be written (signers proven 2026-11-22; CAS = keyed MD5).

## Option A: parked ventilation from the key (low risk)

Recipe `key-parked-ventilation`: CAS 4th button -> `standkuehlen`, IHKA `STANDKUEHLEN` -> on.
Effect: engine off, the blower pulls outside air through the cabin. Takes the worst heat out, no cold air.
Cost: the 4th button stops doing the home lights.

## Research (2026-11-22): factory remote start is not codeable on F-series

- No report anywhere of CAS4 `MOTORFERNSTART_*` doing anything on an F-series car. Consensus (f10-forum.de,
  bimmerfest, motor-talk): the key never sends the request, and CAS4 will not start the engine without a key inside.
  The switches are probably left over from the E65 (which had "Fernstart" outside Germany with extra hardware).
- BMW's factory remote engine start is SA **1CR**: G-series (2018/19+), BDC2/3, **petrol only**, and the DME needs an
  **FSC** (activation code); coding alone was refused even on a G30. Our N57 diesel DDE has no remote-start function.
- What works on F-series: aftermarket modules that use the factory key (e.g. BimmerTech's: lock 3x starts, trunk 3x
  stops, brake before unlock stops the engine). Hardware purchase, not coding.
- Sources: f10-forum.de thread 8756, drive2.ru/l/537623528782954799 (1CR petrol-only per TIS), bmw-rudel.de E66
  Motorfernstart. Notes: research/remote_start.md (session scratchpad).

## Option B: remote engine start (experimental, very unlikely)

Recipe `remote-engine-start`: CAS `MOTORFERNSTART_GENERELL`, `MOTORFERNSTART_CAN`, `REMOTE_KEY_SPECIAL_FCT` on;
FZD `Motorfernstart` on. With the engine running, the A/C runs at its last setting.

Unknowns: the trigger gesture (BMW's is lock, then hold the 3rd button), whether the DME starts without a key
inside, run time and auto-stop. BMW's own remote start stops the engine when a door opens without a valid key and
after about 10-15 minutes; we must confirm the car does that before relying on it.

Test at the car (all must hold, or revert from the backup the recipe takes):
1. Outdoors, P, handbrake on, nobody inside, doors locked.
2. Try the gesture. Watch the cluster for Check Control messages and the DME/CAS fault memory afterwards
   (`tools_diag_dump.py`).
3. If the engine starts: open a door without the key -> engine must stop. Leave it -> must stop by itself.
4. Pressing the brake with the key inside must take over normally; without the key it must not drive.
5. Any doubt -> restore the CAS and FZD backups.

## Order on flashing day

1. Option A (cheap win, easy to undo).
2. Option B only after A, with the test above, and only if Adi wants to try it.
