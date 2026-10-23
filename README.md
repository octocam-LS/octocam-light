# Octocam Light

LED light unit for the Octocam Remote: Wemos D1 mini (ESP8266), MOSFET dimmer on D7,
fan PWM on D4, dimmer pot on A0, buttons on D5 and D6 (presets), 0.91" 128x32 OLED on
D2/D1. Listens to the remote over ESP-NOW (channel 11); details are at the top of the sketch.

## Updating a light over WiFi

1. Join the light's own network `OctocamLight<hex>` (password `88888888` unless changed) and open
   http://8.8.8.8 (or tap the phone's "sign in to network" notice).
2. First time only: enter your home WiFi name and password under "Home WiFi (for updates)".
3. Tap **Update Firmware**. The light leaves its network, joins your WiFi, reads
   `firmware/version.txt` here and, if it differs from the installed version, installs
   `firmware/OctocamLight_8266.bin` and restarts. Then it is back on its own network:
   reconnect and the page shows the result.

The WiFi password is stored only in the light. It is never in the sketch, a .bin or this repo.

## Publishing a new firmware

1. Bump `FIRMWARE_VERSION` in `OctocamLight_8266/OctocamLight_8266.ino`.
2. Arduino IDE: board "LOLIN(WEMOS) D1 R2 & mini", Flash Size "4MB (FS:2MB OTA:~1019KB)",
   then **Sketch > Export Compiled Binary**.
3. Copy the exported `OctocamLight_8266.ino.bin` to `firmware/OctocamLight_8266.bin`.
4. Write the same version string into `firmware/version.txt` (one line).
5. Commit both files (and the sketch) to `main`. Lights see it within a few minutes
   (GitHub caches raw files for up to 5 min).

The first install on a board is over USB (Arduino IDE); after that, WiFi updates work as
long as every build uses the same Flash Size setting.
