# DCC-EX Dual Handheld Controller — User Manual

Last updated 2026-10-11. A shorter controls summary is in [CONTROLS.md](CONTROLS.md).

## 1. Overview

This handheld drives two DCC locos at once, or one DC loco, over Wi-Fi from a DCC-EX EX-CSB1 base station. Each engine has its own speed knob, and one keypad serves both.

- **Two DCC engines**, one rotary encoder each, adjustable at the same time.
- **One keypad** that acts on whichever engine is selected: functions F0 to F28, the handheld address, and writing a new address into a loco.
- **Two emergency-stop buttons**, one per engine.
- **A DC switch** that turns the handheld into a single-engine DC throttle with a simple screen.
- **Self-checking inputs**: a chattering button or noisy encoder is flagged on screen instead of being trusted.

Out of the box the engines are set to addresses 4401 (engine 1) and 8302 (engine 2). Both can be changed from the keypad.

## 2. Controls reference

Every physical control and what it does in each mode. Anything marked unused is deliberately ignored in that mode.

| Control | DCC mode | DC mode |
| --- | --- | --- |
| Encoder 1, turn | Speed of engine 1 | Speed of the DC loco |
| Encoder 1, push | Toggle direction of engine 1 | Toggle direction of the DC loco |
| Encoder 2, turn and push | Speed and direction of engine 2 | Unused |
| SEL1 / SEL2 | Select that engine. Click the selected side again to type a new address for it; click once more to cancel | Unused |
| Keypad 0 to 9 | Digits for a function number (2 digits) or an address (4 digits) | Unused |
| `#` | Toggle the typed function, or save the typed address | Unused |
| `*` | Start or cancel writing a new address into the loco (programming track) | Unused |
| E-STOP 1 / 2 | Emergency stop engine 1 / engine 2 | Either button stops the DC loco |
| DC switch | DCC position: normal two-engine mode | DC position: single DC loco |
| Hold SEL1 + SEL2 for 1.5 s | Enter or leave debug mode | Enter or leave debug mode |

Each count from an encoder moves the speed by 2 of the 126 speed steps. How many counts a single click produces depends on the encoder you fitted.

## 3. Powering on and connecting

On power-up the screen shows `Connecting to CSB1...` while the handheld joins the CSB1's Wi-Fi network (default name `DCCEX_CSB1`, waiting up to 15 seconds) and opens a connection to 192.168.4.1 on port 2560.

1. **Track power.** The first successful connection after power-up switches track power on. Reconnecting later does not, so it never overrides someone who cut power on purpose.
2. **Link dot.** The dot at the top right is filled while the handheld is connected to the CSB1 and hollow when it is not.
3. **If the link drops.** The top strip reads `NO LINK - CONTROLS NOT SENT`. Throttle, direction and function input is ignored, so the screen always matches what the layout is really doing. The locos keep running at their last speed, and the E-stop buttons cannot reach the layout until the link returns.
4. **Reconnecting.** The handheld retries on its own: Wi-Fi every 10 seconds, the CSB1 connection every 3 seconds. Nothing needs pressing.

Stay in range of the CSB1, and keep the layout's own stop control within reach in case the link is lost while a loco is moving.

## 4. DCC mode

With the DC switch in the DCC position, each half of the screen belongs to one engine and each engine has its own knob.

### Reading the screen

- **Top strip:** the link dot on the right, and a small filled square on the left if the DC switch input looks faulty.
- **Line 1 of each engine:** `>E1:#4401` is the engine number and its address; the `>` marks the selected engine, which also gets a thicker border. Next come `[FWD]` or `[REV]`, a speed bar, and the speed as a percentage.
- **Line 2 of each engine:** the functions that are on (for example `F:0 3`), or `F:NONE`. While you type, this line shows what you are entering, and fault or programming messages replace it when they apply.

### Driving

- Turn a knob to change that engine's speed. Both knobs work at the same time.
- Push a knob to flip that engine's direction. The change is sent immediately, so slow the loco down first.

