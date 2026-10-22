# TTX: transmit a .sub file

TTX plays back a Sub-GHz `.sub` file that you choose, through the internal CC1101, the external CC1101, or **both at the same time on two antennas** (each on its own frequency). It uses the firmware's own transmit path, the same one as the stock Sub-GHz app, and it follows the firmware's own limits.

> **Read this first.** Transmitting is regulated. Which frequencies, powers and uses are allowed depends on your country. Only send signals that you own or are allowed to send, only on frequencies that are legal where you are, and never to interfere with other people's equipment. You are responsible for what you transmit. TTX shows a warning every time you open it.

## What TTX does and does not do

| It does | It does not |
|---|---|
| Send a `.sub` file you pick (RAW captures and protocol key files) | Generate noise, carrier-only or "jamming" signals |
| Use INT only, EXT only, or both antennas (DUAL) | Transmit by itself, or in the background |
| Send on a different frequency per antenna | Override the firmware's region or band limits |
| Stop the moment you let go of OK or press Back | Run longer than a fixed safety limit |
| Show per-antenna status (ready, blocked, no radio) | Break rolling codes (a replayed rolling-code signal is generally already used) |

The firmware's transmit check decides what is allowed. If a frequency is blocked you see why on screen (region, band not enabled, unsupported). Changing those firmware settings is up to you and the local rules; TTX has no option to bypass them.

## How to use it

1. **Options > TTX (Transmit)**. Read the warning and **hold OK** to continue.
2. **Pick a file.** The browser starts in `/ext/subghz` (the stock folder, with FlipSay's auto-decoded keys in `FlipSay/`). OK opens a folder or file, Back goes up, Up/Down scroll (long folders page).
3. **Set up the transmission:**

   | Row | Left / Right |
   |---|---|
   | MODE | INT, EXT or DUAL |
   | A INT | frequency of the internal radio (10 kHz per press, 100 kHz while held). OK resets it to the file's frequency |
   | B EXT | frequency of the external radio, same keys |
   | PASSES | 1, 2 or 3 passes of the file |

   Each antenna shows its status: `READY`, `NO RADIO`, `BUS BUSY`, `REGION`, `BAND OFF` or `UNSUPP`.
4. **Hold OK to transmit.** Keep holding for the whole transmission. Letting go, or pressing Back, stops it. There is also a hard limit of 20 seconds per press.
5. When it ends the screen shows the result. In DUAL mode it also shows how many microseconds apart the two antennas were started.

## Two antennas, one file (DUAL)

- A is the **internal** CC1101 and B is the **external** CC1101. They have separate timers and DMA channels in the firmware, so they can transmit in parallel.
- Both get their own copy of the file, their own frequency and the same preset (modulation) from the file. The same file can therefore go out on, for example, 433.92 MHz on A and 868.35 MHz on B.
- Both antennas are prepared first, then started back to back. They are **not phase-locked**: the start offset is measured on the device and shown after the send, and it is small but not zero.
- The external CC1101 must be wired and detected (see [WIRING.md](WIRING.md)). If the NRF24 is selected as the source it holds the external bus, so the external CC1101 cannot be used until you change the source.
- On Unleashed and RogueMaster builds the external CC1101 uses the standard pin 4 chip select.

## Supported files

- `Filetype: Flipper SubGhz ...` files with `Frequency`, `Preset` and `Protocol`.
- `Protocol: RAW` files, and key files for protocols that have an encoder in your firmware. If your firmware has no encoder for the protocol you get "NO ENCODER FOR PROTOCOL".
- Standard presets and `FuriHalSubGhzPresetCustom` with a valid `Custom_preset_data`. A malformed custom preset is refused.

## Messages

| Message | Meaning |
|---|---|
| BLOCKED BY REGION / BAND NOT ENABLED / FREQ UNSUPPORTED | The firmware does not allow transmitting on that frequency |
| RADIO NOT FOUND | That CC1101 is not available (for EXT: wiring or detection) |
| EXT BUS BUSY (NRF) | The NRF24 is holding the external SPI bus |
| BAD OR UNKNOWN PRESET | The file's preset is not understood |
| NO ENCODER FOR PROTOCOL | The firmware cannot send this protocol |
| FILE DATA INVALID / FILE READ ERROR | The file is damaged or incomplete |
| TX REFUSED BY FIRMWARE | The firmware's own transmit start failed |
| STOPPED / STOPPED EARLY | You released OK or pressed Back |
| LOW MEMORY | Not enough free memory to prepare the transmission |

## Status of testing

TTX compiles and passes the app symbol check against the Momentum and the Unleashed SDKs, and it follows the same call sequence as the stock Sub-GHz app. It has **not** been tested on a real Flipper yet, and DUAL in particular is new. Please report what you see, with `/ext/apps_data/flipsay/trace.log` if something misbehaves.
