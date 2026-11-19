# At-car test — `aa-dev` fork build (2026-11-19)

Installs our own build over v0.9.0-beta. Rollback to v0.9.0 is one command (end of page).
Car parked, ignition on, ENET cable in, SSH already open (done 2026-11-19).

> The fork is **not** v0.9.0: it uses the original hardware video path, not v0.9.0's closed 60 fps GPU path.
> Picture may look softer / lower frame rate. Judge the bug fixes, not the picture.

Several fixes are ported from [valinet/nbt-android-auto](https://github.com/valinet/nbt-android-auto) (car-tested on an
F30 with the original NBT, mostly in split screen; we run full screen): steering roller, parking-assistant return,
focus-event pool, decoder audio fix, USB robustness, Bluetooth-audio fix, night mode.

## 1. Install
```bash
cd /Users/adityadroid/experiment/bmw && ./flash-aa-open.sh --install-only
```
Then a full restart (ignition off, lock, wait ~3–5 min) so the HMI loads the new plugin and page file.

## 2. Tests (one per reported bug)

| # | Bug | Do this | Pass |
|---|-----|---------|------|
| 1 | Reverse camera doesn't return | AA in front, playing. Shift R, wait 5 s, shift D. | AA comes back by itself within ~2 s |
| 1b | …but not when you left AA | Press MEDIA (leave AA), shift R, then D. | AA does **not** pop back |
| 2a | Steering volume | AA music playing. Steering-wheel volume up/down, also iDrive knob volume. | AA music gets louder/quieter |
| 2b | Steering roller (right side) | AA music playing. Roll up / down one step, push it. | Next / previous track, play/pause |
| 2c | Roller after phone button | Press the phone button, scroll the contact list. | List scrolls, songs do **not** skip |
| 2d | Skip buttons (if fitted) | Press next / prev. | May not work yet: see §3 |
| 3 | Track in HUD / cluster | AA music, no navigation. Skip to a new song. | Song title shows ~8 s in the cluster nav area (and HUD if it mirrors it) |
| 4 | Freeze | Drive normally. | Note time if it freezes |
| 5 | "Rear audio can't be controlled" | Use volume / sources as usual. | Message should no longer appear (fork uses front browser connection 0xb8, not v0.9.0's 0xf6) |
| 6 | AA exits, audio keeps playing | Drive normally, popups/PDC etc. | Note time if it happens |
| 7 | "Connected" but won't open | Use AA a long time, many popups; then Multimedia → Android Auto. | Opens (focus-event pool: iDrive ignored each focus event after 11 uses) |
| 8 | iDrive reboot | — | If it happens, logs now survive the reboot |
| 10 | Bluetooth / radio sound after AA | Use AA, then switch to Bluetooth audio or radio. | Sound plays (decoder no longer mutes io-nme) |
| 11 | Night mode | Headlights on. | Android Auto / Maps go dark |

## 3. Steering-wheel skip buttons — one-time mapping (only if 2d failed)
nbt_knob logs every steering-wheel message. Press next / prev a few times, then Claude reads the log over SSH and
writes `mfl.map` — no rebuild needed:
```
/fs/ram3/nbt-aa-native/knob.log                       ([MFL] lines)
/var/opt/hmi/data/rw/NbtAndroidAutoRT/mfl.map         e.g.  0402 01 87   (87 next, 88 prev, 85 play/pause)
```
nbt_knob reads `mfl.map`, `wheel-off` and `ptt-assistant` only when it starts: after changing them, restart the Android
Auto service (Android Auto page -> restart) or the head unit.

## 4. Logs to pull afterwards (Claude does this over SSH)
- `/var/opt/hmi/data/rw/NbtAndroidAutoRT/events.log` — session start/end + reason, picture restarts, plugin log tail. Survives reboots.
- `/var/opt/hmi/data/rw/NbtAndroidAutoRT/logs/` and `logs.prev/` — state changes, last capture/decoder/hwvideo logs.
- `/fs/ram3/nbt-aa-plugin.log` — `CDS sensors.PDCStatus`, `parking assistant off: back to Android Auto`, `resume:` lines.
- `/fs/ram3/nbt-aa-native/knob.log` — `[Wheel]` / `[MFL]` lines.

## 5. Microphone, calls and voice

How it works:
- **Voice commands** (mic icon, "Hey Google", voice replies) use the car mic over the cable, **on by default**. The input
  comes from the car's own firmware (HU_NBT 2.49/2.51 `libioamp-router-config.so.1`, `natp.cfg`):
  `/output_mono_MIC_Prepro` (mono 44.1 kHz, the router's preprocessed mic tap that nothing else reads). If it gives only
  digital zeros it switches once to `/output_multich_0` channel 2 (mic 1, 4 ch 22.05 kHz, natp's hands-free input,
  released whenever the phone stops listening) and remembers that until the next reboot.
- **Calls** go over the car's Bluetooth hands-free (HFP), as on every factory Android Auto car: Android Auto has no
  call-audio channel. Keep the phone paired over Bluetooth. Android Auto shows the dialer; the car's mic and speakers
  carry the call. **Answer / end** with the steering-wheel phone button: the car's hands-free does it.

| # | Test | Pass |
|---|------|------|
| 9a | Tap the mic in Android Auto, ask "what's the weather" | Assistant hears you; `capture-*.log` shows `AA_MIC level: peak` well above 8 |
| 9b | "Hey Google, call <name>" | Call starts, sound on car speakers, the other side hears you |
| 9c | Incoming call, press the steering-wheel phone button | Answered; press again ends it |
| 9d | After the call | Android Auto screen comes back (if not: note it) |
| 9e | Long press of the steering-wheel voice button | Google Assistant starts (iDrive passes it over Bluetooth). If not: `touch ptt-assistant` and retry |

If 9a hears nothing or sounds bad: Claude runs `scripts/nbt-mic-discover.sh` and
`micprobe -e <endpoint> -r <rate> -c <channels> -t 6` to compare inputs, then sets `mic-endpoint` / `mic-rate`
("rate channels mic-channel") / `mic-gain` (dB). If the phone button does nothing in 9c: Claude maps it in `mfl.map` to
keycode `5` (answer) / `6` (end). If anything misbehaves: `touch mic-off` and restart the service.

## Switches (files in `/var/opt/hmi/data/rw/NbtAndroidAutoRT/`)
- `aa-volume-off` — keep AA gain fixed (if volume behaves oddly / double attenuation).
- `no-hud-track` — no track line in the cluster.
- `wheel-off` — steering roller stays with iDrive only.
- `mic-off` — no car mic for Android Auto.
- `mic-endpoint`, `mic-rate`, `mic-gain`, `mic-scenario` — override the mic input (see §5).
- `bt-addr` — the car's Bluetooth address `XX:XX:XX:XX:XX:XX`, offered to the phone.
- `ptt-assistant` — long press of the steering-wheel voice button also sends the Assistant key over the cable.

## Rollback to v0.9.0-beta
```bash
/Users/adityadroid/experiment/bmw/.venv/bin/python "/Users/adityadroid/Downloads/BMW-NBT-Native-Android-Auto-v0.9.0-beta/nbt_aa_installer.py" --cli install
```
Then full restart.
