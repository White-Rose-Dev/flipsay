# Wiring

Pins on the Flipper Zero GPIO header used by FlipSay:

| Pin | Signal | CC1101 module | NRF24L01+ module |
|---|---|---|---|
| 2 | MOSI (PA7) | SI | MOSI |
| 3 | MISO (PA6) | SO | MISO |
| 4 | CS-A (PA4) | CSN | CSN |
| 5 | SCK (PB3) | SCK | SCK |
| 6 | PB2 | GDO0 | CE |
| 7 | CS-B (PC3) | CSN | CSN |
| 8 | GND | GND | GND |
| 9 | 3V3 | VCC | VCC |

Use 3.3 V for the modules unless yours has an on-board regulator for 5 V. Check the pinout of your specific board before powering it.

## One module

Wire the module to the table above with CS on pin 4. In the app, leave *Options > Radio & Pins > CC1101 CS / NRF24 CS* on `PIN 4`.

## CC1101 and NRF24 at the same time

They share MOSI, MISO, SCK and pin 6, but they need **different CS pins**:

- CC1101 CSN to pin 4, NRF24 CSN to pin 7 (or the other way around).
- In the app set *CC1101 CS* and *NRF24 CS* accordingly, then the app restarts the radio.
- During sweeps the CC1101's GDO0 line is set to high-impedance so it does not disturb the NRF24's CE line on pin 6.

## Notes

- *Options > Radio & Pins > Pin Layout* shows this table on the device.
- The app changes the SPI-handle settings only while it is running and restores your Momentum settings on exit.
- Parallel Gather with a second radio adds current draw and heat. Read the in-app warnings first.
