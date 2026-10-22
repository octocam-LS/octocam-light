/*
  Octocam Light -- LED light unit for the Octocam Remote
  Board: LOLIN(WEMOS) D1 R2 & mini (ESP8266), "esp8266 by ESP8266 Community" 3.x
         Tools > Flash Size: "4MB (FS:2MB OTA:~1019KB)"   Tools > Erase Flash: "Only Sketch"
  Library: "U8g2" by oliver (Library Manager). Everything else ships with the core.
  GitHub: https://github.com/octocam-LS/octocam-light (sketch + the .bin the light updates from)

  ---------------------------------------------------------------------------
  WIRING
  ---------------------------------------------------------------------------
    D7 (GPIO13)  PWM -> MOSFET dimmer input (HIGH = LED on, see PWM_ACTIVE_HIGH)
    A0           potentiometer wiper (ends to 3V3 and GND). The D1 mini's A0 already
                 has the divider for 0-3.3 V.
    D5 (GPIO14)  button to GND
                   click (release < 1 s) : LED on / off
                   hold 1 - 3 s          : WIFI / web app on (or off again), see below
                   hold 3 - 10 s         : PAIRING window, 8 s (switch the remote on during
                                           it, or hold the remote's TX button 2.5-10 s)
                   hold 10 s +           : CLEAR all pairings
                 Decided on release; while held the OLED shows what a release would do.
    D2 (GPIO4) = SDA, D1 (GPIO5) = SCL : 0.91" 128x32 SSD1306 OLED

  ---------------------------------------------------------------------------
  WHO SETS THE BRIGHTNESS
  ---------------------------------------------------------------------------
  - Power on: LED on at 25 %. The dimmer's position is ignored.
  - DIMMER: turning the pot more than POT_DEADZONE away from where it was when it
    last lost control takes over; from then on the LED follows the pot (and the
    pot turns the LED on if it was off).
  - REMOTE: when the remote's assist light changes (D4 on the remote / web app), the
    remote takes over: assist on = its assist level, assist off = LED off. The pot
    then has to be turned past the dead-zone again to take control back.
  - BUTTON D5 click: LED on / off (on again at the last level; the pot has to be
    turned past the dead-zone again to take control).
  - SHOOTING (remote SHOOT button or a light-barrier signal through the remote):
    the LED goes to the remote's Shooting Light level for as long as the remote says,
    whatever the state above -- also when the LED is off -- and back afterwards.
    Shooting at 100 % = no PWM at all (steady DC), so no banding with the electronic
    shutter. Below 100 % the LED is pulsed at PWM_FREQ_HZ: keep that high.

  ---------------------------------------------------------------------------
  RADIO / PAIRING (receiver side, same as the Octocam triggers)
  ---------------------------------------------------------------------------
  WiFi channel 11, signature 8F 3C D1 62. Pairs ONLY on a sender's hello (0x04) inside
  the pairing window, acks it (0x03 x3) and sends its name (0x05 PAIR_INFO,
  "OctocamLight<last 3 MAC bytes>"), so the remote lists it in Paired Devices.
  An already-paired sender heard in the window is acked once more (re-ack).
  With nothing paired, the window opens by itself at power-on.
  Uses 0x06 REMOTE STATE from paired senders only:
    [7] flags (bit0 = shutter held, not used here) [8] base % (assist light)
    [9] boost % (shooting light) [10..11] boost ms left (big endian)
  Everything else (0x01 TRIGGER ...) is ignored.

  ---------------------------------------------------------------------------
  WIFI / WEB APP / FIRMWARE UPDATE (1.1)
  ---------------------------------------------------------------------------
  ESP-NOW (the remote) is on channel 11 and the ESP8266 has one radio, so the light is
  normally NOT on the home WiFi. Hold D5 1-3 s to switch WiFi on:
    - it joins the saved network (default SSID "birdbox"; the password is not in the
      sketch -- see below). The OLED shows the IP address: open http://<that IP> on a
      phone / PC in the same network.
    - with no password saved yet, or if joining fails for 20 s, it opens its own setup
      network "OctocamLight<hex>" (password 88888888) -> http://8.8.8.8 to enter
      the WiFi name + password. They are saved in the light (not in the sketch, not on
      GitHub) and kept through firmware updates.
  The web app shows the light's state, lets you change the WiFi network and password,
  and has "Check for Update" / "Install Update": the light downloads the newest .bin
  from the GitHub repo (firmware/version.txt + firmware/OctocamLight_8266.bin) over
  HTTPS and restarts with it.
  While WiFi is on, the light can't hear the remote (other channel) -- the button and
  the dimmer still work. WiFi switches off again (back to the remote) with D5 1-3 s,
  "Back to Remote" in the app, or 90 s after the app was last used.

  OLED: big brightness on the left ("OFF" when off). Right: who is in control and the
  number of paired senders; "SHOOT" while the shooting light is on; pairing as on the
  triggers ("Pairing" / "New: n" / seconds left, then "Paired! +n" or "Nothing new").
*/

