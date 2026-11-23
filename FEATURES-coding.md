# F10 530d coding features — checked against this car

**Car:** 2014 BMW F10 530d M Sport (LCI), N57 diesel, 8-speed sport automatic, India spec, original NBT head unit.

**What the factory fitted (from the vehicle order, SA codes):**

| SA | In plain words | What it means for coding |
|---|---|---|
| 337 | M Sport package | M wheel, M aero, sport seats. With 2TB it gives the SPORT+ mode. |
| 2TB | Sport automatic with paddles | Paddles, S/M gear display, launch control in the gearbox. |
| 1CC | Auto start/stop | Start/stop memory and default can be coded. |
| 609 | Navigation Professional (NBT) | Head unit, sport displays, iDrive menus. |
| 6WB | Colour cluster (multi-function display) | Digital speed, logo, fuel warnings, M/sport displays in the cluster. |
| 610 | Head-up display | HUD content is set in the cluster (KOMBI) and head unit. |
| 3AG + 508 | Rear camera + front/rear PDC | Camera and parking menus. **No** top view or side cameras. |
| 552, 5A1, 5AC, 8TN | Adaptive LED lights, LED fogs, high-beam assist, selectable DRL | Welcome lights, DRL, fog and HBA options. |
| 534 | Auto climate (2.5-zone, "high") | Memory, AUTO behaviour, parked ventilation. |
| 688, 6FH, 696 | Harman/Kardon, rear-seat screens, DVD changer | Audio and rear entertainment options. |
| 459, 430, 431 | Electric seats with memory, mirror pack, auto-dim mirrors | Mirror fold and dip. No separate seat module for easy entry. |

**Not fitted:** alarm (302), comfort access (322), tyre pressure sensors (RDC, only the RPA flat-tyre monitor), KAFAS camera
(lane / speed-limit / collision warning), active cruise (ACC), heated wheel, heated seats, top/side view, remote start.

**Why the first version was wrong:** it judged single switches without the vehicle order. SPORT+ and sport displays are
switched on by **vehicle-order (FA) coding**: with 2TB/337 in the order, ICM gets `C_Anpassung_FES_Modus = FES_verbaut`
(driving-experience switch with SPORT+), and the head unit gets `EFF_DYN_SPORT_UNIT = aktiv` (sport displays). `HU_NBT
SPA_CHECKBOX` is a different setting. Many items below are the same: decide by **option first, switch second**.

**Sources.** "Now" values: full read `nbt-coding/backups/probe-full-all-20261122-182551.json`, with the newer per-module
reads of 2026-11-23 for CAS, FZD, KOMBI, HU_NBT and IHKA (FRM's per-module backups are older than the full read, so the
full read wins). Names and value labels: each module's own CAFD plus `nbtcoding/names/`. Car tests by Adi on 2026-11-23
beat any coding read. Feature list: BimmerCode F10 page, bimmerpost/5post F10 cheat sheets, motor-talk, F-series E-Sys
sheets. Nothing was written to the car for this list.

**Head unit (HU_NBT) warning.** Some HU_NBT value labels are shifted (for example `ECO_MODE` and `EFF_DYN_SPORT_CID`
show language names). Treat every HU "now" value as **unconfirmed** unless Adi checked it in the car. HU_NBT also carries
the Android Auto mod; our tool does not write it. Put all HU items into **one** careful write, with a full backup first.

**CAS writes** switch the ignition off at the end: do them engine off. **ACSM, DSC, EGS, DME, ICM**: only with a reason.

---

## Summary

| Status | Count |
|---|---|
| already on | 53 |
| can code | 42 |
| needs hardware | 14 |
| not in this car's software | 5 |
| risky (safety / legal / drivetrain) | 6 |
| **Total** | **120** |

Single-press mirror fold and factory remote start are listed only under "Not possible" at the end (not counted here).

## Best 15 to code next

