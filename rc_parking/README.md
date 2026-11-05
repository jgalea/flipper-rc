# RC Parking

Parking-skills game for RC cars. Tags on the floor mark zones A to D. The app hands out a sequence of manoeuvres, the driver completes each one and holds the Flipper (or a tag fixed to the car) against the tag of the target zone to register the step.

## Supported tags

- 125 kHz LF RFID: any protocol the Flipper LF reader decodes (EM4100, HID, Indala, and others).
- 13.56 MHz NFC: the UID of ISO 14443-A and -B, ISO 15693, FeliCa and ST25TB tags.

While waiting for a tag the app alternates between the LF reader and the NFC reader, so either type can be mixed across zones.

## Learn zone tags

Open "Learn zone tags" from the menu. For zones A, B, C and D in turn, scan the tag for that zone. Each tag can only be assigned to one zone; a repeat scan is rejected. The tags are saved to `/ext/apps_data/rc_parking/zones.txt`.

## Play

Choose "Play". A round has 4 steps. Each step shows a manoeuvre and the zone to finish in; consecutive steps never use the same zone. A running timer, the step number and the miss count are shown on screen.

Scanning the right zone tag completes the step with a green blink and vibration. Any other tag counts as a miss, shows briefly in red, and adds 5 seconds to the total.

After the fourth step the result screen shows the time, the penalties, the total, the best of the session and the all-time best (saved in `/ext/apps_data/rc_parking/best.txt`). Press OK to play again. Back returns to the menu, and Back from the menu exits.

## Build and install

```
cd rc_parking
ufbt
```

The result is `dist/rc_parking.fap`. Copy it to `/ext/apps/Tools/` on the SD card, or install with `ufbt launch`.