#include <ESP8266WiFi.h>
#include <espnow.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecureBearSSL.h>
#include <EEPROM.h>
#include <Wire.h>
#include <U8g2lib.h>
extern "C" {
  #include <user_interface.h>
}

// ---------- USER-CONFIGURABLE SETTINGS ----------
#define FIRMWARE_VERSION      "1.1.0-light8266"   // must match firmware/version.txt for a release

#define PRODUCT_SIGNATURE_0   0x8F
#define PRODUCT_SIGNATURE_1   0x3C
#define PRODUCT_SIGNATURE_2   0xD1
#define PRODUCT_SIGNATURE_3   0x62
#define PACKET_TYPE_PAIR_ACK     0x03
#define PACKET_TYPE_PAIR_HELLO   0x04
#define PACKET_TYPE_PAIR_INFO    0x05
#define PACKET_TYPE_REMOTE_STATE 0x06
#define PACKET_LEN               7
#define REMOTE_STATE_LEN         12
#define SENDER_NAME_MAX          32

#define WIFI_CHANNEL          11
#define TX_POWER_DBM          17.0f
#define MAX_PAIRED            8
#define PAIRING_WINDOW_MS     8000UL
#define PAIR_ACK_COPIES       3
#define PAIR_ACK_GAP_MS       30
#define RESULT_SHOW_MS        2000UL

// ----- pins -----
#define PWM_PIN               13      // D7 -> MOSFET dimmer
#define PWM_ACTIVE_HIGH       true    // false if the dimmer module is on while its input is LOW
#define PWM_FREQ_HZ           20000   // high = no flicker / banding; lower (e.g. 2000) if the MOSFET
                                      // module gets warm or dims unevenly at low levels
#define PWM_RANGE             1000
#define BUTTON_PIN            14      // D5
#define OLED_SDA              4       // D2
#define OLED_SCL              5       // D1

// ----- brightness -----
#define START_LEVEL           25      // % at power-on (and when switched on with no level yet)
#define POT_DEADZONE          40      // ADC counts (of 1023, ~4 %) the pot must move to take over
#define POT_STEP_HYST         6       // ADC counts before a new level is taken while following the pot
#define POT_READ_MS           20
#define POT_LOW_END           12      // below this = 0 %
#define POT_HIGH_END          1010    // above this = 100 %
#define POT_REVERSED          false   // true if turning "up" makes it darker

// ----- WiFi / web app / update -----
#define DEFAULT_WIFI_SSID     "birdbox"   // only the name; the password is entered in the setup network
#define SETUP_AP_PASSWORD     "88888888"
#define WIFI_JOIN_TIMEOUT_MS  20000UL
#define WIFI_IDLE_OFF_MS      90000UL     // back to the remote this long after the app was last used
#define UPDATE_BASE_URL       "https://raw.githubusercontent.com/octocam-LS/octocam-light/main/firmware/"
#define UPDATE_VERSION_FILE   "version.txt"
#define UPDATE_BIN_FILE       "OctocamLight_8266.bin"

// ----- button -----
#define BUTTON_DEBOUNCE_MS    30
#define WIFI_PRESS_MS         1000UL
#define PAIR_PRESS_MS         3000UL
#define CLEAR_PRESS_MS        10000UL
// -------------------------------------------------

// ---------------- Saved pairings (emulated EEPROM) ----------------
#define SETTINGS_MAGIC 0x4C543831UL   // "LT81"
#define WIFI_MAGIC 0x57493131UL       // "WI11": WiFi fields below are valid
struct PersistedSettings {
  uint32_t magic;
  uint8_t  count;
  uint8_t  macs[MAX_PAIRED][6];
  uint32_t wifiMagic;                 // 1.1: appended, so 1.0 pairings stay valid
  char     wifiSsid[33];
  char     wifiPass[65];
};
PersistedSettings settings;

