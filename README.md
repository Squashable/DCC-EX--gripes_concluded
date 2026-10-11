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

The ESP32 firmware lives in `handheld_main/` (open `handheld_main.ino` in Arduino IDE; the folder name must match the sketch name). Libraries needed: `ESP32Encoder`, `Keypad`, `U8g2`. See `CONTROLS.md` for the controls and on-screen indicators.

Set the CSB1 Wi-Fi name/password in `handheld_main/dcc.cpp`. In DC mode the handheld runs one DC loco (track and cab id set by `DC_TRACK_LETTER` / `DC_CAB_ID` in the same file) with a basic single-throttle screen.
