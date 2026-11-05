# RC Parking

Parking-skills game for RC cars. Tags on the floor mark zones A to D. The app hands out a sequence of manoeuvres, the driver completes each one and holds the Flipper (or a tag fixed to the car) against the tag of the target zone to register the step.

## Supported tags

- 125 kHz LF RFID: any protocol the Flipper LF reader decodes (EM4100, HID, Indala, and others).
- 13.56 MHz NFC: the UID of ISO 14443-A and -B, ISO 15693, FeliCa and ST25TB tags.

While waiting for a tag the app alternates between the LF reader and the NFC reader, so either type can be mixed across zones.

## Menu

- Zone game: tags on the floor mark zones A to D.
- Bay game: tags on the car front and rear, the Flipper fixed at a bay.
- Learn zone tags
- Learn car tags

## Learn zone tags

For zones A, B, C and D in turn, scan the tag for that zone. Each tag can only be assigned to one zone; a repeat scan is rejected. The tags are saved to `/ext/apps_data/rc_parking/zones.txt`.

## Zone game

A round has 4 steps. Each step shows a manoeuvre and the zone to finish in; consecutive steps never use the same zone. A running timer, the step number and the miss count are shown on screen.

Scanning the right zone tag completes the step with a green blink and vibration. Any other tag counts as a miss, shows briefly in red, and adds 5 seconds to the total.

After the fourth step the result screen shows the time, the penalties, the total, the best of the session and the all-time best (saved in `/ext/apps_data/rc_parking/best.txt`). Press OK to play again.

## Bay game setup

Stick one tag to the front of the car and another to the rear with Blu-Tack. Put the Flipper at the parking bay, propped against a cone or wall with its back facing where the car's bumper will arrive. The reader works at a few centimetres, so place each tag on the bumper edge, where it ends up closest to the Flipper.

## Learn car tags

Scan the FRONT tag, then the REAR tag. Using the same tag for both is rejected. The tags are saved to `/ext/apps_data/rc_parking/cars.txt`.

## Bay game

A round has 4 steps drawn from a list of bay challenges. Each challenge names the car end that must reach the Flipper, FRONT or REAR, for example "Reverse into the bay" (REAR) or "Nose into the bay" (FRONT). The screen shows the step number, the challenge, the required end, a running timer and the miss count.

Press OK to start the timer. The Flipper reads continuously, with no button press during the round:

- The tag of the required end completes the step with a green blink and vibration. A 3 second "Drive away" screen follows, the timer is paused, and the next step starts when it ends.
- The tag of the other end counts as a miss, shows briefly in red, and adds 5 seconds to the total.
- Tags that are not the car's are ignored, and repeat reads of the same tag within about 1.5 seconds are ignored.

After the fourth step the result screen shows the time, the penalties, the total, the best of the session and the all-time best (saved in `/ext/apps_data/rc_parking/bay_best.txt`, separate from the zone game). Press OK to play again.

Back returns to the menu, and Back from the menu exits.

## Build and install

```
cd rc_parking
ufbt
```

The result is `dist/rc_parking.fap`. Copy it to `/ext/apps/Tools/` on the SD card, or install with `ufbt launch`.