| # | Feature | Module: FUNCTION = target | Why |
|---|---|---|---|
| 1 | HUD turn signals | KOMBI: `BLINKER_HUD_ENABLE`, `HUD_BLINKER_ENABLE` = aktiv, `HUD_PIA_BLINKER` = aktiv (+ HU `HUD_TURNSIGNAL`) | Arrows in the HUD; KOMBI part is writable now. |
| 2 | High-beam assist on at every start | FRM: `FLA_AUTO_AKTIV` = automatisch | No more pressing the stalk each trip. |
| 3 | Fogs off when high beam is on | FRM: `NSW_AUS_BEI_FL` = aktiv | Factory default; less glare. |
| 4 | Auto wipers stay on after restart | SZL: `WIPER_MEMORY_DURATION` = Unendlich | Rain sensor stays armed. |
| 5 | Earlier low-fuel warning | KOMBI: `TNK_SCHWELLE_RESERVE` = 10_liter | 7 L is tight for Indian fuel stops. |
| 6 | Sport Individual | ICM: `IcmKod_B_Sportindividualisierbar` = ein; HU: `MACRO_FDS` = popup_and_config | Sport gearbox with comfort steering, set in iDrive. |
| 7 | Radio off when driver door opens | CAS: `TC_LOGIC_KLR_OFF_DOOR` = aktiv | Factory default; audio stops when you get out. |
| 8 | AUTO without A/C compressor | IHKA: `AC_NICHT_EIN_BEI_AUTO` = aktiv | AUTO stops forcing A/C on. Small fuel saving. |
| 9 | No camera warning text | HU: `MACRO_CAM_LEGALDISCLAIMER` = kein_ld | Camera picture shows at once. |
| 10 | Video from USB + Xvid | HU: `API_USB_VIDEO` = first_usb, `ENT_CODEC_XVID` = aktiv | Films from a stick (parked; rear screens anytime). |
| 11 | Phone's own ringtone | HU: `INBAND_RINGING` = aktiv | Hear your phone's ringtone in the car. |
| 12 | Digital speed inside speedo | KOMBI: `DIGITAL_TACHO_ENABLE` = kmh_kmh | Second digital readout; test, meaning not proven. |
| 13 | Fuel stop suggestion | HU: `NAVI_FUELSTOP_PROPOSAL` = aktiv | Nav offers a fuel stop when range is low. |
| 14 | M lap timer | HU: `M_LAPTIMER` = aktiv | Fun track timer; M-car menu, may not show on NBT. |
| 15 | M Performance start logo | KOMBI: `BMW_LOGO`, `MPM_LOGO` = mpm | Cosmetic; unproven on the 6WB. |

---

## Can be turned on (42)