### Selecting an engine

Press SEL1 or SEL2. The keypad now acts on that engine.

### Changing the handheld address

This changes which loco the handheld is driving. It does not change anything inside the loco.

1. Click the select button of the engine that is already selected.
2. The line shows `>E1:#_`. Type up to 4 digits.
3. Press `#` to save. The engine is stopped and its function list is cleared.
4. To cancel, click the select button again.

### Functions (sound, lights)

Type the function number (0 to 28, up to 2 digits) and press `#` to toggle it on the selected engine. The handheld keeps its own record of which functions are on, because it cannot read them back from the decoder. If another throttle changes a function, or you change the address, the list can drift from the loco and a toggle may seem inverted.

## 5. Programming the address stored in the loco

The `*` key writes a new address into the loco's own decoder, using the CSB1's programming track. This is separate from the handheld address in section 4.

**Before you start:** the CSB1 needs a track set to programming mode (PROG), and the loco must be alone on that track. Check the track modes with `<=>` on the CSB1 serial console if unsure.

1. Put the loco on the programming track.
2. Press `*`. The selected engine's second line shows `PROG ADDR:[_]`.
3. Type the new address, up to 4 digits.
4. Press `#` to write it. Press `*` instead to cancel.
5. Wait. The screen shows `WRITING #n TO LOCO`, then `VERIFYING #n`, then either `LOCO PROGRAMMED TO #n OK` or `PROGRAM FAILED - CHECK PROG TRACK`. Any key dismisses the result.

The handheld sends the write command, then asks the CSB1 to read the address back and compares it. If nothing completes within 30 seconds it reports a failure.

To drive the loco at its new address afterwards, select an engine, click its select button again, type the new address and press `#`.

**If it fails:** check that the loco is on the programming track and the track is clean, that the programming track is wired and defined on the CSB1, and that the link dot is filled. Programming is ignored in DC mode.

## 6. DC mode

DC mode is one DC loco on a single panel, with only encoder 1 working. The handheld follows the DC switch position at all times, including if the switch was already in the DC position at power-up.

**Throw the switch to DC:**

1. Both DCC engines are sent a stop (speed 0).
2. The CSB1 is told to switch the track that normally runs your DCC locos into DC mode, as a fixed cab ID, then track power is switched back on for it. By default that is track A and cab 3; both can be changed in the setup section.
3. The screen changes to `DC MODE`, the direction, a large speed bar and the percentage.

**While in DC mode:**

- Encoder 1 turn sets the speed, and its push button flips direction.
- Either E-stop button stops the DC loco.
- Encoder 2, the select buttons and the keypad are ignored.

**Throw the switch back to DCC:** the DC loco is stopped, the track returns to normal DCC operation with power on, and the two-engine screen comes back. The DCC engines keep their addresses and start at speed 0.

DCC locos on the DC track cannot be controlled while it is in DC mode, so run only the DC loco on that track.

## 7. Safety and faults

### Emergency stop

Each E-stop button stops its own engine immediately. It works in every mode, including debug mode. It stops that loco only; it does not cut track power, so use the CSB1's own controls for a full power-off.

The E-stop fails safe. If an E-stop input starts chattering, as a failing button or loose wire would, the handheld treats that as a press and stops the engine instead of ignoring the button.

### Fault banners

The handheld watches every button, switch and encoder. An input that changes far faster than a hand could is flagged as faulty and ignored until it has been quiet for 3 seconds.

- **Inverted banner on an engine's second line:** `ESTOP1 FAULT - CHECK WIRING`, `SELECT-1 BTN FAULT`, `ENC1 BUTTON FAULT` or `ENC1 SENSOR NOISY - IGNORED` (and the same for engine 2). Check that control's wiring.
- **Small filled square, top left:** the DC switch input is faulty. The handheld stays in whichever mode it was in.
- **`NO LINK - CONTROLS NOT SENT`:** the link to the CSB1 is down (see section 3).