uint8_t pairedMacs[MAX_PAIRED][6];
uint8_t pairedCount = 0;

// ---------------- Light state ----------------
enum Source { SRC_START, SRC_BUTTON, SRC_DIMMER, SRC_REMOTE };
Source source = SRC_START;
bool lightOn = true;
uint8_t level = START_LEVEL;          // % while on (not shooting)
uint8_t lastRemoteBase = 0;           // the remote's assist level last seen (0 = off)
uint8_t boostLevel = 100;
unsigned long boostUntil = 0;
bool boostOn = false;
int outputPct = -1;                   // what the PWM is set to now

// pot
float potF = -1;
float potRef = 0;                     // where the pot was when it last lost control
bool potControl = false;
float potLastApplied = 0;
unsigned long lastPotRead = 0;
uint8_t potWarmup = 0;

// ---------------- Radio ----------------
char myName[33];
volatile bool stateNew = false;
volatile uint8_t stateBase = 0, stateBoost = 0;
volatile uint16_t stateLeftMs = 0;
volatile unsigned long stateAtMs = 0;

#define HELLO_QUEUE_LEN 4
uint8_t helloMacs[HELLO_QUEUE_LEN][6];
volatile uint8_t helloHead = 0, helloTail = 0;

bool pairingWindow = false;
unsigned long pairingEnd = 0;
uint8_t pairingNew = 0;
uint8_t reAckedMask = 0;
unsigned long resultUntil = 0;
char resultLine1[12] = "", resultLine2[12] = "", resultLine3[12] = "";

// ---------------- Button ----------------
bool btnDown = false;
unsigned long btnEdgeMs = 0, btnPressMs = 0;

// ---------------- Display ----------------
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
char lastDrawKey[64] = "";

// ---------------- WiFi / web app ----------------
enum WifiMode { WM_OFF, WM_JOINING, WM_HOME, WM_SETUP };
WifiMode wifiMode = WM_OFF;
unsigned long wifiStartMs = 0;
unsigned long webLastUsed = 0;
ESP8266WebServer server(80);
DNSServer dnsServer;
IPAddress setupIP(8, 8, 8, 8);
IPAddress setupMask(255, 255, 255, 0);
volatile bool wifiReconnectRequested = false;   // new credentials saved: join again
volatile bool wifiOffRequested = false;
volatile bool updateCheckRequested = false;
volatile bool updateInstallRequested = false;
enum UpdState { UPD_IDLE, UPD_CHECKING, UPD_CHECKED, UPD_INSTALLING, UPD_ERROR };
UpdState updState = UPD_IDLE;
String updRemoteVersion = "";
String updError = "";
int updProgress = 0;

void startWifi();
void stopWifi();

// =====================================================================
// Helpers
// =====================================================================
bool macEquals(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }

int findPaired(const uint8_t* mac) {
  for (uint8_t i = 0; i < pairedCount; i++) if (macEquals(pairedMacs[i], mac)) return i;
  return -1;
}

void savePairings() {
  settings.count = pairedCount;
  memset(settings.macs, 0, sizeof(settings.macs));
  memcpy(settings.macs, pairedMacs, pairedCount * 6);
  EEPROM.put(0, settings);
  EEPROM.commit();
}

void loadPairings() {
  EEPROM.begin(sizeof(PersistedSettings));
  EEPROM.get(0, settings);
  if (settings.magic != SETTINGS_MAGIC || settings.count > MAX_PAIRED) {
    memset(&settings, 0, sizeof(settings));
    settings.magic = SETTINGS_MAGIC;
    EEPROM.put(0, settings);
    EEPROM.commit();
  }
  pairedCount = settings.count;
  memcpy(pairedMacs, settings.macs, pairedCount * 6);
  if (settings.wifiMagic != WIFI_MAGIC) {     // 1.0 save or new: default network, no password
    settings.wifiMagic = WIFI_MAGIC;
    strlcpy(settings.wifiSsid, DEFAULT_WIFI_SSID, sizeof(settings.wifiSsid));
    settings.wifiPass[0] = 0;
    EEPROM.put(0, settings);
    EEPROM.commit();
  }
  settings.wifiSsid[sizeof(settings.wifiSsid) - 1] = 0;
  settings.wifiPass[sizeof(settings.wifiPass) - 1] = 0;
}

