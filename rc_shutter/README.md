# RC Shutter

A Flipper Zero app that turns the Flipper into a Bluetooth LE camera shutter for a phone.

It advertises as a Bluetooth HID device named "Shutter" plus the Flipper name. Once paired, the phone camera treats a Volume Up key press as the shutter button.

## Controls

- OK: Volume Up press and release (shutter), increments the Shots counter
- Up: Volume Up
- Down: Volume Down
- Back: exit and restore the default Bluetooth profile

The screen shows the connection state (Advertising or Connected), the shot count and a key hint.

## Pairing

1. Start RC Shutter on the Flipper.
2. Open Bluetooth settings on the phone and pick the "Shutter ..." device.
3. Confirm the pairing code on both devices.
4. Open the phone camera and press OK on the Flipper.

## Build and install

Requires [ufbt](https://github.com/flipperdevices/flipperzero-ufbt).

    ufbt                # builds dist/rc_shutter.fap
    ufbt launch         # builds, installs and runs on a connected Flipper

Or copy `dist/rc_shutter.fap` to `apps/Bluetooth/` on the SD card.
