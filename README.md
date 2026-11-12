**DCC Controller Issues & Solutions**

The goal of this custom controller build is to solve key limitations found in standard commercial DCC units:
 
 
 1. Only being able to address one engine at a time
 2. having to switch between functions from F1-9 F10-19
 3. A proper function activation system 
 4. direction and speed addressable for two different or joined engines. 
 5. be able to do everything a normal DCC controller can do and switch to DC for older engines.

---

**demo version of the GUI**

<img width="905" height="533" alt="image" src="https://github.com/user-attachments/assets/90af791d-4b0f-47bf-b481-9a1be186eda6" />

This is a test version of the GUI can be run with python which is what will be used on the final device. 
This paired with a wireless controller that looks something like this extremely rough drawing 
<img width="801" height="550" alt="image" src="https://github.com/user-attachments/assets/64c98477-baa1-46cc-8662-bdb07265bdf4" />



---

**Firmware**

The ESP32 firmware lives in `handheld_main/` (open `handheld_main.ino` in Arduino IDE; the folder name must match the sketch name). Libraries needed: `ESP32Encoder`, `Keypad`, `U8g2`. See `MANUAL.md` for the full user manual and `CONTROLS.md` for a short controls summary.

Set the CSB1 Wi-Fi name/password in `handheld_main/dcc.cpp`. In DC mode the handheld runs one DC loco (track and cab id set by `DC_TRACK_LETTER` / `DC_CAB_ID` in the same file) with a basic single-throttle screen.

**ESP32 pin map (stability revision)**

| Connection | ESP32 GPIO |
| --- | --- |
| Encoder 1 A / B / push | 19 / 21 / 22 |
| Encoder 2 A / B / push | 4 / 17 / 2 |
| SEL1 / SEL2 buttons | 39 / 26 |
| DC mode switch | 36 |
| E-stop 1 / 2 | 34 / 35 |
| Keypad rows | 13 / 15 / 14 / 27 |
| Keypad columns | 32 / 33 / 25 |
| OLED SPI SCK / MOSI / CS / DC | 18 / 23 / 5 / 16 |
| OLED RESET | ESP32 EN/reset |

**Wiring requirements:** GPIO34, GPIO35, GPIO36 and GPIO39 are input-only and require external 10kΩ pull-ups to 3.3V; their switches connect the GPIO to GND. GPIO12 is deliberately unused because of its flash-voltage boot strap. GPIO0 is no longer used. Keep GPIO15 and GPIO5 at safe inactive levels during reset. The OLED RESET connection goes to ESP32 EN, not GPIO17.

This pin map is for a classic ESP32-WROOM-style module without PSRAM; do not apply it unchanged to ESP32-WROVER, ESP32-S2, ESP32-S3 or ESP32-C3 boards. See `MANUAL.md` for the full wiring notes and bring-up checks.