void addPeer(uint8_t* mac) {
  if (!esp_now_is_peer_exist(mac)) esp_now_add_peer(mac, ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, NULL, 0);
}

// =====================================================================
// Output
// =====================================================================
void setPwm(int pct) {
  if (pct == outputPct) return;
  outputPct = pct;
  int duty = pct * PWM_RANGE / 100;
  if (!PWM_ACTIVE_HIGH) duty = PWM_RANGE - duty;
  if (duty <= 0)              { analogWrite(PWM_PIN, 0); digitalWrite(PWM_PIN, LOW); }
  else if (duty >= PWM_RANGE) { analogWrite(PWM_PIN, 0); digitalWrite(PWM_PIN, HIGH); }   // steady, no PWM
  else                        analogWrite(PWM_PIN, duty);
}

bool boostActive() {
  return boostOn && (long)(boostUntil - millis()) > 0;
}

void updateOutput() {
  setPwm(boostActive() ? boostLevel : (lightOn ? level : 0));
}

// =====================================================================
// Radio
// =====================================================================
void onDataRecv(uint8_t* mac, uint8_t* d, uint8_t len) {
  if (len < PACKET_LEN) return;
  if (d[0] != PRODUCT_SIGNATURE_0 || d[1] != PRODUCT_SIGNATURE_1 ||
      d[2] != PRODUCT_SIGNATURE_2 || d[3] != PRODUCT_SIGNATURE_3) return;
  uint8_t type = d[4];

  if (type == PACKET_TYPE_PAIR_HELLO) {
    if (!pairingWindow) return;
    uint8_t next = (helloHead + 1) % HELLO_QUEUE_LEN;
    if (next == helloTail) return;
    memcpy(helloMacs[helloHead], mac, 6);
    helloHead = next;
    return;
  }
  if (type == PACKET_TYPE_REMOTE_STATE && len >= REMOTE_STATE_LEN) {
    if (findPaired(mac) < 0) return;
    stateBase = d[8] > 100 ? 100 : d[8];
    stateBoost = d[9] > 100 ? 100 : d[9];
    stateLeftMs = ((uint16_t)d[10] << 8) | d[11];
    stateAtMs = millis();
    stateNew = true;
  }
}

void applyRemoteState() {
  if (!stateNew) return;
  stateNew = false;
  uint8_t base = stateBase;
  if (base != lastRemoteBase) {              // the remote's assist light changed: remote takes over
    lastRemoteBase = base;
    source = SRC_REMOTE;
    lightOn = base > 0;
    if (base > 0) level = base;
    potControl = false;
    potRef = potF;
  }
  boostLevel = stateBoost;
  boostOn = stateLeftMs > 0;
  boostUntil = stateAtMs + stateLeftMs;
}

void sendAck(uint8_t* mac) {
  addPeer(mac);
  uint8_t ack[PACKET_LEN] = { PRODUCT_SIGNATURE_0, PRODUCT_SIGNATURE_1, PRODUCT_SIGNATURE_2,
                              PRODUCT_SIGNATURE_3, PACKET_TYPE_PAIR_ACK, 0, 0 };
  for (uint8_t i = 0; i < PAIR_ACK_COPIES; i++) {
    esp_now_send(mac, ack, PACKET_LEN);
    delay(PAIR_ACK_GAP_MS);
  }
  uint8_t n = (uint8_t)strnlen(myName, SENDER_NAME_MAX);
  uint8_t info[PACKET_LEN + 1 + SENDER_NAME_MAX] = { PRODUCT_SIGNATURE_0, PRODUCT_SIGNATURE_1,
      PRODUCT_SIGNATURE_2, PRODUCT_SIGNATURE_3, PACKET_TYPE_PAIR_INFO, 0, 0 };
  info[PACKET_LEN] = n;
  memcpy(info + PACKET_LEN + 1, myName, n);
  esp_now_send(mac, info, PACKET_LEN + 1 + n);
}

void showResult(const char* a, const char* b, const char* c) {
  strlcpy(resultLine1, a, sizeof(resultLine1));
  strlcpy(resultLine2, b, sizeof(resultLine2));
  strlcpy(resultLine3, c, sizeof(resultLine3));
  resultUntil = millis() + RESULT_SHOW_MS;
}

void openPairingWindow() {
  pairingWindow = true;
  pairingEnd = millis() + PAIRING_WINDOW_MS;
  pairingNew = 0;
  reAckedMask = 0;
  resultUntil = 0;
}