| Area | Feature | What it does | Module: FUNCTION = target (now: x) | Status | Risk |
|---|---|---|---|---|---|
| Audio and rear entertainment | Rear max volume step | Volume cap for rear headphones. | HU: `RSE_MAX_VOLUME_STEP` (now: 0x36 of 36/3C) | can code (small change) | low |
| Climate | AUTO without A/C | AUTO does not force the compressor on. | IHKA: `AC_NICHT_EIN_BEI_AUTO` = aktiv (now: nicht_aktiv) | can code | low |
| Climate | Temperature offset | Shifts what "22°" feels like. | IHKA: `TEMPERATUR_OFFSET` = minus1K etc. (now: plus0K) | can code | low |
| Cluster | Digital speed inside speedo | Second digital speed field. | KOMBI: `DIGITAL_TACHO_ENABLE` = kmh_kmh (now: keine_anzeige_kmh) | can code (test; meaning unproven) | low |
| Cluster | Earlier low-fuel warning | Reserve warning at more litres. | KOMBI: `TNK_SCHWELLE_RESERVE` = 10_liter (now: 7_liter; up to 14) | can code | low |
| Cluster | 120 km/h warning chime | Gong above 120 km/h. | KOMBI: `WARNUNG_GESCHW_LIMIT` = 120km/h (now: nicht_aktiv) | can code | low |
| Cluster | Reverse gear chime | Gong when R is engaged. | KOMBI: `R_GANG_AKUSTIK_ENABLE` = aktiv (now: nicht_aktiv) | can code (low value) | low |
| Cluster | M Performance start logo | MPM logo at cluster start. | KOMBI: `BMW_LOGO`, `MPM_LOGO` = mpm (now: bmw) | can code (unproven on 6WB) | low |
| Cluster | M Performance display mode | MPM cluster display. | KOMBI: `MPM_ENABLE` = aktiv (now: nicht_aktiv) | can code (experimental) | medium |
| Cluster | Speedo scale end | 260 / 300 / 330 km/h dial end. | KOMBI: `TACHO_SKALA_ENDWERT` (now: 260 km/h) | can code (cosmetic; dial may look wrong) | medium |
| Comfort and safety | Auto-wiper memory | Rain sensor stays on after restart. | SZL: `WIPER_MEMORY_DURATION` = Unendlich (now: 15 Minuten) | can code | low |
| Driving: modes, gearbox, start/stop | Sport Individual | Your own SPORT mix in iDrive. | ICM: `IcmKod_B_Sportindividualisierbar` = ein (now: aus) + HU `MACRO_FDS` | can code | medium |
| Driving: modes, gearbox, start/stop | Start in ECO PRO | Car always starts in ECO PRO. | ICM: `IcmKod_B_InitEco` = verbaut (now: nicht_verbaut) | can code | medium |
| Driving: modes, gearbox, start/stop | Start/stop off by default | Starts with start/stop off. | CAS: `TC_MSA_DEFAULT_OFF` = aktiv (now: nicht_aktiv). Not needed with memory | can code | low |
| Head-up display | HUD turn signals | Indicator arrows in the HUD. | KOMBI: `BLINKER_HUD_ENABLE`, `HUD_BLINKER_ENABLE` = aktiv, `HUD_PIA_BLINKER` = aktiv; HU: `HUD_TURNSIGNAL` = aktiv (now: all off) | can code | low |
| Head-up display | HUD M-style (rpm, gear) | M-car HUD view. | KOMBI: `HUD_PIA_M_SPORT` = aktiv (now: inaktiv); HU `HUD_M` (now: aktiv) | can code (experimental; may show nothing) | low |
| Head-up display | HUD junction view | Lane/junction picture in HUD. | HU: `JUNCTION_VIEW_HUD` = aktiv (now: nicht_aktiv) | can code (needs map data; unproven) | low |
| Lights | Welcome lights: high beam | Adds high beam to the welcome. | FRM: `WL_FUNKTION_FL` = soft_on (now: nicht_aktiv) | can code | low |
| Lights | Fogs off with high beam | Front fogs switch off while high beam is on. | FRM: `NSW_AUS_BEI_FL` = aktiv (now: nicht_aktiv) | can code | low |
| Lights | Fogs join headlight flasher | Fogs flash with flash-to-pass. | FRM: `FLB_NSW` = aktiv (now: nicht_aktiv) | can code | low |
| Lights | High-beam assist auto at start | HBA is on each time you start. | FRM: `FLA_AUTO_AKTIV` = automatisch (now: manuel) | can code | low |
| Locks, windows, mirrors, key | Unlock driver door first | First press opens only the driver door. | CAS: `SEL_ZV_SENDER_DEFAULT` = aktiv (now: nicht_aktiv). Also an iDrive setting | can code (or set in iDrive) | low |
| Locks, windows, mirrors, key | Auto-lock speed | Speed at which it locks. | CAS: `VERRIEGELUNGSSCHWELLE` = 0x08 (now: 0x10; meaning of units not proven) | can code | low |
| Locks, windows, mirrors, key | Radio off when driver door opens | Audio and iDrive stop when you get out. | CAS: `TC_LOGIC_KLR_OFF_DOOR` = aktiv (now: nicht_aktiv) | can code | low |
| Locks, windows, mirrors, key | Start/stop off when locking | Start/stop state reset on lock. | CAS: `CLM_MSA_OFF_AT_SECURE` (now: Nicht_Aktiv) | can code (low value) | low |
| Locks, windows, mirrors, key | Key 4th button = home lights | Alternative: button switches on home lights. | CAS: `BELEGUNG_DER_4_IDG-TASTE` = heimleuchten (now: standkuehlen) | can code (replaces ventilation) | low |
| Parking and camera | Auto PDC | PDC pops up when slowly nearing an obstacle. | HU: `AUTO_PDC` = aktiv (now: nicht_aktiv) | can code (needs PDC module support; unproven) | low |
| iDrive / head unit | Sport display red/M colour | M-style colours on sport pages. | HU: `M_VEHICLE` = aktiv (now: nicht_aktiv) | can code (F15 sheet; unproven on NBT) | medium |
| iDrive / head unit | Sport Individual menu | Configure SPORT in iDrive (see Driving). | HU: `MACRO_FDS` = popup_and_config (now: only_popup) | can code (with ICM) | medium |
| iDrive / head unit | M lap timer | Lap timer app. | HU: `M_LAPTIMER` = aktiv (now: nicht_aktiv) | can code (experimental) | low |
| iDrive / head unit | No camera warning | No legal text on the camera view. | HU: `MACRO_CAM_LEGALDISCLAIMER` = kein_ld (now: ld_mit_timeout) | can code | low |
| iDrive / head unit | Video from USB | Play video files from USB. | HU: `API_USB_VIDEO` = first_usb (now: none) | can code | medium |
| iDrive / head unit | Xvid / VCD codecs | More file types play. | HU: `ENT_CODEC_XVID`, `ENT_CODEC_VCD` = aktiv (now: nicht_aktiv) | can code | medium |
| iDrive / head unit | Start-up screen | Animation, splash or black. | HU: `STARTUP_TYPE` = splash/black (now: animation) | can code | low |
| iDrive / head unit | Start-up emblem | Other start images (unknown pictures). | HU: `STARTUP_EMBLEM` = variant_01..09 (now: variant_00) | can code (experimental) | low |
| iDrive / head unit | Phone's own ringtone | Ringtone from the phone, not BMW's. | HU: `INBAND_RINGING` = aktiv (now: nicht_aktiv) | can code | low |
| iDrive / head unit | Car ringtone | BMW, MINI or Rolls-Royce ringtone. | HU: `RINGTONE` = rr (now: bmw) | can code | low |
| iDrive / head unit | Gong sound set | BMW, MINI, RR or BMW i system gongs. | HU: `SOUND_SIGNAL_SET` = rolls_royce (now: bmw) | can code (experimental) | low |
| iDrive / head unit | Fuel stop suggestion | Nav proposes a fuel stop on low range. | HU: `NAVI_FUELSTOP_PROPOSAL` = aktiv (now: nicht_aktiv) | can code | low |
| iDrive / head unit | Junction view on map | Lane picture at motorway exits. | HU: `BMAP_JUNCTION_VIEW` = aktiv (now: nicht_aktiv) | can code (map data dependent) | low |
| iDrive / head unit | Speed limits from map (no camera) | Map-based limit sign in iDrive/cluster. | HU: `SLI_NAVI_DISPLAY` = aktiv (now: nicht_aktiv) | can code (experimental; India map data weak) | low |
| iDrive / head unit | Climate AUTO intensity menu | Choose AUTO blower strength in iDrive. | HU: `HVAC_AUTOMATIC_INTENSITY` = aktiv (now: nicht_aktiv) | can code (needs IHKA support; unproven) | low |

