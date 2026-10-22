# Changelog

## 0.2.2

### Added
- **TTX (transmit a file):** *Options > TTX (Transmit)* plays back a `.sub` file through the internal CC1101, the external CC1101, or both at once on two antennas, each with its own frequency (DUAL). Built-in file browser, per-antenna status, 1-3 passes, hold-OK-to-send (releasing OK or pressing Back stops it), 20 s hard limit, measured A/B start offset. It uses the firmware's own transmit path and limits (region / band checks are never bypassed), has no noise or carrier-only modes, and shows a warning every time it is opened. See [docs/TTX.md](docs/TTX.md).
- **Unleashed and RogueMaster support:** the same source now builds against the Unleashed SDK (API 88.19, RogueMaster uses API 88.x). Firmware differences are in `src/flipsay_compat.h`. Releases contain `flipsay-momentum.fap` and `flipsay-unleashed.fap`.

### Changed
- Parallel Gather now shows **one** warning instead of four.
- On Unleashed / RogueMaster the CC1101 / NRF24 chip-select options show as fixed (pin 4); Momentum keeps the pin 4 / pin 7 choice.
- Auto Decode filters sensor protocols by the protocol flag on firmwares that have no protocol filter field.
- The release workflow builds each firmware in a matrix and attaches both files.
- About guide gets a TTX page. The "receive only" claims were updated: only TTX transmits.
- Worker stack raised to 7 KB.

### Known issues
- TTX, the Unleashed / RogueMaster builds, the Wi-Fi window and the Flipper-side SDR link are untested on hardware. RogueMaster is untested; its API is the same major version as Unleashed.
## 0.2.1

### Added
- **About FlipSay:** illustrated 14-page guide (D-pad and Back key graphics, one page per feature group, credits with "By itsc0c0" and the GitHub link). Shown automatically on the first start, reachable from *Options > About FlipSay*. The intro splash now shows "BY ITSC0C0". *Replay Intro* moved to *Options > System*.
- **Multi Use layouts:** Left/Right, Top/Bottom, 2 Top + 1, 1 Top + 2 and a 4-window grid. Every window can show EXT CC1101, INT CC1101, NRF24 spectrum, NRF24 packets, the Wi-Fi module or nothing. Only the radios a window needs are swept; missing hardware only affects its own window. Pan, zoom and band hop work on every spectrum window, including the new NRF24 spectrum window.
- **Wi-Fi module window:** official Flipper Wi-Fi Devboard with ESP32 Marauder firmware, passive scan / beacon / deauth / PMKID listening, four views (AP list, channels, packet rate, raw log). Wrong or silent firmware is detected and reported.
- **SDR Mode for GQRX** (*Options > SDR Mode*): black status screen (waiting for USB / connecting / connected) with a school bus driving along the bottom, USB link, and the PC bridge `tools/flipsay_sdr_bridge.py` that serves an rtl_tcp stream. Frequency, sample rate and gain from GQRX control the Flipper's sweep. Spectrum and waterfall only (synthetic IQ, no audio).

### Changed
- Settings file version 3 (older settings files are ignored once and reset to defaults).
- Multi Use no longer requires an external CC1101 or an NRF24.
- Worker stack raised to 6 KB.

### Known issues
- Wi-Fi and SDR Mode are new and have not been tested on hardware yet. The bridge was tested offline only.
## 0.1.2 - first public release

First release of the Flipper app, built against Momentum `dev` (SDK d3f89dfe, API 87.1).

### Features
- Spectrum, Waterfall, Combo, Persistence, Pulse Scope, S-Meter, Signals and Channels views.
- Internal CC1101, external CC1101 (generic GPIO) and NRF24L01+ with auto-detection.
- Zoom down to 50 kHz, band scrolling, band presets, bookmarks.
- Auto Decode using the firmware's Sub-GHz protocol library (weather, TPMS and sensor protocols ignored), saved as `.sub` key files, with an on-screen notification banner and a saved-decodes manager.
- Parallel Gather: Merged mode and Multi Use split windows; NRF24 packet monitor window.
- CC1101 gain controls, selectable CS pins, squelch trigger, raw `.sub` capture, CSV logging, screenshots.
- Real boot loading bar, Lite Mode, memory/stack readout and low-memory guards.

### Known issues
- Hardware testing is ongoing; Auto Decode and Parallel Gather in particular have had little real-world testing.
- The app is receive-only and not a true SDR (RSSI sweep, no IQ).