void processPairing() {
  while (helloTail != helloHead) {
    uint8_t mac[6];
    memcpy(mac, helloMacs[helloTail], 6);
    helloTail = (helloTail + 1) % HELLO_QUEUE_LEN;
    if (!pairingWindow) continue;
    int idx = findPaired(mac);
    if (idx < 0) {
      if (pairedCount >= MAX_PAIRED) continue;
      memcpy(pairedMacs[pairedCount++], mac, 6);
      pairingNew++;
      savePairings();
      sendAck(mac);
    } else if (!((reAckedMask >> idx) & 1)) {
      reAckedMask |= (uint8_t)(1u << idx);   // already paired: ack once more in case it lost us
      sendAck(mac);
    }
  }
  if (pairingWindow && (long)(millis() - pairingEnd) >= 0) {
    pairingWindow = false;
    char b[12];
    if (pairingNew) {
      snprintf(b, sizeof(b), "+%u", pairingNew);
      char c[12]; snprintf(c, sizeof(c), "%u total", pairedCount);
      showResult("Paired!", b, c);
    } else {
      snprintf(b, sizeof(b), "%u kept", pairedCount);
      showResult("Nothing", "new", b);
    }
  }
}

void clearPairings() {
  for (uint8_t i = 0; i < pairedCount; i++) {
    if (esp_now_is_peer_exist(pairedMacs[i]) > 0) esp_now_del_peer(pairedMacs[i]);
  }
  pairedCount = 0;
  savePairings();
  pairingWindow = false;
  showResult("Pairings", "cleared", "");
}

// =====================================================================
// Pot
// =====================================================================
uint8_t potToPct(float raw) {
  if (POT_REVERSED) raw = 1023 - raw;
  if (raw <= POT_LOW_END) return 0;
  if (raw >= POT_HIGH_END) return 100;
  long p = lround((raw - POT_LOW_END) * 100.0f / (POT_HIGH_END - POT_LOW_END));
  return (uint8_t)constrain(p, 0, 100);
}

void readPot() {
  unsigned long now = millis();
  if (now - lastPotRead < POT_READ_MS) return;
  lastPotRead = now;
  int raw = analogRead(A0);
  if (potF < 0) { potF = raw; potRef = raw; return; }
  potF += (raw - potF) * 0.25f;
  if (potWarmup < 10) { potWarmup++; potRef = potF; return; }   // settle before anything counts

  if (!potControl) {
    if (fabsf(potF - potRef) > POT_DEADZONE) {
      potControl = true;
      source = SRC_DIMMER;
      lightOn = true;
      level = potToPct(potF);
      potLastApplied = potF;
    }
    return;
  }
  if (fabsf(potF - potLastApplied) >= POT_STEP_HYST) {
    potLastApplied = potF;
    level = potToPct(potF);
    lightOn = true;
  }
}

// =====================================================================
// Button D5
// =====================================================================
uint8_t holdTier() {
  if (!btnDown) return 0;
  unsigned long held = millis() - btnPressMs;
  if (held >= CLEAR_PRESS_MS) return 3;
  if (held >= PAIR_PRESS_MS) return 2;
  if (held >= WIFI_PRESS_MS) return 1;
  return 0;
}

void checkButton() {
  unsigned long now = millis();
  bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down == btnDown || now - btnEdgeMs < BUTTON_DEBOUNCE_MS) return;
  btnEdgeMs = now;
  if (down) { btnDown = true; btnPressMs = now; return; }
  unsigned long held = now - btnPressMs;
  btnDown = false;
  if (held >= CLEAR_PRESS_MS) {
    clearPairings();
  } else if (held >= PAIR_PRESS_MS) {
    openPairingWindow();
  } else if (held >= WIFI_PRESS_MS) {
    if (wifiMode == WM_OFF) startWifi(); else stopWifi();
  } else {
    lightOn = !lightOn;                      // click: on / off
    if (lightOn && level == 0) level = START_LEVEL;
    source = SRC_BUTTON;
    potControl = false;                      // the pot has to move again to take over
    potRef = potF;
  }
}

// =====================================================================
// OLED
// =====================================================================
void drawWifiScreen(uint8_t tierShown);

