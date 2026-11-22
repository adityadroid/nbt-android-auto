# Next car visit: flash Car health v2, probe oil values, apply coding

One ENET cable does all of it. Engine running (or a charger on) for every coding write. Leave the iDrive Car health
page closed while the Mac talks to the car. Everything below was built and tested offline on 2026-11-22.

## 1. Car health v2 (head unit)

```bash
cd ~/experiment/bmw && ./flash-aa-open.sh --install-only      # asks for the NBT root password
python3 scripts/nbt_reset.py                                  # restart the head unit (the knob restart did not work)
```

Check: Vehicle Info -> Car health. First line is the overall verdict, then battery, fuel, range, DPF, faults with names.

## 2. Oil and engine values (DME measurement probe, read-only)

```bash
cd ~/experiment/nbt-coding && python3 tools_probe_measure.py
```

It reports, for oil temperature / level / pressure, rail and boost actual vs target, DPF pressure, which route
works (default, extended, dynamic) and the value. Tell me the output; I switch the matching rows on in
`src/health/health.tab` and push the table (re-run step 1, no rebuild needed for table-only changes).

## 3. Coding (recipes)

Every module's signer is proven against this car (all 31, 2026-11-22). Each recipe reads the module again, saves a
backup, writes one signed change per module and reads it back. Review first (`--car`), then write (`--car --write`).

```bash
cd ~/experiment/nbt-coding
python3 -m nbtcoding.recipe                                  # list
python3 -m nbtcoding.recipe digital-speed --car              # review only
python3 -m nbtcoding.recipe digital-speed --car --write      # write (asks for the owner phrase)
```

Suggested order (low risk first): `digital-speed` (KOMBI) -> `start-stop-memory` (CAS) -> `lock-horn` (CAS) ->
`mirror-fold-door-handle` (CAS) -> `key-parked-ventilation` (CAS + IHKA). The GUI (`python3 -m nbtcoding`) has the
same recipes as "Popular changes" cards.

## 4. Remote engine start (experimental, only if wanted)

Read `PLAN-remote-climate.md` first. Recipe `remote-engine-start` (CAS + FZD), then the test there: outdoors, P,
nobody inside, doors locked; try the gesture; a door opened without the key must stop the engine; it must stop by
itself. Any doubt: restore the CAS and FZD backups the recipe saved.

## Safety

- Never code HU_NBT (Android Auto lives there; the tool refuses it).
- One recipe at a time; test the effect before the next.
- A head-unit hang: `python3 scripts/nbt_reset.py` from the Mac.
