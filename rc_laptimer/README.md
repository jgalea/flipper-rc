# RC Lap Timer

Flipper Zero app that times laps for RC cars. An IR break-beam sensor on a GPIO pin registers each pass; the screen shows the running lap, last lap, best lap, lap count and the three most recent laps.

## Wiring

Use a digital break-beam or obstacle sensor module whose output goes low when the beam is broken.

- Sensor signal to pin C3
- Sensor ground to GND
- Sensor power from the 3V3 pin, not 5V, unless the module is documented as 3.3V logic when powered from 5V

The pin is configured as an input with pull-up and counts falling edges.

## Buttons

- OK: start the session, then record a lap (works without a sensor)
- Left / Right: minimum lap time, 0.5 s to 5 s in 0.5 s steps (default 1.0 s)
- Up: reset the session
- Back: exit

The first beam break or OK press starts lap 0; no lap is recorded for it. Every later trigger records a lap, unless it arrives sooner than the minimum lap time after the previous one. Times show as seconds with two decimals, or m:ss.cc past a minute. The backlight stays on while the app runs.

## Build and install

Requires [ufbt](https://github.com/flipperdevices/flipperzero-ufbt).

    ufbt

The app is written to `dist/rc_laptimer.fap`. Copy it to `apps/GPIO/` on the SD card, or install it with `ufbt launch` while the Flipper is connected.
