# Connect FlipSay to SDR++ (Windows)

This guide shows how to see the FlipSay spectrum and waterfall in [SDR++](https://github.com/AlexandreRouma/SDRPlusPlus) on Windows. GQRX has no Windows version, and SDR++ can read the same **rtl_tcp** stream, so it works with FlipSay's PC bridge.

> **What to expect:** SDR++ shows a spectrum and waterfall of what the Flipper's CC1101 measures, and tuning in SDR++ retunes the Flipper. It is **not a real SDR**: there is no IQ and no audio, so you cannot listen to or demodulate a signal. See [SDR_MODE.md](SDR_MODE.md) for the details.

## What you need

- A Flipper Zero with FlipSay **0.2.1 or newer** and an internal or external CC1101 (an NRF24 alone does not work for SDR Mode).
- A **data** USB cable (not charge-only).
- A Windows PC with Python 3.9 or newer.

## 1. Install the pieces (once)

**Python.** Install it from [python.org](https://www.python.org/downloads/) and tick *Add python.exe to PATH* in the installer. Check it in PowerShell:

```
python --version
```

**FlipSay bridge files.** Download the repository ([Code > Download ZIP](https://github.com/itsc0c0/flipsay)) and unzip it, or clone it. You need the `tools` folder from it.

**Python packages.** In PowerShell, inside the unzipped folder:

```
pip install -r tools\requirements.txt
```

If `pip` is not found, use `python -m pip install -r tools\requirements.txt`. If you have several Python versions, run the bridge with the same `python` that you installed the packages into.

**SDR++.** Download `sdrpp_windows_x64.zip` from the [SDR++ releases](https://github.com/AlexandreRouma/SDRPlusPlus/releases) (the *nightly* release has the Windows build) and unzip it anywhere. There is no installer: run `sdrpp.exe` from the folder. The build includes the **RTL-TCP** source module that this guide uses.

## 2. Test the bridge without the Flipper (optional, recommended)

This checks that SDR++ and the bridge work together before you involve the Flipper.

1. In PowerShell, in the folder that contains `tools`:
   ```
   python tools\flipsay_sdr_bridge.py --demo
   ```
   It prints `listening on port 1234`.
2. Do step 4 below (SDR++ settings) and tune to **433.92 MHz**. You should see a strong fake signal in the middle, and smaller ones near 315, 868 and 915 MHz when you tune there.
3. Stop the bridge with `Ctrl+C` when you are done.

## 3. Start SDR Mode on the Flipper

1. Close **qFlipper** and any serial terminal that is connected to the Flipper.
2. On the Flipper open FlipSay, press **OK** for the options, and choose **SDR Mode (GQRX)**. The screen goes black.
3. The text follows what happens:

   | Flipper says | Meaning |
   |---|---|
   | Waiting for USB to connect | No USB host yet. Plug the cable into the PC. |
   | Connecting | USB is up, but the bridge has not answered yet. |
   | Connected | The bridge is talking to the Flipper. |

   A school bus drives along the bottom the whole time.
4. While it says *Connecting*, start the bridge on the PC (without `--demo`):
   ```
   python tools\flipsay_sdr_bridge.py
   ```
   It finds the Flipper's serial port by itself and prints `[flipper] connected on COMx`. The Flipper then shows **Connected**.

If the bridge cannot find the port, list the ports and name the right one:

```
python tools\flipsay_sdr_bridge.py --list
python tools\flipsay_sdr_bridge.py --port COM7
```

In SDR Mode the Flipper shows two serial ports in Windows. The bridge uses the second one, and it probes them for you.

## 4. Connect SDR++

Do this with the bridge already running.

1. Start `sdrpp.exe`.
2. In the **Source** panel on the left, choose **RTL-TCP**. (Not "RTL-SDR": that is for a USB dongle.)
3. Enter:
   - **Hostname:** `127.0.0.1`
   - **Port:** `1234`
   - **Sample rate:** `2.4 MSPS` (any value from about 250 kHz to 3.2 MHz works; see below)
4. Press the **Play** button (the triangle at the top left). SDR++ connects only when you press Play, not when you pick the source.
5. Check the bridge console. It should now print:
   ```
   [rtl_tcp] client 127.0.0.1:xxxxx
   [gqrx] sample rate 2.400 MS/s -> span 2400 kHz
   [gqrx] frequency 433.9200 MHz
   ```
   (The `[gqrx]` tag is only the name of the log line. SDR++ causes it too.)
6. The spectrum and waterfall appear. The Flipper screen shows the frequency, span, gain, frames per second and a frame counter.

## 5. Use it

- **Frequency:** change it in SDR++ (type it in the frequency display, or click the digits). The Flipper retunes its sweep around it.
- **Sample rate = span:** the width of the spectrum is the sample rate you chose. A wider span covers more band but each sweep takes longer. 2 to 2.4 MSPS is a good start.
- **Gain:** the SDR++ gain slider sets the CC1101 LNA gain.
- **Band limits:** the CC1101 only receives about **300-348, 387-464 and 779-928 MHz**. Anywhere else you see a flat line.
- **Speed:** the Flipper sweeps about 5 to 10 times per second, so the waterfall moves slowly compared with a real SDR.
- **Levels:** the levels in SDR++ are relative, not calibrated dBm. If the trace is hard to see, adjust the FFT min/max range and the waterfall min/max in SDR++.
- **Try a real signal:** tune to 433.92 MHz and press a remote near the Flipper. You should see a bump appear.

## 6. Stop

1. Press **Back** on the Flipper to leave SDR Mode. It switches its USB port back to the normal single serial port, so qFlipper works again.
2. Stop the bridge with `Ctrl+C`.

## Troubleshooting

**The Flipper stays on "Waiting for USB to connect".**
Use a data cable, close qFlipper, and give Windows a few seconds to detect the new serial ports.

**The Flipper stays on "Connecting".**
The bridge is not running or cannot find the port. Start it, or use `--list` and `--port COMx`. Check that `pip install -r tools\requirements.txt` finished without errors.

**The bridge says Connected, but SDR++ shows nothing.**
There are two separate links: Flipper to bridge (USB), and SDR++ to bridge (network). "Connected" on the Flipper only covers the first one. Look in the bridge console for `[rtl_tcp] client ...`. If it is missing, SDR++ has not connected: check that the source is **RTL-TCP** (not RTL-SDR), host `127.0.0.1`, port `1234`, and that you pressed **Play**. If you changed the port with `--rtl-port`, use the same number in SDR++.

**SDR++ is connected but the screen is flat.**
Tune inside a CC1101 band (for example 433.92 MHz), and use `--demo` to check that SDR++ itself is fine. A real signal only appears while something is transmitting.

**There is no sound.**
Expected. SDR Mode provides spectrum and waterfall only.

**Windows Firewall asks about Python.**
The bridge listens on `127.0.0.1` only, so you can allow it for private networks or cancel; SDR++ on the same PC should still connect.

More: [SDR_MODE.md](SDR_MODE.md) (limits and the serial protocol) and [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
