# SDR Mode (GQRX)

SDR Mode turns FlipSay into a spectrum source for any **rtl_tcp** client: [GQRX](https://gqrx.dk) on Linux and macOS, and [SDR++](https://github.com/AlexandreRouma/SDRPlusPlus) on Windows (GQRX has no Windows build). Read the limits first, because this is not what a real SDR does.

## What you get, and what you do not

| You get | You do not get |
|---|---|
| The spectrum and waterfall of the band GQRX is tuned to | Audio or demodulation (AM, FM, ...) |
| GQRX frequency control (the Flipper retunes its sweep) | Real IQ samples |
| GQRX sample rate = the width of the sweep (up to 12 MHz) | A fast waterfall (the sweep is 5-10 per second) |
| GQRX gain slider drives the CC1101 LNA gain | dBm-accurate levels in GQRX (they are relative) |

Why: the Flipper's CC1101 reports signal strength (RSSI) for one frequency at a time. FlipSay steps through the band, sends the 128-point spectrum over USB, and the PC script turns that spectrum into a **synthetic** IQ stream (random phase, matching the measured levels) and serves it over the rtl_tcp protocol. GQRX's own FFT then draws the same spectrum back. A real signal is therefore visible as a bump in the spectrum and waterfall, but there is nothing to demodulate.

The CC1101 only receives 300-348, 387-464 and 779-928 MHz. Outside those bands the sweep reads nothing and GQRX shows a flat floor.

SDR Mode needs the internal or the external CC1101. With only an NRF24 the Flipper shows "NO CC1101".

## Setup

PC (Windows, Linux or macOS), Python 3.9 or newer:

```
pip install -r tools/requirements.txt
```

1. Close qFlipper and any serial terminal that is connected to the Flipper.
2. On the Flipper open **Options > SDR Mode**. The screen goes black.
3. The Flipper moves its USB port to a dual serial configuration (the second serial port is the SDR link). The state text then follows what happens:
   - **WAITING FOR USB TO CONNECT**: no USB host yet. Plug in the cable.
   - **CONNECTING**: USB is up but the bridge script has not answered.
   - **CONNECTED**: the bridge answered. Frequency, span, gain, frames per second and a frame counter are shown.
   A school bus drives left to right and back along the bottom in every state.
4. Run the bridge:
   ```
   python tools/flipsay_sdr_bridge.py
   ```
   It probes every serial port and picks the one that answers FlipSay's handshake. If that fails use `--list` and `--port COM7` (or `/dev/ttyACM1`).
5. **Windows (SDR++):** unzip the Windows build, start `sdrpp.exe`, pick the source **RTL-TCP**, enter host `127.0.0.1` and port `1234`, set the sample rate to 2.4 MSPS and press the play button. Tune with the frequency display; the Flipper follows.
   - Device string: `rtl_tcp=127.0.0.1:1234`
   - Input rate: `2400000` (anything from about 250000 to 3200000 works; the sweep span follows it)
   - Decimation: none
6. Start GQRX and tune. Changing frequency, input rate or gain makes the Flipper retune and the next sweep appears within a fraction of a second.
7. Press **Back** on the Flipper to leave. The USB configuration is restored to the normal single serial port.

## Bridge options

```
python tools/flipsay_sdr_bridge.py --port COM7        # fixed port
python tools/flipsay_sdr_bridge.py --rtl-port 1235    # other TCP port
python tools/flipsay_sdr_bridge.py --listen 0.0.0.0   # reachable from another PC
python tools/flipsay_sdr_bridge.py --demo             # fake spectrum, no Flipper
python tools/flipsay_sdr_bridge.py --selftest         # offline test of the rtl_tcp side
python tools/flipsay_sdr_bridge.py --list             # list serial ports
```

`--demo` is handy to check GQRX with the bridge before involving the Flipper: it shows a noise floor and a few fixed signals around 315, 344, 433.4, 433.92, 868.35 and 915 MHz.

## How the link works

Serial frames on USB CDC channel 1: `'F' 'S' type length payload xor` (xor of type, length and payload).

| Direction | Type | Payload |
|---|---|---|
| PC to Flipper | `0x10` HELLO | none |
| | `0x11` TUNE | center kHz (u32 LE), span kHz (u32 LE) |
| | `0x12` GAIN | 0-100, or 255 for auto |
| | `0x13` PING | none (keep-alive, needed at least every 4 s) |
| | `0x14` BYE | none |
| Flipper to PC | `0x01` HELLO_ACK | protocol, radio, bins |
| | `0x02` FRAME | center kHz (u32), span kHz (u32), bins (u8), seq (u8), sweep ms (u16), levels (128 bytes, 0-255 = -120 to -20 dBm) |

The sweep covers `span = GQRX input rate` around the tuned frequency, 128 points, using the same receiver setup as the Spectrum view.

## Status of testing

The bridge's rtl_tcp server was tested offline with `--selftest` (header, tuning commands, signal position in the generated IQ), and its Flipper protocol against a software imitation of the Flipper. The USB side on a real Flipper and a real GQRX have **not** been tested yet. Reports are welcome.
