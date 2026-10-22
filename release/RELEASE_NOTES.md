## FlipSay FAP 0.2.2

**Install:** copy the `.fap` for your firmware to `SD Card/apps/Sub-GHz/`:
- Momentum: `flipsay-momentum.fap`
- Unleashed (and RogueMaster, untested): `flipsay-unleashed.fap`

For SDR Mode also download `tools/flipsay_sdr_bridge.py` and `tools/requirements.txt` (or clone the repo). SDR++ on Windows: see `docs/SDR_PLUSPLUS.md`.

**New**
- **TTX**: plays back a `.sub` file you choose on the internal CC1101, the external CC1101, or both at once on two antennas with separate frequencies. Hold OK to send, release to stop. It uses the firmware's own limits (no region bypass, no jamming or carrier modes) and warns you every time you open it. Transmitting is regulated: only send what you are allowed to send.
- **Unleashed and RogueMaster support** (separate build; Momentum build unchanged)
- Parallel Gather shows one warning instead of four

**Built against:** Momentum dev (API 87.1) and Unleashed release unlshd-094 (API 88.19).

**Notes:** TTX, the Unleashed/RogueMaster builds, the Wi-Fi window and SDR Mode are not tested on hardware yet. Please report problems with `/ext/apps_data/flipsay/trace.log` attached. See the README and `docs/TTX.md`.