void updateDisplay() {
  unsigned long now = millis();
  if (wifiMode != WM_OFF && holdTier() == 0) { drawWifiScreen(0); return; }
  char big[8];
  if (outputPct <= 0) strcpy(big, "OFF");
  else snprintf(big, sizeof(big), "%d%%", outputPct);

  char l1[12] = "", l2[12] = "", l3[12] = "";
  uint8_t tier = holdTier();
  if (tier == 1)      { strcpy(l1, "Release:"); strcpy(l2, wifiMode == WM_OFF ? "WiFi on" : "WiFi off"); }
  else if (tier == 2) { strcpy(l1, "Release:"); strcpy(l2, "Pairing"); }
  else if (tier == 3) { strcpy(l1, "Release:"); strcpy(l2, "Clear"); strcpy(l3, "pairings"); }
  else if (pairingWindow) {
    long left = (long)(pairingEnd - now);
    strcpy(l1, "Pairing");
    snprintf(l2, sizeof(l2), "New: %u", pairingNew);
    snprintf(l3, sizeof(l3), "%lds", left > 0 ? (left + 999) / 1000 : 0);
  } else if ((long)(resultUntil - now) > 0) {
    strcpy(l1, resultLine1); strcpy(l2, resultLine2); strcpy(l3, resultLine3);
  } else {
    const char* src[] = { "", "Button", "Dimmer", "Remote" };
    strcpy(l1, boostActive() ? "SHOOT" : src[source]);
    if (pairedCount) snprintf(l2, sizeof(l2), "%u paired", pairedCount);
    else strcpy(l2, "no pair");
  }

  char key[48];
  snprintf(key, sizeof(key), "%s|%s|%s|%s", big, l1, l2, l3);
  if (strcmp(key, lastDrawKey) == 0) return;
  strcpy(lastDrawKey, key);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_logisoso24_tr);
  u8g2.drawStr(0, 28, big);
  u8g2.drawVLine(76, 0, 32);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(80, 9, l1);
  u8g2.drawStr(80, 20, l2);
  u8g2.drawStr(80, 31, l3);
  u8g2.sendBuffer();
}

// =====================================================================
// WiFi / web app / firmware update
// =====================================================================
#include "lightpage.h"

void drawWifiScreen(uint8_t) {
  char l1[24], l2[24], l3[24];
  char lvl[8];
  if (outputPct <= 0) strcpy(lvl, "OFF"); else snprintf(lvl, sizeof(lvl), "%d%%", outputPct);
  if (updState == UPD_INSTALLING) {
    strcpy(l1, "Updating firmware");
    snprintf(l2, sizeof(l2), "%d %%", updProgress);
    strcpy(l3, "don't switch off");
  } else if (wifiMode == WM_JOINING) {
    snprintf(l1, sizeof(l1), "WiFi %.15s", settings.wifiSsid);
    strcpy(l2, "connecting...");
    snprintf(l3, sizeof(l3), "LED %s", lvl);
  } else if (wifiMode == WM_HOME) {
    snprintf(l1, sizeof(l1), "WiFi %.15s", settings.wifiSsid);
    strlcpy(l2, WiFi.localIP().toString().c_str(), sizeof(l2));
    snprintf(l3, sizeof(l3), "LED %s  D5 1-3s:off", lvl);
  } else {
    strcpy(l1, "Setup: join WiFi");
    strlcpy(l2, myName, sizeof(l2));
    strcpy(l3, "pw 88888888  8.8.8.8");
  }
  char key[64];
  snprintf(key, sizeof(key), "W%s|%s|%s", l1, l2, l3);
  if (strcmp(key, lastDrawKey) == 0) return;
  strlcpy(lastDrawKey, key, sizeof(lastDrawKey));
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 9, l1);
  u8g2.drawStr(0, 20, l2);
  u8g2.drawStr(0, 31, l3);
  u8g2.sendBuffer();
}

void webUsed() { webLastUsed = millis(); }

void startSetupAP() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(setupIP, setupIP, setupMask);
  WiFi.softAP(myName, SETUP_AP_PASSWORD, WIFI_CHANNEL, 0, 1);
  dnsServer.start(53, "*", setupIP);
  server.begin();
  wifiMode = WM_SETUP;
  webUsed();
}

void startWifi() {
  wifiStartMs = millis();
  webUsed();
  updState = UPD_IDLE;
  if (settings.wifiPass[0] == 0 || settings.wifiSsid[0] == 0) { startSetupAP(); return; }
  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.wifiSsid, settings.wifiPass);
  wifiMode = WM_JOINING;
}

