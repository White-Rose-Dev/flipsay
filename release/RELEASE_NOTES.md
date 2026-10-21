## FlipSay FAP 0.2.1

**Install:** copy `flipsay.fap` to `SD Card/apps/Sub-GHz/`. For SDR Mode also download `tools/flipsay_sdr_bridge.py` and `tools/requirements.txt` (or clone the repo).

**New**
- **About FlipSay**: illustrated how-to on the first start, feature pages, credits and GitHub link
- **Multi Use**: pick what each window shows, with 2, 3 or 4 windows (EXT CC1101, INT CC1101, NRF24 spectrum, NRF24 packets, Wi-Fi module)
- **Wi-Fi module** window for the official devboard running Marauder (AP list, channels, packet rate, raw log); tells you to update if the firmware is not Marauder
- **SDR Mode** for GQRX over USB (school bus included), with a PC bridge script

**Built against:** Momentum dev `d3f89dfe` (API 87.1).

**Notes:** receive only, and not a true SDR (RSSI sweep, no IQ, no audio in GQRX). Wi-Fi and SDR Mode have not been tested on hardware yet, please report problems with `/ext/apps_data/flipsay/trace.log` attached. See the README, `docs/SDR_MODE.md` and `docs/WIFI_MODULE.md`.
