<h1 align="center">Flipper RC</h1>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/LICENSE-MIT-5C9E31?style=for-the-badge" alt="License"></a>
  <a href="https://rebelcode.com"><img src="https://img.shields.io/badge/BUILT%20BY-REBELCODE-8A2BE2?style=for-the-badge" alt="Built by"></a>
</p>

<p align="center"><b>Flipper Zero apps for RC car days: a phone shutter, a lap timer, a parking game and a lights remote.</b></p>

## Apps

- `rc_shutter`: Bluetooth remote that triggers the phone camera. OK takes the shot; Up and Down change volume.
- `rc_laptimer`: lap timer for an IR break-beam sensor on pin C3, with a manual lap button.
- `rc_parking`: parking-skills game for kids. Random manoeuvres, with 125 kHz and 13.56 MHz tags on the floor as checkpoints.
- `rc_lights`: Bluetooth keyboard that sends F13 to F20 to run light commands on a computer.

Each folder has its own README.

## Build

Install [ufbt](https://github.com/flipperdevices/flipperzero-ufbt), then run `ufbt` inside an app folder. The result is in `dist/`.
