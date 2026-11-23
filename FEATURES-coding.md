# F10 530d coding features — checked against this car

Car: 2014 F10 LCI 530d (N57), India spec, original NBT, 6WB cluster, HUD, H/K, rear entertainment, RPA flat-tyre
monitor. No alarm, no comfort access, no seat module, no KAFAS camera (only the FLA high-beam camera).

Source of "now" values: the full read `nbt-coding/backups/probe-full-all-20261122-182551.json`, overlaid with the
newest per-module backup (CAS, FRM, FZD, KOMBI, HU_NBT, IHKA: the after-write reads of 2026-11-22/23). Names and
values come from each module's own CAFD plus `nbtcoding/names/`. Feature list from BimmerCode's F10 page, bimmerpost
cheat sheets and our own CAFD search. Nothing was written to the car for this list.

> Correction 2026-11-23: SPORT+ and sport displays are already on in the car (the first version got both wrong). Head-unit (HU_NBT) value labels can be wrong in places, so check each head-unit item in the car before trusting its "now" value.

## Summary

| Status | Count |
|---|---|
| available (codeable, not set yet) | 27 |
| already on | 35 |
| not in this car's coding (needs hardware or the function is missing) | 15 |
| risky (safety / legal / drivetrain) | 4 |
| **Total** | **81** |

### Best 10 to try

1. **HUD turn signals**: KOMBI `BLINKER_HUD_ENABLE` + `HUD_BLINKER_ENABLE` = aktiv.
2. ~~Sport displays in iDrive~~: already on (Adi, 2026-11-23).
3. **Fogs off when the high beam is on**: FRM `NSW_AUS_BEI_FL` = aktiv.
4. **Auto wipers stay on after a restart**: SZL `WIPER_MEMORY_DURATION` = Unendlich.
5. **Earlier low-fuel warning**: KOMBI `TNK_SCHWELLE_RESERVE` = 10_liter.
6. **No camera warning text**: HU_NBT `MACRO_CAM_LEGALDISCLAIMER` = kein_ld.
7. **Video from USB + Xvid**: HU_NBT `API_USB_VIDEO` = first_usb, `ENT_CODEC_XVID` = aktiv.
8. **Sport Individual mode** (set up your own SPORT): ICM `IcmKod_B_Sportindividualisierbar` = ein.
9. **Radio turns off when the driver door opens**: CAS `TC_LOGIC_KLR_OFF_DOOR` = aktiv.
10. **M Performance logo on the cluster at start**: KOMBI `BMW_LOGO` / `MPM_LOGO` = mpm (not proven on the 6WB).

Put HU_NBT items into **one** write. Every head-unit write rewrites all its datagroups, and the head unit carries the
Android Auto mod. CAS writes switch the ignition off at the end, so do them with the engine off.

**Head-unit names need a check.** In some HU_NBT datagroups the name overlay looks shifted: for example, a few ECO_*
functions show language values. Before writing a HU item, confirm its value list in the GUI.

---

## Lights

| Feature | What it does | Module: FUNCTION = target (now: current) | Status | Risk |
|---|---|---|---|---|
| Welcome lights: low beam + fogs | Lights come on at unlock | FRM: WL_FUNKTION_AL, WL_FUNKTION_NSW = soft_on (now: soft_on) | already on | low |
| Welcome lights: high beam | Adds high beam to the welcome lights | FRM: WL_FUNKTION_FL = soft_on (now: nicht_aktiv) | available | low |
| Welcome lights with switch in AUTO or ON | When welcome lights may run | FRM: WL_AKTIVIERUNG = LDS_in_A_oder_2 (now: LDS_in_A_oder_2) | already on | low |
| Flash on lock/unlock | Indicators confirm lock and unlock | FRM: QUITTIERUNGSBLINKEN_FREISCHALTEN = aktiv (now: aktiv) | already on | low |
| Comfort indicator 3 blinks | Tap the stalk to blink 3 times (1 or 3 only) | FRM: BLINKZYKLEN_ANZAHL_TIPP = dreimal (now: dreimal) | already on | low |
| Fogs off with high beam | Front fogs switch off while the high beam is on | FRM: NSW_AUS_BEI_FL = aktiv (now: nicht_aktiv) | available | low |
| Fogs flash with headlight flasher | Front fogs join the flash-to-pass | FRM: FLB_NSW = aktiv (now: nicht_aktiv) | available | medium |
| DRL brightness | Daytime light strength | FRM: HELLIGKEIT_DRL = wert_01__100% (now: 100%) | already on | low |
| DRL on by default | Daytime lights, with an iDrive toggle | FRM: DRL_MODUS = tfl_s_ohne_sl, TFL_PIA_DEFAULT = aktiv (now: same) | already on | low |
| Flashing brake lights on hard stop | Brake lights flash in an emergency stop | FRM: ESS_ERSCHEINUNGSBILD = bremslicht blinkend (now: same) | already on | low |
| Brake force display (bigger area) | Lights up more of the tail lamp when braking hard, instead of flashing | FRM: ESS_ERSCHEINUNGSBILD = flaechenvergroesserung (now: bremslicht blinkend) | available | medium |
| Hazards after emergency braking | Hazards come on after a hard stop | no separate function found in FRM 0000106D | not in this car's coding | low |
| Interior light soft fade | Interior lights fade in and out | FRM: IB_SOFT_ON_ENABLE = aktiv (now: aktiv) | already on | low |

