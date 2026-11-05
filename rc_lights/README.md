# RC Lights

A Flipper Zero Bluetooth keyboard with eight light shortcuts. It sends F13 to F20, and a small rule on the computer turns each key into a command for Govee lamps.

| Flipper | Key | Action |
| --- | --- | --- |
| Up | F13 | On |
| Down | F14 | Off |
| Left | F15 | Dim (20%) |
| Right | F16 | Bright (100%) |
| OK | F17 | Warm white (2700K) |
| Hold Up | F18 | Red |
| Hold Down | F19 | Green |
| Hold Left | F20 | Blue |

## Setup

1. Group the lamps once: `govee group "Lamps" "Lamp 1" "Lamp 2"` (uses the `govee` CLI from govee-mcp).
2. Install the app and open it. The Flipper advertises as "Lights ..."; pair it in the computer's Bluetooth settings.
3. In Karabiner-Elements, copy `mac/karabiner-rc-lights.json` into `~/.config/karabiner/assets/complex_modifications/` and enable the rule under Complex Modifications > Add predefined rule.

Change the commands in the JSON to drive anything else a shell can reach.

## Build

```
ufbt
```

The app is at `dist/rc_lights.fap`. `ufbt launch` runs it on a connected Flipper.
