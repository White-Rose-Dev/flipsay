# Wi-Fi module (ESP32 Marauder)

FlipSay can show data from the **official Flipper Zero Wi-Fi Devboard** inside a Multi Use window. The board must run **ESP32 Marauder** firmware. With any other firmware (for example the stock Blackmagic firmware) the window does not show data and tells you to update.

FlipSay only sends passive commands: `scanap`, `sniffbeacon`, `sniffdeauth` and `sniffpmkid`, plus `stopscan`. It never sends deauth, spam or attack commands.

## Setup

1. Flash Marauder on the devboard (see the [ESP32 Marauder wiki](https://github.com/justcallmekoko/ESP32Marauder/wiki)).
2. Plug the devboard into the Flipper's GPIO header.
3. In FlipSay enable Parallel Gather, set **PG Mode** to **Multi Use**.
4. Under **Options > Multi Use** pick a layout and set one window to **WIFI**. In the 4-window grid it can sit next to EXT CC, INT CC and NRF24.

The app takes over the Flipper's UART for the Wi-Fi board: it pauses the Expansion Modules service, enables 5 V on the header (if *5V OTG* is on) and gives everything back when you leave Multi Use or exit. The serial channel is the one chosen in Momentum's settings for the ESP module. Do not run another app that uses the same UART (for example the Marauder app) at the same time.

## What the window shows

| View | Contents |
|---|---|
| **APS** | Access points sorted by signal: name, channel, RSSI, bar |
| **CHAN** | Bars for channels 1-13, height = strongest AP on that channel |
| **RATE** | Lines received per second over the last 32 seconds, with the current rate |
| **LOG** | The last lines the board sent |

With the window focused: **Left / Right** changes the view, **Up / Down** changes the capture mode (also under *Multi Use > Wi-Fi Mode*):

| Mode | Command |
|---|---|
| SCAN AP | `scanap` |
| BEACON | `sniffbeacon` |
| DEAUTH | `sniffdeauth` (listens for deauth frames) |
| PMKID | `sniffpmkid` |

## Window messages

| Message | Meaning |
|---|---|
| STARTING... | Opening the UART, asking the board for `info` |
| NO RESPONSE | The board sent nothing: check seating, power and that firmware is running |
| NOT MARAUDER FW | The board answered but is not Marauder. Update it to Marauder firmware |
| UART IN USE | Another service holds the UART |
| LISTENING... | Marauder is running, nothing parsed yet |

Detection: FlipSay sends `info` and looks for "Marauder" in the answer (and the version). If that is missing it sends `help` and accepts the board if the list contains Marauder commands.

## Status of testing

The Marauder output format is not documented in detail and differs a little between versions, so the line parser is tolerant (it looks for `RSSI:`, `Ch:`, `BSSID:` and `ESSID:` fields and the `[index][CH:n] name rssi` list format). This was written from the protocol description and has **not** been tested on a real devboard yet. If a window stays empty on a working board, attach the LOG view lines to an issue.