## Already on (53)

| Area | Feature | What it does | Module: FUNCTION = target (now: x) | Status | Risk |
|---|---|---|---|---|---|
| Audio and rear entertainment | Rear screens video while driving | Rear screens play while moving. | HU: `ENT_RSE_DRIVE` = dvd (now: dvd) | already on | low |
| Climate | Climate remembers OFF | Off stays off after restart. | IHKA: `OFF_MEMORY` = aktiv (now: aktiv) | already on (car-tested) | low |
| Climate | Recirculation memory | Recirc kept after restart. | IHKA: `MEMORY_UMLUFT` = aktiv (now: aktiv) | already on | low |
| Climate | A/C state kept at ignition off | A/C button state survives a restart. | IHKA: `AC_AUS_BEI_KL_15_AUS` = nicht_aktiv (now: nicht_aktiv) | already on | low |
| Climate | Parked ventilation | Blower runs with the engine off. | IHKA: `STANDLUEFTEN` = aktiv (now: aktiv) | already on (car-tested) | low |
| Cluster | Digital speed in trip computer | km/h readout in the trip list. | KOMBI: `BC_DIGITAL_V` = aktiv (now: aktiv) | already on (car-tested) | low |
| Cluster | Speedo zoom | Zoomed speedo scale in ECO/city. | KOMBI: `LUPE_TACHO_ENABLE` = aktiv (now: aktiv) | already on | low |
| Cluster | Fuel economy in km/l | Consumption unit. | KOMBI: `VERBRAUCH` = km_l (now: km_l) | already on (car-tested) | low |
| Cluster | Range warning | Check Control at this range. | KOMBI: `CC_RW_WARNUNG` = 50_km (now: 50_km) | already on | low |
| Cluster | Gear in S/M mode | Shows S1..S8 / M1..M8. | KOMBI: `SPA_IST_GANG_ENABLE` = aktiv (now: aktiv) | already on | low |
| Cluster | Sport displays (cluster side) | Cluster part of sport displays. | KOMBI: `SPA_SPORT_ENABLE` = aktiv; DME `SPA` = ON (now: both) | already on (car-tested) | low |
| Cluster | Shift lights | M-car shift lights. | KOMBI: `SHIFT_LIGHTS_ENABLE` (now: aktiv) — no visible effect on 530d | already on (no effect) | low |
| Driving: modes, gearbox, start/stop | SPORT+ mode | Sharper gearbox/throttle, looser DSC. | ICM: `C_Anpassung_FES_Modus` = FES_verbaut (now: same); from FA (337 + 2TB) | already on (car-tested) | low |
| Driving: modes, gearbox, start/stop | ECO PRO mode | Fuel-saving mode with coasting. | ICM: `IcmKod_B_ECONOMIC` = verbaut (now: verbaut) | already on | low |
| Driving: modes, gearbox, start/stop | ECO PRO coasting | Gearbox decouples when you lift off in ECO PRO. | EGS: `Segeln` = aktiv; DME `Segeln` = ON (now: on) | already on | low |
| Driving: modes, gearbox, start/stop | Paddles | Shift paddles on the wheel. | SZL: `Lenkrad_Schaltpaddles` = aktiv; ICM `IcmKod_B_S2TBA` = verbaut (now: both) | already on | low |
| Driving: modes, gearbox, start/stop | Launch control | Gearbox launch start (DSC off, SPORT, brake + full throttle). | EGS: `LC` = aktiv; DSC `Funktion_LaunchControl_aktiv` = aktiv (now: both) | already on (coding; not car-tested) | high (drivetrain wear) |
| Driving: modes, gearbox, start/stop | Start/stop remembers off | Off stays off after restart. | CAS: `TC_MSA_MEMORY` = aktiv (now: aktiv) | already on (car-tested) | low |
| Driving: modes, gearbox, start/stop | Auto Hold | Holds the car at a stop. | DSC: `Funktion_AutomaticHold_aktiv` = aktiv (now: aktiv) | already on | low |
| Driving: modes, gearbox, start/stop | Brake drying, brake standby | Wipes discs in rain, pre-fills brakes. | DSC: `Funktion_Trockenbremsen_aktiv`, `Funktion_Bremsbereitschaft_aktiv` = aktiv (now: aktiv) | already on | low |
| Driving: modes, gearbox, start/stop | Hill start assist | Holds on hills briefly. | DSC: `Funktion_Anfahrassistent_aktiv` = aktiv (now: aktiv) | already on | low |
| Driving: modes, gearbox, start/stop | RPA flat-tyre monitor | Warns of a flat from wheel speeds. | DSC: `Funktion_ReifenPannenerkennung_aktiv` = aktiv_RPA (now: same) | already on | low |
| Head-up display | HUD nav arrows | Turn arrows in the HUD. | KOMBI: `HUD_NAVI_ENABLE` = aktiv (now: aktiv) | already on | low |
| Head-up display | HUD music/phone lists | Steering-wheel lists in the HUD. | KOMBI: `HUD_ENTERTAINMENT_ENABLE`; HU `HUD_ENTERTAINMENTLIST` (now: aktiv) | already on | low |
| Head-up display | HUD Check Control | Warnings shown in the HUD. | KOMBI: `HUD_CC_ENABLE` = aktiv (now: aktiv) | already on | low |
| Head-up display | HUD ECO PRO hints | ECO PRO info in the HUD. | KOMBI: `HUD_ECO_PRO` = aktiv (now: aktiv) | already on | low |
| Head-up display | HUD rotation / height memory | HUD angle setting, saved per key. | KOMBI: `ROTATION_HUD_ENABLE`, `HUD_SMEMORY_ENABLE` = aktiv (now: aktiv) | already on | low |
| Lights | Welcome lights: low beam + fogs | Lights come on when you unlock. | FRM: `WL_FUNKTION_AL`, `WL_FUNKTION_NSW` = soft_on (now: soft_on) | already on (car-tested) | low |
| Lights | Welcome lights: tail, plate, position | Rear and side lights join the welcome. | FRM: `WL_FUNKTION_KZL`/`SSL`/`POL` = hard_on (now: hard_on) | already on | low |
| Lights | Welcome lights trigger | Works with light switch in AUTO. | FRM: `WL_AKTIVIERUNG` = LDS_in_A_oder_2 (now: same) | already on | low |
| Lights | Home lights 40 s | Lights stay on after you lock (flash stalk). | FRM: `FOMEHO_DEFAULT` = 0x28 = 40 s (now: 0x28; only 20/40 s) | already on | low |
| Lights | Lock/unlock flash | Indicators blink on lock and unlock. | FRM: `QUITTIERUNGSBLINKEN_FREISCHALTEN` = aktiv (now: aktiv) | already on | low |
| Lights | One-touch indicator 3 blinks | Tap stalk: 3 blinks (1 or 3 only). | FRM: `BLINKZYKLEN_ANZAHL_TIPP` = dreimal (now: dreimal) | already on | low |
| Lights | DRL on, iDrive toggle | Daytime lights (LED rings), switchable in iDrive. | FRM: `DRL_MODUS` = tfl_s_ohne_sl, `TFL_PIA_DEFAULT` = aktiv (now: same) | already on | low |
| Lights | DRL brightness | Strength of the daytime rings. | FRM: `HELLIGKEIT_DRL` = 100 % (now: 100 %; 50/80 % possible) | already on | low |
| Lights | Cornering light in reverse | Both cornering lights on when reversing. | FRM: `C_CLC_REV_ALWAYS_ON` = F07/7er_LCI (now: same) | already on | low |
| Lights | Flashing brake lights | Brake lights flash 5 Hz in an emergency stop. | FRM: `ESS_ERSCHEINUNGSBILD` = bremslicht blinkend, `ESS_ECE_BLINKFREQUENZ` = 5_hz (now: same) | already on | low |
| Lights | Hazards after emergency stop | Hazards come on after a hard stop. | FRM: `WB_GB_ENABLE` = aktiv (now: aktiv). First version missed this | already on (not car-tested) | low |
| Locks, windows, mirrors, key | Comfort close by key (windows + mirrors) | Hold lock: windows/roof close, mirrors fold. | CAS: `KOMFORTSCHLIESSUNG_FB` = aktiv; FRM: `ASP_BEIKLAPPEN_BEI_KOMFORTSCHLIESSEN` = aktiv (now: both) | already on (car-tested) | low |
| Locks, windows, mirrors, key | Comfort close at door lock | Hold the lock in the door handle. | CAS: `KOMFORTSCHLIESSUNG_PA` = aktiv (now: aktiv) | already on | low |
| Locks, windows, mirrors, key | Comfort open by key | Hold unlock: windows and roof open. | CAS: `KOMFORTOEFFNUNG_FB` = aktiv (now: aktiv) | already on | low |
| Locks, windows, mirrors, key | Mirror dips in reverse | Passenger mirror tilts to the kerb. | FRM: `ASP_BORDSTEINAUTOMATIK` = aktiv (now: aktiv) | already on | low |
| Locks, windows, mirrors, key | Auto-lock when driving off | Locks at speed. | CAS: `VERRIEGELN_XKMH_DEFAULT` (now: nicht_aktiv); HU menu `DOOR_LOCK_AFTER_DRIVING_AWAY` = aktiv | already on (iDrive setting) | low |
| Locks, windows, mirrors, key | Re-lock if no door opened | Re-locks after an unlock with no door opened. | CAS: `VER_AUT_X_MIN_DEFAULT` = aktiv (now: aktiv) | already on | low |
| Locks, windows, mirrors, key | Horn if locked with engine running | Warns you left it running. | CAS: `HUPEN_BEI_FZG_SICHERN_IN_N` = aktiv (now: aktiv) | already on | low |
| Locks, windows, mirrors, key | Key 3rd button | Unlocks and opens the boot. | CAS: `BELEGUNG_3_IDG_TASTE` = entriegeln-grosse-heckklappe_oeffnen (now: same) | already on | low |
| Locks, windows, mirrors, key | Key 4th button = parked ventilation | Headlight-symbol button runs the blower. | CAS: `BELEGUNG_DER_4_IDG-TASTE` = standkuehlen; IHKA `STANDKUEHLEN`; HU `KEY_CONF_COMFORT_COOLING` (now: all set) | already on (car-tested; blocked at low battery) | low |
| Parking and camera | Parking menu | Camera/PDC menu in iDrive. | HU: `MACRO_PDC` = aktiv, `MACRO_L6_PARKMENU` = aktiv (now: aktiv) | already on | low |
| Parking and camera | PDC tones through audio | Beeps over the H/K speakers, left/right. | PDC: `TONAUSGABE_LSP` = keine_Tonausgabe (audio system plays them), `AKUSTIK_RECHTS_LINKS` = aktiv (now: same) | already on | low |
| iDrive / head unit | Sport displays | Power and torque gauges in iDrive. | HU: `EFF_DYN_SPORT_UNIT` = aktiv (now: aktiv); set by FA (609 + 337/2TB) | already on (car-tested) | low |
| iDrive / head unit | No start-up warning | No legal text at start. | HU: `LEGAL_DISCLAIMER_TIME` = kein_ld (now: kein_ld) | already on | low |
| iDrive / head unit | Two phones at once | Two Bluetooth phones connected. | HU: `TWO_PHONES_PARALLEL` = aktiv (now: aktiv) | already on | low |
| iDrive / head unit | Service history page | Service records in iDrive. | HU: `SERVICE_HISTORY` = aktiv (now: aktiv) | already on | low |