## Mirrors, locks, windows, key

| Feature | What it does | Module: FUNCTION = target (now: current) | Status | Risk |
|---|---|---|---|---|
| Fold mirrors on comfort close | Hold lock on the key: windows close and mirrors fold | FRM: ASP_BEIKLAPPEN_BEI_KOMFORTSCHLIESSEN = aktiv; CAS: KOMFORTSCHLIESSUNG_FB = aktiv (now: both aktiv) | already on | low |
| Comfort close from door handle | Hold the handle's lock area: windows close, mirrors fold | CAS: KOMFORTSCHLIESSUNG_PA = aktiv (now: aktiv) | already on | medium |
| Fold mirrors on a single lock press | Mirrors fold on every lock | no such function in CAS 0000000F / FRM 0000106D | not in this car's coding | low |
| Comfort open with key | Hold unlock: windows (and sunroof) open | CAS: KOMFORTOEFFNUNG_FB = aktiv (now: aktiv) | already on | low |
| Passenger mirror dips in reverse | Kerb view when reversing | FRM: ASP_BORDSTEINAUTOMATIK = aktiv (now: aktiv) | already on | low |
| Lock horn chirp | Short horn beep on lock | CAS: CLM_HORN_AT_SECURE = Aktiv + HU_NBT: ACOUSTICAL_LOCK_CONFIRM = aktiv (now: both set). Tested 2026-11-23: no chirp | not in this car's coding (needs alarm siren) | medium |
| Lock chirp by faking an alarm | FZD thinks an alarm is fitted | FZD: DWA verbaut + Akkustische Quittierung Schaerfen = aktiv (now: nicht aktiv, restored) | not in this car's coding (tried, no sound) | medium |
| Horn if locked with engine running | Warns you that you left the engine running | CAS: HUPEN_BEI_FZG_SICHERN_IN_N = aktiv (now: aktiv) | already on | low |
| Auto-lock when driving off | Locks doors at speed | CAS: VERRIEGELN_XKMH_DEFAULT = aktiv (now: nicht_aktiv). The iDrive menu exists (HU_NBT DOOR_LOCK_AFTER_DRIVING_AWAY = aktiv), so tick it there | already on (iDrive setting) | low |
| Relock if no door opened | Re-locks after unlock with no door opened | CAS: VER_AUT_X_MIN_DEFAULT = aktiv (now: aktiv) | already on | low |
| Unlock driver door first | First press opens only the driver door | CAS: SEL_ZV_SENDER_DEFAULT = aktiv (now: nicht_aktiv); also in iDrive | available | low |
| Radio off when driver door opens | iDrive/audio shuts off when you get out | CAS: TC_LOGIC_KLR_OFF_DOOR = aktiv (now: nicht_aktiv) | available | medium |
| Key 4th button: parked ventilation | Headlight button starts the blower | CAS: BELEGUNG_DER_4_IDG-TASTE = standkuehlen; IHKA: STANDKUEHLEN = aktiv; HU_NBT: KEY_CONF_COMFORT_COOLING = aktiv (now: all set) | already on (not car-tested) | medium |
| Key 3rd button | Unlocks and opens the boot | CAS: BELEGUNG_3_IDG_TASTE = entriegeln-grosse-heckklappe_oeffnen (now: same) | already on | low |
| Easy entry (seat slides back) | Seat moves back when the door opens | CAS: EINSTIEGSHILFE (now: nicht_aktiv). No seat module on this car | not in this car's coding | low |
| Remote engine start | Start the engine from the key | CAS: MOTORFERNSTART_GENERELL/_CAN, REMOTE_KEY_SPECIAL_FCT; FZD: Motorfernstart (now: off) | not in this car's coding (needs G-series SA 1CR) | high |

## Engine, start/stop, driving modes