// Back to the remote: home WiFi off, radio on channel 11 again.
void stopWifi() {
  server.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  wifi_station_disconnect();
  wifi_set_channel(WIFI_CHANNEL);
  wifiMode = WM_OFF;
  lastDrawKey[0] = 0;
}

const char* sourceName() {
  const char* src[] = { "Start", "Button", "Dimmer", "Remote" };
  return src[source];
}

String jsonEsc(const char* in) {
  String o;
  for (const char* p = in; *p; p++) { if (*p == '"' || *p == '\\') o += '\\'; o += *p; }
  return o;
}

String statusJson() {
  String j = "{\"version\":\"" FIRMWARE_VERSION "\"";
  j += ",\"name\":\"" + String(myName) + "\"";
  j += ",\"led\":" + String(outputPct < 0 ? 0 : outputPct);
  j += ",\"on\":" + String(lightOn ? "true" : "false");
  j += ",\"source\":\"" + String(sourceName()) + "\"";
  j += ",\"paired\":" + String(pairedCount);
  j += ",\"mode\":\"" + String(wifiMode == WM_HOME ? "home" : "setup") + "\"";
  j += ",\"ssid\":\"" + jsonEsc(settings.wifiSsid) + "\"";
  j += ",\"passSet\":" + String(settings.wifiPass[0] ? "true" : "false");
  j += ",\"ip\":\"" + (wifiMode == WM_HOME ? WiFi.localIP().toString() : setupIP.toString()) + "\"";
  const char* us[] = { "idle", "checking", "checked", "installing", "error" };
  j += ",\"upd\":\"" + String(us[updState]) + "\"";
  j += ",\"remoteVersion\":\"" + jsonEsc(updRemoteVersion.c_str()) + "\"";
  j += ",\"updError\":\"" + jsonEsc(updError.c_str()) + "\"";
  j += ",\"progress\":" + String(updProgress);
  j += ",\"idleOffS\":" + String((long)(WIFI_IDLE_OFF_MS - (millis() - webLastUsed)) / 1000) + "}";
  return j;
}

void handleRoot() { webUsed(); server.send_P(200, "text/html", PAGE_HTML); }
void handleRedirect() {
  server.sendHeader("Location", "http://" + setupIP.toString() + "/", true);
  server.send(302, "text/plain", "");
}
void handleNotFound() {
  if (wifiMode == WM_SETUP) handleRedirect();      // captive portal in the setup network
  else server.send(404, "text/plain", "not found");
}
void handleStatus() { webUsed(); server.send(200, "application/json", statusJson()); }

void handleWifiSave() {
  webUsed();
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  ssid.trim();
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64 || (pass.length() > 0 && pass.length() < 8)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"WiFi name 1-32 characters, password 8-64 characters\"}");
    return;
  }
  strlcpy(settings.wifiSsid, ssid.c_str(), sizeof(settings.wifiSsid));
  strlcpy(settings.wifiPass, pass.c_str(), sizeof(settings.wifiPass));
  EEPROM.put(0, settings);
  EEPROM.commit();
  server.send(200, "application/json", "{\"ok\":true}");
  wifiReconnectRequested = true;                     // after the answer is out
}

void handleWifiOff() { server.send(200, "application/json", "{\"ok\":true}"); wifiOffRequested = true; }

void handleCheckUpdate() {
  webUsed();
  if (wifiMode != WM_HOME) { server.send(200, "application/json", statusJson()); return; }
  updState = UPD_CHECKING;
  updateCheckRequested = true;
  server.send(200, "application/json", statusJson());
}

void handleInstallUpdate() {
  webUsed();
  if (wifiMode == WM_HOME && updState != UPD_INSTALLING) {
    updState = UPD_INSTALLING;
    updProgress = 0;
    updateInstallRequested = true;
  }
  server.send(200, "application/json", statusJson());
}

void doUpdateCheck() {
  BearSSL::WiFiClientSecure client;
  client.setInsecure();                 // GitHub over HTTPS; the image itself is checked by the updater
  client.setBufferSizes(16384, 512);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(8000);
  updError = "";
  if (!http.begin(client, UPDATE_BASE_URL UPDATE_VERSION_FILE)) { updState = UPD_ERROR; updError = "can't reach GitHub"; return; }
  int code = http.GET();
  if (code == 200) {
    updRemoteVersion = http.getString();
    updRemoteVersion.trim();
    if (updRemoteVersion.length() == 0 || updRemoteVersion.length() > 40) { updState = UPD_ERROR; updError = "version.txt looks wrong"; }
    else updState = UPD_CHECKED;
  } else if (code == 404) {
    updState = UPD_ERROR; updError = "no firmware published on GitHub yet";
  } else {
    updState = UPD_ERROR; updError = "GitHub answered " + String(code) + " " + http.errorToString(code);
  }
  http.end();
}

