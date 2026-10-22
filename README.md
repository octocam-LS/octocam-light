# Octocam Light

LED light unit for the Octocam Remote: Wemos D1 mini (ESP8266), MOSFET dimmer on D7,
dimmer pot on A0, button on D5, 0.91" 128x32 OLED on D2/D1. Listens to the remote over
ESP-NOW (channel 11); details are at the top of the sketch.

## Updating a light over WiFi

1. On the light, hold D5 for 1-3 s: WiFi on. The OLED shows its IP address
   (first time: it opens the setup network `OctocamLight<hex>`, password `88888888`,
   page http://8.8.8.8 -- enter the WiFi name and password there).
2. Open `http://<IP>` in the same network, tap **Check for Update**, then **Install Update**.
3. The light downloads `firmware/OctocamLight_8266.bin` from this repo and restarts.

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