### Debug mode

Hold SEL1 and SEL2 together for 1.5 seconds to enter or leave debug mode. Use it after building the handheld to check that every control reports correctly.

- The screen lists the live state of every button and switch (`DN` pressed, `UP` released), how far each encoder has turned since you entered, the last key pressed, and any fault codes.
- Nothing is sent to the layout except E-stop.
- All throttles are stopped each time you enter or leave debug mode.

## 8. Setup for builders

### Software

The firmware is in the `handheld_main` folder of the repository. Open `handheld_main.ino` in the Arduino IDE (the folder name must match the sketch name), install ESP32 board support, and add the libraries `ESP32Encoder`, `Keypad` and `U8g2`.

### Settings in `dcc.cpp`

| Setting | Default | Meaning |
| --- | --- | --- |
| `WIFI_SSID` / `WIFI_PASS` | `DCCEX_CSB1` / `PASS_CSB1` | The CSB1's Wi-Fi name and password. Change both to match yours |
| `CSB1_IP` / `CSB1_PORT` | `192.168.4.1` / 2560 | Where the CSB1 listens |
| `DC_TRACK_LETTER` | `A` | The track that normally runs your DCC locos. It is switched to DC while the DC switch is thrown. Never the programming track |
| `DC_CAB_ID` | 3 | The cab ID the DC loco is driven as |
| Starting engines | 4401, 8302 | Initial addresses for engine 1 and 2 |

### Pins (ESP32)

| Part | Pins |
| --- | --- |
| Encoder 1 | A 19, B 21, push 22 |
| Encoder 2 | A 4, B 0, push 2 |
| SEL1 / SEL2 buttons | 15 / 26 |
| DC switch | 36 (low = DC) |
| E-STOP 1 / 2 | 34 / 35 |
| Keypad rows | 13, 12, 14, 27 |
| Keypad columns | 32, 33, 25 |
| OLED (256x64, SPI) | SCK 18, MOSI 23, CS 5, DC 16, RESET 17 |

- Pins 34, 35 and 36 are input-only with no internal pull-up. Each needs an external 10k resistor to 3.3V.
- Pins 0, 2, 12 and 15 affect booting if held at the wrong level at power-up. Avoid a control holding them that way.
- Pins 16 and 17 clash with PSRAM on WROVER boards.

### CSB1 requirements

A track defined for programming (PROG) is needed for section 5, and the DC switch acts on your normal DCC track, never on the programming track.

## 9. Troubleshooting and known limits

The handheld prints every command it sends (lines starting `TX:`) to the serial monitor at 115200 baud, which helps when something does not respond.

| Symptom | Likely cause | What to try |
| --- | --- | --- |
| Hollow dot or `NO LINK` after power-up | CSB1 Wi-Fi not up, or wrong name or password | Check the settings in `dcc.cpp`. The handheld keeps retrying by itself |
| Link is up but a loco does not respond | Wrong address, or the CSB1 rejected the command | Check the `TX:` lines and try the same command on the CSB1 serial console |
| A function does nothing | The decoder does not map that function number | Try another number, or check the decoder's function mapping |
| DC mode has no power | Wrong `DC_TRACK_LETTER`, or the track is not DC-capable | Set the letter to the track that runs your DCC locos |
| Programming always fails | Loco not on the programming track, no PROG track defined, or a dirty track | See section 5 |
| Inverted fault banner | Loose wire, failed pull-up, or a worn switch | Check that control's wiring, then use debug mode |
| Blank screen | OLED wiring or SPI problem | Check the pins above, or try the software-SPI line noted in `ui.cpp` |
| Will not boot with a control held | A strapping pin is held at the wrong level | See the pin notes in section 8 |

### Known limits

- Joined engines (one throttle driving two locos together) are not built. The two engines are always independent.
- Function state is tracked by the handheld and never read back from the decoder.
- There is no global track power-off button. E-stop stops one loco at a time.
- If the link drops, locos keep running at their last speed until it returns.