## Needs hardware or not in this car's software (19)

| Area | Feature | What it does | Module: FUNCTION = target (now: x) | Status | Risk |
|---|---|---|---|---|---|
| Audio and rear entertainment | B&O high-end menu | Speaker light show menu. | HU: `HIGH_END_AUDIO_MENUE` (now: kein_menue) | needs hardware (B&O 6F1) | low |
| Climate | Seat heating / ventilation | Heated or cooled seats. | IHKA: `SITZHEIZUNG`, `SITZLUEFTUNG` (now: nicht_aktiv) | needs hardware (494 / 453) | low |
| Climate | Parked heating | Fuel heater. | IHKA: `STANDHEIZUNG` (now: nicht_aktiv) | needs hardware (536) | low |
| Cluster | Gear number in D (D1..D8) | Gear number while in D. | no F10 switch found (forums agree D stays "D") | not in this car's software | low |
| Cluster | Needle sweep at start | Needles sweep at start-up. | no sweep function in KOMBI 00001060 | not in this car's software | low |
| Comfort and safety | Heated steering wheel | Wheel heating. | SZL: `Lenkrad_Heizung` (now: nicht aktiv) | needs hardware (248 wheel) | low |
| Comfort and safety | Passenger seatbelt chime | Chime for passenger belt. | ACSM: `SeatBeltReminder_SBR_Beifahrer` (now: nicht_aktiv) | needs hardware (seat occupancy mat) | medium |
| Comfort and safety | Easy entry (seat slides back) | Seat moves back on door open. | CAS: `EINSTIEGSHILFE` (now: nicht_aktiv) | needs hardware (seat module) | low |
| Comfort and safety | Lock horn chirp | Short horn on lock. | CAS: `CLM_HORN_AT_SECURE` = Aktiv (now: Aktiv, no sound) | needs hardware (alarm 302) | low |
| Driving: modes, gearbox, start/stop | Hill descent control | Crawls down steep hills. | DSC: `Funktion_HillDecentControl_aktiv` (now: nicht_aktiv). xDrive feature | not in this car's software (RWD) | high |
| Head-up display | HUD speed-limit signs | Road signs in HUD. | KOMBI: `HUD_SLI_ENABLE` (now: nicht_aktiv) | needs hardware (KAFAS camera 5AG/5AS) | low |
| Head-up display | HUD lane-change warning | Blind-spot warning in HUD. | KOMBI: `HUD_TLC_ENABLE` (now: nicht_aktiv) | needs hardware (lane-change radar 5AG) | low |
| Lights | Corona as parking light / brightness | Change ring brightness as parking light. | FRM: `POS_LICHT_BEI_SL` (now: 0x64, single value); LED_L/R tables | not in this car's software (no menu value) | medium |
| Parking and camera | Trailer hitch view | Zoomed view of the tow ball. | HU: `MACRO_TRAILER_COUPLING` = aktiv (now: nicht_aktiv) | needs hardware (tow bar 3AC) | low |
| Parking and camera | Top view / side view | 360 and side cameras. | HU: `PDC_3D`; TRSVC top-view data (now: off) | needs hardware (5DL surround view) | low |
| Parking and camera | Parking assistant | Self-parking. | HU: `MACRO_PMA_EINPARKEN` | needs hardware (5DP) | low |
| iDrive / head unit | Max start-up volume | Cap on volume at start. | HU: `VOL_MAX_ON` (now: standard, single value) | not in this car's software | low |
| iDrive / head unit | Tyre pressure screen | Pressures and temperatures. | HU: `RDC_SAFETY` (now: nicht_aktiv) | needs hardware (RDC sensors, SA 2VB) | low |
| iDrive / head unit | Lane departure / collision menus | KAFAS warning settings. | HU: `LANE_DEPARTURE_WARNING`; FRM `SPURVERLASSENS_WARNUNG` (now: off) | needs hardware (KAFAS 5AS/5AG) | low |