void doUpdateInstall() {
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(16384, 512);
  ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  ESPhttpUpdate.rebootOnUpdate(true);
  ESPhttpUpdate.onProgress([](int done, int total) {
    int p = total > 0 ? (int)((long long)done * 100 / total) : 0;
    if (p != updProgress) { updProgress = p; drawWifiScreen(0); }
  });
  updProgress = 0;
  drawWifiScreen(0);
  t_httpUpdate_return r = ESPhttpUpdate.update(client, UPDATE_BASE_URL UPDATE_BIN_FILE);
  // only gets here if it failed (success restarts the light)
  updState = UPD_ERROR;
  updError = (r == HTTP_UPDATE_NO_UPDATES) ? String("no update") : ESPhttpUpdate.getLastErrorString();
  lastDrawKey[0] = 0;
}

void serviceWifi() {
  if (wifiMode == WM_OFF) return;
  unsigned long now = millis();
  if (wifiMode == WM_JOINING) {
    if (WiFi.status() == WL_CONNECTED) {
      server.begin();
      wifiMode = WM_HOME;
      webUsed();
    } else if (now - wifiStartMs >= WIFI_JOIN_TIMEOUT_MS) {
      startSetupAP();                          // wrong password / network not here
    }
    return;
  }
  if (wifiMode == WM_SETUP) {
    dnsServer.processNextRequest();
    if (WiFi.softAPgetStationNum() > 0) webUsed();
  }
  server.handleClient();

  if (wifiReconnectRequested) {
    wifiReconnectRequested = false;
    delay(300);                                // let the answer reach the phone
    server.stop();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    startWifi();
    return;
  }
  if (wifiOffRequested) { wifiOffRequested = false; delay(200); stopWifi(); return; }
  if (updateCheckRequested) { updateCheckRequested = false; doUpdateCheck(); webUsed(); }
  if (updateInstallRequested) { updateInstallRequested = false; doUpdateInstall(); webUsed(); }
  if (now - webLastUsed >= WIFI_IDLE_OFF_MS) stopWifi();
}

// =====================================================================
// Setup / loop
// =====================================================================
void setup() {
  pinMode(PWM_PIN, OUTPUT);
  digitalWrite(PWM_PIN, PWM_ACTIVE_HIGH ? LOW : HIGH);
  analogWriteRange(PWM_RANGE);
  analogWriteFreq(PWM_FREQ_HZ);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
  u8g2.setBusClock(400000);

  loadPairings();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  wifi_station_disconnect();
  WiFi.setSleepMode(WIFI_NONE_SLEEP);        // react at once to the remote
  WiFi.setOutputPower(TX_POWER_DBM);
  wifi_set_channel(WIFI_CHANNEL);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(myName, sizeof(myName), "OctocamLight%02X%02X%02X", mac[3], mac[4], mac[5]);

  if (esp_now_init() == 0) {
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(onDataRecv);
    for (uint8_t i = 0; i < pairedCount; i++) addPeer(pairedMacs[i]);
  }

  lightOn = true;                            // power on: 25 %, the pot is ignored
  level = START_LEVEL;
  updateOutput();
  server.on("/", handleRoot);
  server.on("/generate_204", handleRedirect);
  server.on("/gen_204", handleRedirect);
  server.on("/hotspot-detect.html", handleRedirect);
  server.on("/ncsi.txt", handleRedirect);
  server.on("/connecttest.txt", handleRedirect);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/wifi-off", HTTP_POST, handleWifiOff);
  server.on("/api/check-update", HTTP_POST, handleCheckUpdate);
  server.on("/api/install-update", HTTP_POST, handleInstallUpdate);
  server.onNotFound(handleNotFound);

  if (pairedCount == 0) openPairingWindow(); // nothing paired yet: ready to pair at once
  updateDisplay();
}

void loop() {
  applyRemoteState();        // first: shooting light must not wait
  updateOutput();
  checkButton();
  readPot();
  processPairing();
  updateOutput();
  serviceWifi();
  updateOutput();
  updateDisplay();           // only redraws when the text changed (~15 ms)
}