| Feature | What it does | Module: FUNCTION = target (now: current) | Status | Risk |
|---|---|---|---|---|
| Start/stop remembers last choice | Off stays off after a restart | CAS: TC_MSA_MEMORY = aktiv (now: aktiv) | already on | medium |
| Start/stop off by default | Starts with auto start/stop off (not needed now memory is on) | CAS: TC_MSA_DEFAULT_OFF = aktiv (now: nicht_aktiv) | available | medium |
| Sport Individual | Lets you set up SPORT (engine/gearbox/steering) in iDrive | ICM: IcmKod_B_Sportindividualisierbar = ein (now: aus) | available | medium |
| Start in ECO PRO | Car always starts in ECO PRO | ICM: IcmKod_B_InitEco = verbaut (now: nicht_verbaut) | available | medium |
| SPORT+ mode | Extra driving mode | Already on (confirmed by Adi in the car). Comes from the vehicle order (M Sport / 2TB), not a single coding switch | already on | low |
| Auto Hold | Holds the car at a stop | DSC: Funktion_AutomaticHold_aktiv = aktiv (now: aktiv) | already on | low |
| Launch control | Gearbox launch function | EGS: LC = aktiv (now: aktiv) | already on | high |
| Top speed limiter | Changes the DME speed limit | DME: VMAX (now: 255) | risky — leave alone | high |

## Cluster and HUD

| Feature | What it does | Module: FUNCTION = target (now: current) | Status | Risk |
|---|---|---|---|---|
| Digital speed in trip computer | km/h readout in the trip computer list | KOMBI: BC_DIGITAL_V = aktiv (now: aktiv) | already on | low |
| Fuel economy in km/l | Consumption unit | KOMBI: VERBRAUCH = km_l (now: km_l) | already on | low |
| Digital speed in speedometer | Digital speed inside the dial | KOMBI: DIGITAL_TACHO_ENABLE = kmh_kmh (now: keine_anzeige_kmh); meaning not confirmed, test it | available | low |
| HUD turn signals | Indicator arrows in the HUD | KOMBI: BLINKER_HUD_ENABLE = aktiv, HUD_BLINKER_ENABLE = aktiv (now: both nicht_aktiv) | available | low |
| HUD music and phone lists | Lists from the steering wheel shown in HUD | KOMBI: HUD_ENTERTAINMENT_ENABLE = aktiv; HU_NBT: HUD_ENTERTAINMENTLIST = aktiv (now: aktiv) | already on | low |
| HUD speed-limit signs | Road signs in the HUD | KOMBI: HUD_SLI_ENABLE (now: nicht_aktiv). Needs the KAFAS camera | not in this car's coding | low |
| HUD lane-change warning | Blind-spot warning in the HUD | KOMBI: HUD_TLC_ENABLE (now: nicht_aktiv). Needs the lane-change radar | not in this car's coding | low |
| M Performance start logo | Cluster shows the M Performance logo at start | KOMBI: BMW_LOGO = mpm, MPM_LOGO = mpm (now: bmw) | available (not proven on 6WB) | low |
| M Performance display mode | M Performance cluster displays | KOMBI: MPM_ENABLE = aktiv (now: nicht_aktiv) | available (not proven on 6WB) | medium |
| M5-style cluster | Cluster shows as the M5 variant | KOMBI: M_VARIANTE = motorsport (now: ag_variante). Scales/redline may be wrong | risky | high |
| Earlier low-fuel warning | Reserve warning at more litres | KOMBI: TNK_SCHWELLE_RESERVE = 10_liter (now: 7_liter; up to 14) | available | low |
| Range warning | Check Control message at this range | KOMBI: CC_RW_WARNUNG = 50_km (now: 50_km) | already on | low |
| 120 km/h warning chime | Chime above 120 km/h | KOMBI: WARNUNG_GESCHW_LIMIT = 120km/h (now: nicht_aktiv) | available | low |
| Outside-temperature warning | Check Control on low outside temp (meaning not confirmed) | KOMBI: CC_TEMPERATURWARNUNG = aktiv (now: nicht_aktiv) | available | low |
| Sport display support (cluster) | Cluster side of sport displays | KOMBI: SPA_SPORT_ENABLE = aktiv; DME: SPA = ON (now: both on) | already on | low |
| Date format | dd.mm.yy | KOMBI: DATUM = tt_mm_jj (now: tt_mm_jj) | already on | low |
| Blank line in trip computer | Lets the trip computer show nothing | KOMBI: BC_LEERZEILE = aktiv (now: aktiv) | already on | low |
| Needle sweep at start | Needles sweep at start-up | no such function in KOMBI 00001060 | not in this car's coding | low |

## iDrive (head unit, HU_NBT)

