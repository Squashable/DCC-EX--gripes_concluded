# Controls and on-screen indicators

## Normal (DCC) mode

| Control | Action |
|---|---|
| Encoder 1 / 2 (turn) | Speed for engine 1 / 2 |
| Encoder 1 / 2 (push) | Toggle direction for engine 1 / 2 |
| SEL1 / SEL2 | Send keypad input to engine 1 / 2 (focused engine shows `>` and a thick border) |
| Keypad digits + `#` | Toggle that function number (0-28) on the focused engine |
| `*` | Start/finish loco address entry; type up to 4 digits then `#` to save |
| E-STOP 1 / 2 | Emergency stop engine 1 / 2 |
| DC switch | Throw to DC for DC mode (below) |
| Hold SEL1 + SEL2 for 1.5 s | Enter/exit debug mode |

## DC mode (DC switch thrown)

A single DC loco with a basic screen. The two DCC engines are stopped on entry and left alone until the switch is thrown back.

| Control | Action |
|---|---|
| Encoder 1 (turn) | DC speed |
| Encoder 1 (push) | DC direction |
| Either E-STOP | Emergency stop the DC loco |
| Everything else | Ignored |

The DC track letter and cab id are `DC_TRACK_LETTER` and `DC_CAB_ID` at the top of `handheld_main/dcc.cpp`.

## Status indicators

- Filled dot, top right: link to the CSB1 is up. Hollow dot: link is down.
- `NO LINK - CONTROLS NOT SENT`: while the link is down, throttle, direction and function input is ignored so the screen matches what the layout is doing. The loco keeps running at its last speed.
- Small filled square, top left (normal mode): the DC switch input looks faulty (chattering).
- Inverted banner on an engine row: that engine's E-stop, select button, encoder button or encoder sensor looks faulty. A faulty input is ignored, except an E-stop, which fails safe and stops the engine.

## Debug mode

Shows the live state of every button, switch and encoder plus the last key pressed. Nothing is forwarded to the layout except E-stop.
