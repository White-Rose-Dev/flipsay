# Troubleshooting

## Collecting information for a bug report

1. **Trace file:** the app writes `/ext/apps_data/flipsay/trace.log` on every start (startup steps, radio open, first sweep, every 40th sweep). If the app crashes, the last line shows roughly where. Attach the file.
2. **Live log:** connect the Flipper to a PC, close qFlipper, run `ufbt cli`, then type `log debug` and start the app. Lines tagged `FlipSay` are from this app.
3. **Crash screen:** note the exact message and, if shown, the thread name.
4. Say which radio is attached (internal only, external CC1101, NRF24) and which source is selected.

## Common problems

**Buttons feel unresponsive.** Button events go through a low-priority system timer thread. The radio worker sleeps briefly during sweeps so that thread can run; a very busy CPU can still delay presses.

**"Low memory" messages / out-of-memory.**
- Turn on *Options > System > Lite Mode* (smaller waterfall, no persistence view).
- Check *Options > System* for free memory and stack headroom.
- Auto Decode needs roughly 36 KB of free memory; Persistence needs about 6 KB.
- Close other apps / reboot the Flipper to free heap.

**No radio found.**
- External CC1101: check wiring and the CS setting in *Options > Radio & Pins*. The module must be wired before the app starts or the source changed (which restarts the radio).
- NRF24: needs 3.3 V and a decoupling capacitor close to the module on many clones.
- Using both at once needs different CS pins, see [WIRING.md](WIRING.md).

**Auto Decode finds nothing.**
- It listens on common remote frequencies and the strongest peak. Hold the button on the remote for about a second.
- Try a higher sensitivity (*Options > Auto Decode > Decode Sens*).
- Rolling-code protocols need the key files in `/ext/subghz/assets/`.
- Weather/sensor/TPMS protocols are deliberately ignored.

**SDR Mode stays on "Waiting for USB to connect".**
- Plug in a data USB cable (not charge-only) and close qFlipper and serial terminals. The Flipper re-enumerates its USB port when SDR Mode starts, so Windows may take a few seconds to show the new serial ports.

**SDR Mode stays on "Connecting".**
- Start `python tools/flipsay_sdr_bridge.py`. If it cannot find the port, run it with `--list` and then `--port COMx`. The Flipper has two serial ports in SDR Mode, the bridge talks to the second one.
- Check `pip install -r tools/requirements.txt` finished without errors.

**GQRX shows only a flat line.**
- The CC1101 only receives 300-348, 387-464 and 779-928 MHz. Tune inside one of them.
- Use `--demo` to check GQRX and the bridge without the Flipper.

**GQRX has no audio.**
- Expected. SDR Mode provides a spectrum and waterfall only, not demodulation.

**Wi-Fi window says NO RESPONSE or NOT MARAUDER FW.**
- See [WIFI_MODULE.md](WIFI_MODULE.md). The devboard must run ESP32 Marauder firmware and must not be in use by another app.