## Risky (listed, not advised) (6)

| Area | Feature | What it does | Module: FUNCTION = target (now: x) | Status | Risk |
|---|---|---|---|---|---|
| Cluster | M5-style cluster | Cluster as M variant. | KOMBI: `M_VARIANTE` = motorsport (now: ag_variante) | risky (wrong scales/redline) | high |
| Comfort and safety | Driver seatbelt chime off | Silences driver belt warning. | ACSM: `SeatBeltReminder_SBR_Fahrer` = nicht_aktiv (now: aktiv) | risky (airbag module, safety) | high |
| Driving: modes, gearbox, start/stop | Top speed limiter | DME speed limit. | DME: `VMAX` (now: 255) | risky — leave alone | high |
| Lights | Brake force display (bigger area) | Lights more of the tail lamp instead of flashing. | FRM: `ESS_ERSCHEINUNGSBILD` = flaechenvergroesserung (now: bremslicht blinkend) | risky (legal grey) | medium |
| iDrive / head unit | Video in motion | Front video while driving. | HU: `SPEEDLOCK_X_KMH_MIN` = deaktivierung_bmw, `VIDEO_FRONT_LOCKED` = nicht_aktiv (now: nicht_aktiv) | risky (illegal while driving; HU labels may be shifted) | high |
| iDrive / head unit | Full message text while driving | Show whole SMS/email texts on the move. | HU: `PIM_DRIVING_TEXT_LENGTH` = whole_text (now: one_page) | risky (distraction) | medium |