| Feature | What it does | Module: FUNCTION = target (now: current) | Status | Risk |
|---|---|---|---|---|
| Sport displays | Power/torque pages in iDrive | Already on (confirmed by Adi). HU_NBT SPA_CHECKBOX is off but is not what turns them on | already on | low |
| Sport display line colour | Sport-coloured lines | HU_NBT: COLOUR_SWITCH_LINES = sport (now: nicht_aktiv); meaning not confirmed | available | medium |
| No camera warning | Drops the warning text on the camera view | HU_NBT: MACRO_CAM_LEGALDISCLAIMER = kein_ld (now: ld_mit_timeout) | available | medium |
| No start-up warning | No legal text at start | HU_NBT: LEGAL_DISCLAIMER_TIME = kein_ld (now: kein_ld) | already on | low |
| Video from USB | Play video files from USB | HU_NBT: API_USB_VIDEO = first_usb (now: none) | available | medium |
| Extra video codecs | Xvid and VCD playback | HU_NBT: ENT_CODEC_XVID = aktiv, ENT_CODEC_VCD = aktiv (now: nicht_aktiv) | available | medium |
| Video in motion | Video while driving | HU_NBT: SPEEDLOCK_X_KMH_MIN (now: nicht_aktiv), VIDEO_FRONT_LOCKED (now: nicht_aktiv); SPEEDLOCK_X_KMH_MAX is fixed FF. Names here may be shifted | risky (illegal while driving; verify first) | high |
| Start-up screen type | Animation, splash or black | HU_NBT: STARTUP_TYPE = splash/black (now: animation) | available | medium |
| Start-up emblem | Other start emblem (unknown images; M logo not expected on original NBT) | HU_NBT: STARTUP_EMBLEM = variant_01..09 (now: variant_00) | available (experimental) | medium |
| Ringtone | BMW, MINI or Rolls-Royce ringtone | HU_NBT: RINGTONE = rr (now: bmw) | available | low |
| Service history | Service history page | HU_NBT: SERVICE_HISTORY = aktiv (now: aktiv) | already on | low |
| Lock-confirm sound menu | iDrive tick for the lock chirp | HU_NBT: ACOUSTICAL_LOCK_CONFIRM = aktiv (now: aktiv) | already on | low |
| Lock after driving off menu | iDrive tick for auto-lock | HU_NBT: DOOR_LOCK_AFTER_DRIVING_AWAY = aktiv (now: aktiv) | already on | low |
| Volume popup / xView / max start volume | BimmerCode NBT options | not found in HU_NBT 00000DED v003.012.025 | not in this car's coding | low |

## Climate

| Feature | What it does | Module: FUNCTION = target (now: current) | Status | Risk |
|---|---|---|---|---|
| Climate remembers OFF | Off stays off after a restart | IHKA: OFF_MEMORY = aktiv (now: aktiv) | already on | low |
| Recirculation memory | Recirc state kept after a restart | IHKA: MEMORY_UMLUFT = aktiv (now: aktiv) | already on | low |
| AUTO without A/C | Pressing AUTO does not switch A/C on | IHKA: AC_NICHT_EIN_BEI_AUTO = aktiv (now: nicht_aktiv) | available | low |
| Temperature offset | Shifts the cabin temperature reading | IHKA: TEMPERATUR_OFFSET = minus1K etc. (now: plus0K) | available | low |
| Parked ventilation | Blower runs with the engine off | IHKA: STANDLUEFTEN = aktiv (now: aktiv) | already on | low |
| Seat heating | Heated seats | IHKA: SITZHEIZUNG (now: nicht_aktiv). No heaters or seat module | not in this car's coding | low |

## Steering wheel, wipers, safety

| Feature | What it does | Module: FUNCTION = target (now: current) | Status | Risk |
|---|---|---|---|---|
| Auto-wiper memory | Rain sensor stays on after a restart | SZL: WIPER_MEMORY_DURATION = Unendlich (now: 15 Minuten) | available | low |
| Heated steering wheel | Wheel heating button | SZL: Lenkrad_Heizung (now: nicht aktiv). Needs the heated wheel | not in this car's coding | low |
| Passenger seatbelt reminder | Chime for the passenger belt | ACSM: SeatBeltReminder_SBR_Beifahrer = aktiv (now: nicht_aktiv). No seat occupancy sensor (SBR_Sensor_Beifahrer_* off) | not in this car's coding | medium |
| Driver seatbelt chime off | Silences the driver belt warning | ACSM: SeatBeltReminder_SBR_Fahrer = nicht_aktiv (now: aktiv) | risky — not recommended | high |
| Lane departure / collision warning | KAFAS warnings | FRM: SPURVERLASSENS_WARNUNG, AUFFAHR_WARNUNG_FRONT; HU_NBT: LANE_DEPARTURE_WARNING (now: off). Needs KAFAS | not in this car's coding | medium |
| RPA flat-tyre monitor | Warns of a flat from wheel speeds | DSC: Funktion_ReifenPannenerkennung_aktiv = aktiv_RPA (now: same) | already on | low |