## Needs hardware

| Feature | Missing part / SA |
|---|---|
| Lock horn chirp / alarm sounds | Alarm with siren (302). FZD fake-alarm test failed and was reverted. |
| Tyre pressures and temperatures | RDC wheel sensors (2VB). Car has RPA only. |
| HUD speed-limit signs, lane / collision warnings | KAFAS camera (5AS/5AG). |
| HUD lane-change warning | Lane-change radar (5AG). |
| Comfort access (keyless) | 322 (door handle antennas, CAS search). |
| Easy entry | Seat module for 459 seats not fitted. |
| Heated wheel | 248 wheel. |
| Heated / ventilated seats | 494 / 453. |
| Parked (fuel) heater | 536. |
| Passenger belt chime | Seat occupancy mat. |
| Top / side view, 3D PDC | 5DL surround view. |
| Parking assistant | 5DP. |
| Trailer hitch view | Tow bar 3AC. |

## Not possible

| Feature | Why |
|---|---|
| Single-press mirror fold | No such function in CAS 0000000F / FRM 0000106D; only hold-lock comfort close (car-tested). |
| Factory remote engine start | Needs G-series SA 1CR + FSC; switches in CAS/FZD are off and do nothing on F-series. |
| Gear number in D | No F10 cluster switch; D stays "D". |
| Needle sweep | No sweep function in KOMBI 00001060 (aftermarket flash only). |
| Max start-up volume | HU `VOL_MAX_ON` has one value only. |
| Hill descent control | xDrive only. |
