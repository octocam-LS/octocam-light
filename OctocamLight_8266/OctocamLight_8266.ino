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
    D6 (GPIO12)  button to GND: each press = next brightness preset 25 -> 40 -> 60 -> 80 %
                 -> 25 % (the next one above the current level; LED off -> on at 25 %)
    D2 (GPIO4) = SDA, D1 (GPIO5) = SCL : 0.91" 128x32 SSD1306 OLED
    D4 (GPIO2)   PWM -> fan (4-pin fan PWM input, or a MOSFET for a 2-pin fan;
                 see FAN_PWM_ACTIVE_HIGH). Same PWM frequency as the LED (PWM_FREQ_HZ;
                 the ESP8266 has one frequency for all pins). D4 is a boot pin: it must not
                 be pulled LOW at power-on (no pull-down on a MOSFET gate here). The D1
                 mini's blue LED is on D4 too and glows with the fan.

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
    The shooting light comes on at once; when shooting stops it fades back down to
    the normal level in BOOST_RAMP_DOWN_MS (100 ms).
    SHOOTING MAX = the shooting light's level (default 100 %, saved). Set by the remote
    (its per-light setting arrives as 0x06 [9]; a CHANGED value takes over, like the
    assist level) or in this web app (then 0x07 LIGHT_INFO tells the remotes).

  ---------------------------------------------------------------------------
  FAN (1.2)
  ---------------------------------------------------------------------------
  - Never below FAN_MIN_PCT (30 %) while the LED is on, FAN_OFF_PCT (20 %) while it
    is off. 1 s at 100 % at power-on so a slow fan surely starts.
  - Follows the normal LED level (button / dimmer / remote assist) directly:
    fan % = LED %, and 100 % while that level is over FAN_FULL_ABOVE_PCT (70 %).
  - Heat from shooting: once the LED has been above FAN_HEAT_LEVEL_PCT (25 %) for
    more than FAN_HEAT_AFTER_MS (1 s) -- a long burst, a held SHOOT button -- the fan
    ramps up slowly (FAN_RAMP_UP_PCT_S) towards what that light level needs (same
    rule: level %, 100 % above 70 %). Short shots never get there. Dips shorter than
    FAN_HEAT_GAP_MS don't restart the 1 s. Afterwards it ramps back down slowly
    (FAN_RAMP_DOWN_PCT_S) to what the normal level needs.

  ---------------------------------------------------------------------------
  RADIO / PAIRING (receiver side, same as the Octocam triggers)
  ---------------------------------------------------------------------------
  WiFi channel 11, signature 8F 3C D1 62. Pairs ONLY on a sender's hello (0x04) inside
  the pairing window, acks it (0x03 x3) and sends its name (0x05 PAIR_INFO,
  "OctocamLight<last 3 MAC bytes>"), so the remote lists it in Paired Devices.
  An already-paired sender heard in the window is acked once more (re-ack).
  With nothing paired, the window opens by itself at power-on.
  Web app "Paired Devices" (1.2.3, like the triggers): Pair (opens the window), Clear
  Pairings (tap twice), one toggle per paired remote (blue = listened to) + Forget.
  Names come from the remote's hello and are refreshed when it is heard again.
  Uses 0x06 REMOTE STATE from paired senders that are on only:
    [7] flags (bit0 = shutter held, not used here) [8] base % (assist / AF light)
    [9] Shooting Max % for this light [10..11] boost ms left (big endian)
  Everything else (0x01 TRIGGER ...) is ignored.
  Sends 0x07 LIGHT_INFO (1.2.4, 9 bytes) to its paired remotes: [7] Shooting Max %,
  [8] flags bit0 = changed in this web app. After pairing, at power-on, when Shooting Max
  is changed here, and as an answer to a remote's state at most every 10 s. Remote
  1.2.0+ lists the light with its own row (on/off, AF, TEST, max) from this.
  ESP-NOW goes out on the station interface, which keeps the chip's original soft-AP MAC
  (the address remotes paired with); the own network gets a neighbouring MAC. So the
  remote keeps reaching the light with the own network switched off.

  ---------------------------------------------------------------------------
  WIFI / WEB APP / FIRMWARE UPDATE (1.1)
  ---------------------------------------------------------------------------
  The light has its OWN network "OctocamLight<hex>" (password 88888888 until changed),
  web app at http://8.8.8.8 (phones then show their "sign in to network" notice). It is
  on channel 11 like the remote, so the remote keeps working while it is on.
  WIFI_ALWAYS_ON (default true): up from power-on, no timer. "Kill WiFi" in the app or
  D5 1-3 s switches it off; D5 1-3 s again (or power off/on) brings it back. With
  WIFI_ALWAYS_ON false it starts off and goes off again 90 s after the app was last used.
  Network name and password can be changed in the app, like on the senders: a new
  password goes live once no phone is connected; 8+ characters, empty = open network;
  the network is always open for the first 10 s after power-on (forgotten password).
  A new name restarts the network at once (reconnect to it).
  The web app shows the light's state, stores the HOME WiFi name + password (default
  SSID "birdbox"; saved only in the light, not in the sketch or on GitHub, kept through
  updates) and has ONE button "Update Firmware". Only then does the light join the home
  WiFi: it leaves its own network (the phone loses it), joins (20 s timeout), reads
  firmware/version.txt from the GitHub repo and, if that differs from FIRMWARE_VERSION,
  downloads firmware/OctocamLight_8266.bin over HTTPS, installs it and restarts. Up to
  date or failed: back to its own network; reconnect the phone and the app shows the
  result. The OLED shows each step. The light never stays on the home WiFi.

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
#define FIRMWARE_VERSION      "1.2.4-light8266"  // must match firmware/version.txt for a release

#define PRODUCT_SIGNATURE_0   0x8F
#define PRODUCT_SIGNATURE_1   0x3C
#define PRODUCT_SIGNATURE_2   0xD1
#define PRODUCT_SIGNATURE_3   0x62
#define PACKET_TYPE_PAIR_ACK     0x03
#define PACKET_TYPE_PAIR_HELLO   0x04
#define PACKET_TYPE_PAIR_INFO    0x05
#define PACKET_TYPE_REMOTE_STATE 0x06
#define PACKET_TYPE_LIGHT_INFO   0x07   // light -> remote: [7] Shooting Max %, [8] flags bit0 = changed here
#define PACKET_LEN               7
#define REMOTE_STATE_LEN         12
#define LIGHT_INFO_LEN           9
#define STATE_FLAG_LIGHT_MAX     0x02     // 0x06 [7]: [9] is our own Shooting Max, set on the remote (remote 1.2+)
#define LIGHT_INFO_EVERY_MS      10000UL  // answer a remote's state with our info at most this often
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
#define PRESET_PIN            12      // D6: preset button
#define OLED_SDA              4       // D2
#define OLED_SCL              5       // D1
#define FAN_PIN               2       // D4 -> fan PWM (boot pin, see WIRING)
#define FAN_PWM_ACTIVE_HIGH   true    // false if the fan runs faster while its input is LOW

// ----- fan -----
#define FAN_MIN_PCT           30      // never slower while the LED is on
#define FAN_OFF_PCT           20      // never slower while the LED is off
#define FAN_FULL_ABOVE_PCT    70      // LED level above this = fan 100 %
#define FAN_HEAT_LEVEL_PCT    25      // LED above this ...
#define FAN_HEAT_AFTER_MS     1000UL  // ... for longer than this = shooting heat, fan ramps up
#define FAN_HEAT_GAP_MS       500UL   // shorter dips below FAN_HEAT_LEVEL_PCT don't restart the timer
#define FAN_RAMP_UP_PCT_S     20      // shooting heat: 30 -> 100 % in 3.5 s
#define FAN_RAMP_DOWN_PCT_S   5       // after shooting: 100 -> 30 % in 14 s
#define FAN_KICK_MS           1000UL  // 100 % at power-on
#define FAN_STEP_MS           50

// ----- brightness -----
#define BOOST_RAMP_DOWN_MS    100UL   // shooting light fades back to the normal level in this time
#define SHOOT_MAX_DEFAULT     100     // % of the shooting light (web app "Shooting Max", or set by the remote)
#define SHOOT_MAX_MIN         5
#define START_LEVEL           25      // % at power-on (and when switched on with no level yet)
const uint8_t PRESET_LEVELS[] = { 25, 40, 60, 80 };   // D6 button cycles through these
#define POT_DEADZONE          40      // ADC counts (of 1023, ~4 %) the pot must move to take over
#define POT_STEP_HYST         6       // ADC counts before a new level is taken while following the pot
#define POT_READ_MS           20
#define POT_LOW_END           12      // below this = 0 %
#define POT_HIGH_END          1010    // above this = 100 %
#define POT_REVERSED          false   // true if turning "up" makes it darker

// ----- WiFi / web app / update -----
#define DEFAULT_WIFI_SSID     "birdbox"   // home WiFi for updates; only the name, the password is entered in the app
#define DEFAULT_AP_PASSWORD   "88888888"   // until a password is saved in the app
#define AP_IP                 8, 8, 8, 8   // the light's own network: web app at http://8.8.8.8 (phones show "sign in")
#define PASSWORD_GRACE_MS     10000UL      // own network is open this long after power-on (forgotten password)
#define AP_MAX_CLIENTS        4
#define WIFI_JOIN_TIMEOUT_MS  20000UL
#define WIFI_IDLE_OFF_MS      90000UL     // only with WIFI_ALWAYS_ON false: own network off this long after the app was last used
#define WIFI_ALWAYS_ON        true        // own network "OctocamLight<hex>" from power-on, no timer
                                          // (same channel as the remote, so the remote keeps working)
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
#define EXT_MAGIC  0x4C583132UL       // "LX12": names / active / shootMax below are valid
struct PersistedSettings {
  uint32_t magic;
  uint8_t  count;
  uint8_t  macs[MAX_PAIRED][6];
  uint32_t wifiMagic;                 // 1.1: appended, so 1.0 pairings stay valid
  char     wifiSsid[33];
  char     wifiPass[65];
  uint32_t extMagic;                  // 1.2.3: appended
  uint8_t  active;                    // bit i = paired sender i is on
  char     names[MAX_PAIRED][SENDER_NAME_MAX + 1];
  uint8_t  shootMax;                  // % of the shooting light
  uint32_t apMagic;                   // 1.2.4: appended
  uint8_t  apPassSaved;               // 0 = never saved -> DEFAULT_AP_PASSWORD
  char     apPass[65];                // empty (but saved) = open network
  char     apSsid[33];                // empty = automatic "OctocamLight<hex>"
};
#define AP_MAGIC 0x4C573134UL         // "LW14": own-network name / password above are valid
PersistedSettings settings;

uint8_t pairedMacs[MAX_PAIRED][6];
char pairedNames[MAX_PAIRED][SENDER_NAME_MAX + 1];
uint8_t pairedActive = 0xFF;
uint8_t pairedCount = 0;

// ---------------- Light state ----------------
enum Source { SRC_START, SRC_BUTTON, SRC_DIMMER, SRC_REMOTE };
Source source = SRC_START;
bool lightOn = true;
uint8_t level = START_LEVEL;          // % while on (not shooting)
uint8_t lastRemoteBase = 0;           // the remote's assist level last seen (0 = off)
uint8_t lastRemoteMax = 0;            // the remote's Shooting Max for us last seen (0 = none yet)
uint8_t boostLevel = 100;
unsigned long boostUntil = 0;
bool boostOn = false;
int outputPct = -1;                   // what the PWM is set to now
bool wasBoost = false;
int rampFrom = -1;                    // > normal level: fading down after shooting
unsigned long rampStartMs = 0;

// fan
int fanOutPct = -1;                   // what the fan PWM is set to now
float fanHeatPct = FAN_OFF_PCT;       // slow part (shooting heat)
unsigned long fanLastStep = 0;
unsigned long hotSince = 0, lastHotMs = 0;
bool hot = false;                     // LED above FAN_HEAT_LEVEL_PCT (with gap tolerance)

// pot
float potF = -1;
float potRef = 0;                     // where the pot was when it last lost control
bool potControl = false;
float potLastApplied = 0;
unsigned long lastPotRead = 0;
uint8_t potWarmup = 0;

// ---------------- Radio ----------------
char myName[33];                      // = the own network's name; also our name in PAIR_INFO
volatile uint8_t stateFrom = 0;       // paired index of the remote whose state is below
volatile uint8_t infoDueMask = 0;     // paired remotes owed a LIGHT_INFO
bool infoChangedHere = false;         // ... and it says "Shooting Max changed on the light"
unsigned long infoSentAt[MAX_PAIRED];
bool shootMaxSaveDue = false;
unsigned long shootMaxSaveAt = 0;
volatile bool stateNew = false;
volatile uint8_t stateBase = 0, stateBoost = 0, stateFlags = 0;
volatile uint16_t stateLeftMs = 0;
volatile unsigned long stateAtMs = 0;

#define HELLO_QUEUE_LEN 4
uint8_t helloMacs[HELLO_QUEUE_LEN][6];
char helloNames[HELLO_QUEUE_LEN][SENDER_NAME_MAX + 1];
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
bool presetDown = false;
unsigned long presetEdgeMs = 0;

// ---------------- Display ----------------
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
char lastDrawKey[64] = "";

// ---------------- WiFi / web app ----------------
enum WifiMode { WM_OFF, WM_AP, WM_UPDATE };   // WM_UPDATE: on the home WiFi, only during an update
WifiMode wifiMode = WM_OFF;
unsigned long webLastUsed = 0;
ESP8266WebServer server(80);
DNSServer dnsServer;
IPAddress apIP(AP_IP);
IPAddress apMask(255, 255, 255, 0);
String apPassword = "";
bool passwordApplyPending = false;    // a new password is saved but not yet on the live network
bool apProtectedNow = false;
bool apRestartRequested = false;      // new name: restart the network after the answer is out
bool wifiOffRequested = false;
bool updateRequested = false;
enum UpdState { UPD_IDLE, UPD_JOINING, UPD_CHECKING, UPD_INSTALLING, UPD_UPTODATE, UPD_ERROR };
UpdState updState = UPD_IDLE;
String updRemoteVersion = "";
String updError = "";
int updProgress = 0;

void startAP();
void stopAP();

// =====================================================================
// Helpers
// =====================================================================
bool macEquals(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }

// ---------------- Radio identity: ESP-NOW goes out on the STATION interface ----------------
// Same as the remote / LS_ESP8266: the station takes over the chip's original soft-AP MAC (the
// address remotes paired with), so ESP-NOW keeps working when the own network is switched off.
// The soft-AP gets a neighbouring address.
uint8_t factoryStaMac[6];   // the chip's own station MAC: used for the default network name
uint8_t radioMac[6];        // the address ESP-NOW uses = the chip's original soft-AP MAC
uint8_t apIfMac[6];         // the soft-AP's address from now on
bool radioMacsRead = false;

void readRadioMacs() {
  if (radioMacsRead) return;
  wifi_get_macaddr(STATION_IF, factoryStaMac);
  wifi_get_macaddr(SOFTAP_IF, radioMac);
  bool same = true, zero = true;
  for (uint8_t i = 0; i < 6; i++) {
    if (radioMac[i] != factoryStaMac[i]) same = false;
    if (radioMac[i] != 0) zero = false;
  }
  if (same || zero) {
    memcpy(radioMac, factoryStaMac, 6);
    radioMac[0] |= 0x02;
  }
  memcpy(apIfMac, radioMac, 6);
  apIfMac[0] = (uint8_t)((apIfMac[0] ^ 0x04) | 0x02);
  radioMacsRead = true;
}

bool macIs(uint8_t iface, const uint8_t* want) {
  uint8_t cur[6];
  wifi_get_macaddr(iface, cur);
  return memcmp(cur, want, 6) == 0;
}

// After every WiFi.mode() change, before softAP().
void applyRadioIdentity() {
  readRadioMacs();
  if ((wifi_get_opmode() & SOFTAP_MODE) && !macIs(SOFTAP_IF, apIfMac)) wifi_set_macaddr(SOFTAP_IF, apIfMac);
  if (!macIs(STATION_IF, radioMac)) wifi_set_macaddr(STATION_IF, (uint8_t*)radioMac);
}

void stationStayOffline() {
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  wifi_station_disconnect();
}

int findPaired(const uint8_t* mac) {
  for (uint8_t i = 0; i < pairedCount; i++) if (macEquals(pairedMacs[i], mac)) return i;
  return -1;
}

void savePairings() {
  settings.count = pairedCount;
  memset(settings.macs, 0, sizeof(settings.macs));
  memcpy(settings.macs, pairedMacs, pairedCount * 6);
  memset(settings.names, 0, sizeof(settings.names));
  memcpy(settings.names, pairedNames, sizeof(settings.names));
  settings.active = pairedActive;
  EEPROM.put(0, settings);
  EEPROM.commit();
}

bool senderActive(int i) { return i >= 0 && ((pairedActive >> i) & 1); }

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
  if (settings.extMagic != EXT_MAGIC || settings.shootMax < 1 || settings.shootMax > 100) {
    settings.extMagic = EXT_MAGIC;            // older save: all senders on, no names yet
    settings.active = 0xFF;
    memset(settings.names, 0, sizeof(settings.names));
    settings.shootMax = SHOOT_MAX_DEFAULT;
    EEPROM.put(0, settings);
    EEPROM.commit();
  }
  for (uint8_t i = 0; i < MAX_PAIRED; i++) settings.names[i][SENDER_NAME_MAX] = 0;
  memcpy(pairedNames, settings.names, sizeof(pairedNames));
  pairedActive = settings.active;
  if (settings.shootMax < SHOOT_MAX_MIN) settings.shootMax = SHOOT_MAX_MIN;
  if (settings.apMagic != AP_MAGIC) {         // older save: default password, automatic name
    settings.apMagic = AP_MAGIC;
    settings.apPassSaved = 0;
    settings.apPass[0] = 0;
    settings.apSsid[0] = 0;
    EEPROM.put(0, settings);
    EEPROM.commit();
  }
  settings.apPass[sizeof(settings.apPass) - 1] = 0;
  settings.apSsid[sizeof(settings.apSsid) - 1] = 0;
  apPassword = settings.apPassSaved ? String(settings.apPass) : String(DEFAULT_AP_PASSWORD);
}

void saveSettingsNow() {
  EEPROM.put(0, settings);
  EEPROM.commit();
}

// Own network name: the saved custom name, else "OctocamLight" + last 3 bytes of the factory MAC.
void buildName() {
  readRadioMacs();
  if (settings.apSsid[0]) strlcpy(myName, settings.apSsid, sizeof(myName));
  else snprintf(myName, sizeof(myName), "OctocamLight%02X%02X%02X", factoryStaMac[3], factoryStaMac[4], factoryStaMac[5]);
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

void setFan(int pct) {
  if (pct == fanOutPct) return;
  fanOutPct = pct;
  int duty = pct * PWM_RANGE / 100;
  if (!FAN_PWM_ACTIVE_HIGH) duty = PWM_RANGE - duty;
  if (duty <= 0)              { analogWrite(FAN_PIN, 0); digitalWrite(FAN_PIN, LOW); }
  else if (duty >= PWM_RANGE) { analogWrite(FAN_PIN, 0); digitalWrite(FAN_PIN, HIGH); }
  else                        analogWrite(FAN_PIN, duty);
}

// What the fan needs for an LED level: the same %, 100 % above FAN_FULL_ABOVE_PCT, never below the minimum.
int fanForLevel(int pct) {
  if (pct <= 0) return FAN_OFF_PCT;
  if (pct > FAN_FULL_ABOVE_PCT) return 100;
  return pct < FAN_MIN_PCT ? FAN_MIN_PCT : pct;
}

void updateFan(int normalPct) {
  unsigned long now = millis();
  if (now < FAN_KICK_MS) { setFan(100); return; }

  if (outputPct > FAN_HEAT_LEVEL_PCT) {
    if (!hot) { hot = true; hotSince = now; }
    lastHotMs = now;
  } else if (hot && now - lastHotMs > FAN_HEAT_GAP_MS) {
    hot = false;
  }

  if (now - fanLastStep < FAN_STEP_MS) return;
  float dt = (now - fanLastStep) / 1000.0f;
  fanLastStep = now;

  // slow part: ramps up towards the shooting level's need after 1 s of heat, then slowly back down
  float heatTarget = (hot && now - hotSince > FAN_HEAT_AFTER_MS) ? fanForLevel(outputPct) : FAN_OFF_PCT;
  if (heatTarget > fanHeatPct) fanHeatPct = min(heatTarget, fanHeatPct + FAN_RAMP_UP_PCT_S * dt);
  else                         fanHeatPct = max(heatTarget, fanHeatPct - FAN_RAMP_DOWN_PCT_S * dt);

  int pct = max(fanForLevel(normalPct), (int)lroundf(fanHeatPct));   // follows the normal level directly
  setFan(pct);
}

// Shooting light on at once; when it ends, fade to the normal level in BOOST_RAMP_DOWN_MS.
void updateOutput() {
  int normalPct = lightOn ? level : 0;
  int pct = normalPct;
  if (boostActive()) {
    pct = settings.shootMax;                 // the remote sets it (0x06 [9]) or the web app does
    wasBoost = true;
    rampFrom = -1;
  } else {
    if (wasBoost) { wasBoost = false; rampFrom = outputPct; rampStartMs = millis(); }
    if (rampFrom > normalPct) {
      unsigned long t = millis() - rampStartMs;
      if (t >= BOOST_RAMP_DOWN_MS) rampFrom = -1;
      else pct = normalPct + (int)((long)(rampFrom - normalPct) * (long)(BOOST_RAMP_DOWN_MS - t) / (long)BOOST_RAMP_DOWN_MS);
    } else {
      rampFrom = -1;
    }
  }
  setPwm(pct);
  updateFan(normalPct);
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
    if (!pairingWindow && findPaired(mac) < 0) return;   // outside the window: only to refresh a name
    uint8_t next = (helloHead + 1) % HELLO_QUEUE_LEN;
    if (next == helloTail) return;
    memcpy(helloMacs[helloHead], mac, 6);
    uint8_t n = 0;
    if (len > PACKET_LEN) {
      n = d[PACKET_LEN];
      if (n > SENDER_NAME_MAX) n = SENDER_NAME_MAX;
      if (n > len - PACKET_LEN - 1) n = len - PACKET_LEN - 1;
      memcpy(helloNames[helloHead], d + PACKET_LEN + 1, n);
    }
    helloNames[helloHead][n] = 0;
    helloHead = next;
    return;
  }
  if (type == PACKET_TYPE_REMOTE_STATE && len >= REMOTE_STATE_LEN) {
    int idx = findPaired(mac);
    if (!senderActive(idx)) return;                  // not paired, or switched off in the app
    if (millis() - infoSentAt[idx] >= LIGHT_INFO_EVERY_MS) infoDueMask |= (uint8_t)(1u << idx);
    stateFrom = (uint8_t)idx;
    stateFlags = d[7];
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
  uint8_t mx = stateBoost;
  // Only a remote that knows us as a light (flag) sends OUR Shooting Max; older ones send their
  // general shooting level, which doesn't overwrite the setting here.
  if ((stateFlags & STATE_FLAG_LIGHT_MAX) && mx >= SHOOT_MAX_MIN && mx != lastRemoteMax) {
    lastRemoteMax = mx;
    if (settings.shootMax != mx) {
      settings.shootMax = mx;
      shootMaxSaveDue = true;
      shootMaxSaveAt = millis() + 1000;
    }
  }
  boostLevel = stateBoost;
  boostOn = stateLeftMs > 0;
  boostUntil = stateAtMs + stateLeftMs;
}

// 0x07 LIGHT_INFO to a paired remote: tells it we are a light and our Shooting Max.
void sendLightInfo(const uint8_t* mac, bool changedHere) {
  uint8_t p[LIGHT_INFO_LEN] = { PRODUCT_SIGNATURE_0, PRODUCT_SIGNATURE_1, PRODUCT_SIGNATURE_2,
                                PRODUCT_SIGNATURE_3, PACKET_TYPE_LIGHT_INFO, 0, 0,
                                settings.shootMax, (uint8_t)(changedHere ? 1 : 0) };
  esp_now_send((uint8_t*)mac, p, LIGHT_INFO_LEN);
}

// Shooting Max changed in our own web app: save, and tell the remotes so they take it over.
void setShootMaxHere(uint8_t v) {
  if (v < SHOOT_MAX_MIN) v = SHOOT_MAX_MIN;
  if (v > 100) v = 100;
  if (v == settings.shootMax) return;
  settings.shootMax = v;
  shootMaxSaveDue = true;
  shootMaxSaveAt = millis() + 1000;
  infoChangedHere = true;
  infoDueMask = 0xFF;
}

void serviceLightInfo() {
  uint8_t due = infoDueMask;
  if (due) {
    infoDueMask = 0;
    for (uint8_t i = 0; i < pairedCount; i++) {
      if (!((due >> i) & 1) || !senderActive(i)) continue;
      sendLightInfo(pairedMacs[i], infoChangedHere);
      infoSentAt[i] = millis();
    }
    infoChangedHere = false;
  }
  if (shootMaxSaveDue && (long)(millis() - shootMaxSaveAt) >= 0) { shootMaxSaveDue = false; saveSettingsNow(); }
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
  delay(PAIR_ACK_GAP_MS);
  sendLightInfo(mac, false);
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
    char name[SENDER_NAME_MAX + 1];
    memcpy(mac, helloMacs[helloTail], 6);
    memcpy(name, helloNames[helloTail], sizeof(name));
    helloTail = (helloTail + 1) % HELLO_QUEUE_LEN;
    int idx = findPaired(mac);
    if (idx >= 0 && name[0] && strcmp(pairedNames[idx], name) != 0) {   // a paired sender's name changed
      strlcpy(pairedNames[idx], name, sizeof(pairedNames[idx]));
      savePairings();
    }
    if (!pairingWindow) continue;
    if (idx < 0) {
      if (pairedCount >= MAX_PAIRED) continue;
      strlcpy(pairedNames[pairedCount], name, sizeof(pairedNames[0]));
      pairedActive |= (uint8_t)(1u << pairedCount);
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
  memset(pairedNames, 0, sizeof(pairedNames));
  pairedActive = 0xFF;
  savePairings();
  pairingWindow = false;
  showResult("Pairings", "cleared", "");
}

// Forget one sender (one-sided, like the triggers): the rest moves up one place.
void forgetSender(int idx) {
  if (idx < 0 || idx >= pairedCount) return;
  if (esp_now_is_peer_exist(pairedMacs[idx]) > 0) esp_now_del_peer(pairedMacs[idx]);
  for (uint8_t i = idx; i + 1 < pairedCount; i++) {
    memcpy(pairedMacs[i], pairedMacs[i + 1], 6);
    memcpy(pairedNames[i], pairedNames[i + 1], sizeof(pairedNames[i]));
  }
  pairedCount--;
  memset(pairedNames[pairedCount], 0, sizeof(pairedNames[0]));
  uint8_t low = pairedActive & (uint8_t)((1u << idx) - 1);
  uint8_t high = (uint8_t)(pairedActive >> (idx + 1)) << idx;
  pairedActive = low | high | (uint8_t)(0xFF << pairedCount);   // free slots default to on
  savePairings();
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
    if (wifiMode == WM_OFF) startAP(); else stopAP();
  } else {
    lightOn = !lightOn;                      // click: on / off
    if (lightOn && level == 0) level = START_LEVEL;
    source = SRC_BUTTON;
    potControl = false;                      // the pot has to move again to take over
    potRef = potF;
  }
}

// D6: on press, next preset above the current level (wraps to the first; LED off -> first).
void checkPresetButton() {
  unsigned long now = millis();
  bool down = digitalRead(PRESET_PIN) == LOW;
  if (down == presetDown || now - presetEdgeMs < BUTTON_DEBOUNCE_MS) return;
  presetEdgeMs = now;
  presetDown = down;
  if (!down) return;
  uint8_t next = PRESET_LEVELS[0];
  if (lightOn) {
    for (uint8_t i = 0; i < sizeof(PRESET_LEVELS); i++) {
      if (PRESET_LEVELS[i] > level) { next = PRESET_LEVELS[i]; break; }
    }
  }
  level = next;
  lightOn = true;
  source = SRC_BUTTON;
  potControl = false;                        // the pot has to move again to take over
  potRef = potF;
}

// =====================================================================
// OLED
// =====================================================================
void updateDisplay() {
  unsigned long now = millis();
  char big[8];
  if (outputPct <= 0) strcpy(big, "OFF");
  else snprintf(big, sizeof(big), "%d%%", outputPct);

  char l1[12] = "", l2[12] = "", l3[12] = "";
  uint8_t tier = holdTier();
  bool ipLine = false;
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
    if (wifiMode == WM_AP) { strlcpy(l3, apIP.toString().c_str(), sizeof(l3)); ipLine = true; }
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
  if (ipLine) u8g2.setFont(u8g2_font_4x6_tr);   // small, so any address fits the 48 px column
  u8g2.drawStr(80, 31, l3);
  u8g2.sendBuffer();
}

// =====================================================================
// WiFi / web app / firmware update
// =====================================================================
#include "lightpage.h"

void drawWifiScreen() {
  char l1[24], l2[24], l3[24];
  char lvl[8];
  if (outputPct <= 0) strcpy(lvl, "OFF"); else snprintf(lvl, sizeof(lvl), "%d%%", outputPct);
  strcpy(l1, "Firmware update");
  if (updState == UPD_JOINING) {
    snprintf(l2, sizeof(l2), "joining %.13s", settings.wifiSsid);
    snprintf(l3, sizeof(l3), "LED %s", lvl);
  } else if (updState == UPD_CHECKING) {
    strcpy(l2, "asking GitHub...");
    snprintf(l3, sizeof(l3), "LED %s", lvl);
  } else {
    snprintf(l2, sizeof(l2), "installing %d %%", updProgress);
    strcpy(l3, "don't switch off");
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

// While the update blocks loop(): dimmer, D6 presets, LED, fan and the OLED keep going.
void keepLightRunning() {
  checkPresetButton();
  readPot();
  updateOutput();
  drawWifiScreen();
}

void webUsed() { webLastUsed = millis(); }

// Open during the first PASSWORD_GRACE_MS after power-on (forgotten password), then protected
// if a password (8+) is set.
bool apShouldBeProtected() { return millis() >= PASSWORD_GRACE_MS && apPassword.length() >= 8; }

void applyAPNow() {
  bool prot = apShouldBeProtected();
  WiFi.softAPConfig(apIP, apIP, apMask);
  WiFi.softAP(myName, prot ? apPassword.c_str() : NULL, WIFI_CHANNEL, 0, AP_MAX_CLIENTS);
  apProtectedNow = prot;
  if (millis() >= PASSWORD_GRACE_MS) passwordApplyPending = false;
}

// The light's own network on channel 11, so ESP-NOW (the remote) keeps working.
void startAP() {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  stationStayOffline();
  applyRadioIdentity();
  applyAPNow();
  dnsServer.start(53, "*", apIP);
  server.begin();
  wifi_set_channel(WIFI_CHANNEL);
  wifiMode = WM_AP;
  webUsed();
  lastDrawKey[0] = 0;
}

void stopAP() {
  server.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  stationStayOffline();
  applyRadioIdentity();                      // the station keeps the ESP-NOW address
  wifi_set_channel(WIFI_CHANNEL);
  wifiMode = WM_OFF;
  lastDrawKey[0] = 0;
}

// End of the grace period / a new password: re-apply, but never kick a phone that is using the app.
void applyAPConfiguration() {
  bool prot = apShouldBeProtected();
  if (!passwordApplyPending && prot == apProtectedNow) return;
  if (millis() < PASSWORD_GRACE_MS) return;
  if (WiFi.softAPgetStationNum() > 0) return;
  applyAPNow();
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
  j += ",\"fan\":" + String(fanOutPct < 0 ? 0 : fanOutPct);
  j += ",\"on\":" + String(lightOn ? "true" : "false");
  j += ",\"source\":\"" + String(sourceName()) + "\"";
  j += ",\"paired\":" + String(pairedCount);
  j += ",\"shootMax\":" + String(settings.shootMax);
  j += ",\"apPassSet\":" + String(apPassword.length() >= 8 ? "true" : "false");
  j += ",\"apProtected\":" + String(apProtectedNow ? "true" : "false");
  j += ",\"apCustom\":" + String(settings.apSsid[0] ? "true" : "false");
  j += ",\"ssid\":\"" + jsonEsc(settings.wifiSsid) + "\"";
  j += ",\"passSet\":" + String(settings.wifiPass[0] ? "true" : "false");
  j += ",\"ip\":\"" + apIP.toString() + "\"";
  const char* us[] = { "idle", "joining", "checking", "installing", "uptodate", "error" };
  j += ",\"upd\":\"" + String(us[updState]) + "\"";
  j += ",\"remoteVersion\":\"" + jsonEsc(updRemoteVersion.c_str()) + "\"";
  j += ",\"updError\":\"" + jsonEsc(updError.c_str()) + "\"";
  j += ",\"progress\":" + String(updProgress) + "}";
  return j;
}

void handleRoot() { webUsed(); server.send_P(200, "text/html", PAGE_HTML); }
void handleRedirect() {
  server.sendHeader("Location", "http://" + apIP.toString() + "/", true);
  server.send(302, "text/plain", "");
}
void handleStatus() { webUsed(); server.send(200, "application/json", statusJson()); }

// Home WiFi name + password: only saved here, used by the firmware update.
void handleWifiSave() {
  webUsed();
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  ssid.trim();
  if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 64 || (pass.length() > 0 && pass.length() < 8)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"WiFi name 1-32 characters, password 8-64 characters\"}");
    return;
  }
  strlcpy(settings.wifiSsid, ssid.c_str(), sizeof(settings.wifiSsid));
  if (pass.length()) strlcpy(settings.wifiPass, pass.c_str(), sizeof(settings.wifiPass));
  EEPROM.put(0, settings);
  EEPROM.commit();
  server.send(200, "application/json", "{\"ok\":true}");
}

// ----- Paired Devices (same API as the triggers) -----
String macStr(const uint8_t* m) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}

int pairedFromArg() {
  String s = server.arg("mac");
  uint8_t m[6];
  if (sscanf(s.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6) return -1;
  return findPaired(m);
}

String sendersJson() {
  long left = pairingWindow ? (long)(pairingEnd - millis()) : 0;
  String j = "{\"pairing\":" + String(left > 0 ? (left + 999) / 1000 : 0) + ",\"senders\":[";
  for (uint8_t i = 0; i < pairedCount; i++) {
    if (i) j += ",";
    j += "{\"mac\":\"" + macStr(pairedMacs[i]) + "\",\"name\":\"" + jsonEsc(pairedNames[i]) +
         "\",\"active\":" + String(senderActive(i) ? "true" : "false") + "}";
  }
  return j + "]}";
}

void handleSenders() { webUsed(); server.send(200, "application/json", sendersJson()); }

void handleSenderActive() {
  webUsed();
  int i = pairedFromArg();
  if (i >= 0) {
    if (server.arg("active") == "1") pairedActive |= (uint8_t)(1u << i);
    else pairedActive &= (uint8_t)~(1u << i);
    savePairings();
  }
  server.send(200, "application/json", sendersJson());
}

void handleSenderForget() {
  webUsed();
  forgetSender(pairedFromArg());
  server.send(200, "application/json", sendersJson());
}

void handleClearPairings() { webUsed(); clearPairings(); server.send(200, "application/json", sendersJson()); }
void handlePair() { webUsed(); openPairingWindow(); server.send(200, "application/json", sendersJson()); }

void handleSettings() {
  webUsed();
  if (server.hasArg("shootMax")) setShootMaxHere((uint8_t)constrain(server.arg("shootMax").toInt(), SHOOT_MAX_MIN, 100));
  if (server.hasArg("apPass")) {               // own network password: 8+ characters, empty = open
    String p = server.arg("apPass");
    if (p.length() == 0 || (p.length() >= 8 && p.length() <= 64)) {
      apPassword = p;
      strlcpy(settings.apPass, p.c_str(), sizeof(settings.apPass));
      settings.apPassSaved = 1;
      saveSettingsNow();
      passwordApplyPending = true;             // goes live once nobody is connected
    }
  }
  if (server.hasArg("apSsid")) {               // own network name: empty = automatic name
    String s = server.arg("apSsid");
    s.trim();
    s.replace("\"", "");
    s.replace("\\", "");
    if (s.length() > 32) s = s.substring(0, 32);
    strlcpy(settings.apSsid, s.c_str(), sizeof(settings.apSsid));
    saveSettingsNow();
    buildName();
    apRestartRequested = true;                 // restarts the network after this answer
  }
  server.send(200, "application/json", statusJson());
}

void handleWifiOff() { webUsed(); server.send(200, "application/json", "{\"ok\":true}"); wifiOffRequested = true; }

void handleUpdate() {
  webUsed();
  if (settings.wifiPass[0] == 0 || settings.wifiSsid[0] == 0) {
    updState = UPD_ERROR;
    updError = "Save your WiFi name and password first.";
  } else if (!updateRequested) {
    updateRequested = true;                    // runs after the answer is out
    updState = UPD_JOINING;
    updError = "";
  }
  server.send(200, "application/json", statusJson());
}

bool doUpdateCheck() {
  BearSSL::WiFiClientSecure client;
  client.setInsecure();                 // GitHub over HTTPS; the image itself is checked by the updater
  client.setBufferSizes(16384, 512);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(8000);
  bool ok = false;
  if (!http.begin(client, UPDATE_BASE_URL UPDATE_VERSION_FILE)) { updError = "can't reach GitHub"; return false; }
  int code = http.GET();
  if (code == 200) {
    updRemoteVersion = http.getString();
    updRemoteVersion.trim();
    if (updRemoteVersion.length() == 0 || updRemoteVersion.length() > 40) updError = "version.txt on GitHub looks wrong";
    else ok = true;
  } else if (code == 404) {
    updError = "no firmware published on GitHub yet";
  } else {
    updError = "GitHub answered " + String(code) + " " + http.errorToString(code);
  }
  http.end();
  return ok;
}

void doUpdateInstall() {
  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  client.setBufferSizes(16384, 512);
  ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  ESPhttpUpdate.rebootOnUpdate(true);
  ESPhttpUpdate.onProgress([](int done, int total) {
    int p = total > 0 ? (int)((long long)done * 100 / total) : 0;
    if (p != updProgress) { updProgress = p; keepLightRunning(); }
  });
  t_httpUpdate_return r = ESPhttpUpdate.update(client, UPDATE_BASE_URL UPDATE_BIN_FILE);
  // only gets here if it failed (success restarts the light)
  updError = (r == HTTP_UPDATE_NO_UPDATES) ? String("no update") : ESPhttpUpdate.getLastErrorString();
}

// "Update Firmware" in the app: leave the own network, join the home WiFi, check GitHub,
// install if the version differs (restarts), otherwise come back to the own network.
void runUpdate() {
  delay(300);                                  // let the answer reach the phone
  server.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  wifiMode = WM_UPDATE;
  updState = UPD_JOINING;
  updProgress = 0;
  updRemoteVersion = "";
  updError = "";
  WiFi.mode(WIFI_STA);
  applyRadioIdentity();
  WiFi.begin(settings.wifiSsid, settings.wifiPass);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_JOIN_TIMEOUT_MS) { keepLightRunning(); delay(20); }

  if (WiFi.status() != WL_CONNECTED) {
    updState = UPD_ERROR;
    updError = "Couldn't join " + String(settings.wifiSsid) + " (wrong password or out of range?)";
  } else {
    updState = UPD_CHECKING;
    keepLightRunning();
    if (!doUpdateCheck()) updState = UPD_ERROR;
    else if (updRemoteVersion == FIRMWARE_VERSION) updState = UPD_UPTODATE;
    else {
      updState = UPD_INSTALLING;
      keepLightRunning();
      doUpdateInstall();                       // restarts the light on success
      updState = UPD_ERROR;
    }
  }
  WiFi.disconnect();
  wifi_station_disconnect();
  startAP();                                   // back to the own network, the remote works again
  if (updState == UPD_UPTODATE) showResult("Up to", "date", "");
  else showResult("Update", "failed", "see app");
}

void serviceWifi() {
  if (wifiMode != WM_AP) return;
  dnsServer.processNextRequest();
  if (WiFi.softAPgetStationNum() > 0) webUsed();
  server.handleClient();
  if (updateRequested) { updateRequested = false; runUpdate(); return; }
  if (wifiOffRequested) { wifiOffRequested = false; delay(200); stopAP(); return; }
  if (apRestartRequested) { apRestartRequested = false; delay(300); applyAPNow(); }
  applyAPConfiguration();
  if (!WIFI_ALWAYS_ON && millis() - webLastUsed >= WIFI_IDLE_OFF_MS) stopAP();
}

// =====================================================================
// Setup / loop
// =====================================================================
void setup() {
  pinMode(PWM_PIN, OUTPUT);
  digitalWrite(PWM_PIN, PWM_ACTIVE_HIGH ? LOW : HIGH);
  analogWriteRange(PWM_RANGE);
  analogWriteFreq(PWM_FREQ_HZ);
  pinMode(FAN_PIN, OUTPUT);
  setFan(100);                               // start kick, see FAN_KICK_MS
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(PRESET_PIN, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
  u8g2.setBusClock(400000);

  loadPairings();

  readRadioMacs();                           // before any MAC is changed
  buildName();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  stationStayOffline();
  applyRadioIdentity();
  WiFi.setSleepMode(WIFI_NONE_SLEEP);        // react at once to the remote
  WiFi.setOutputPower(TX_POWER_DBM);
  wifi_set_channel(WIFI_CHANNEL);
  for (uint8_t i = 0; i < MAX_PAIRED; i++) infoSentAt[i] = (unsigned long)(0 - LIGHT_INFO_EVERY_MS);

  if (esp_now_init() == 0) {
    esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);   // sends on the station: works with the own network off
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
  server.on("/api/update", HTTP_POST, handleUpdate);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/senders", HTTP_GET, handleSenders);
  server.on("/api/sender-active", HTTP_POST, handleSenderActive);
  server.on("/api/sender-forget", HTTP_POST, handleSenderForget);
  server.on("/api/clear-pairings", HTTP_POST, handleClearPairings);
  server.on("/api/pair", HTTP_POST, handlePair);
  server.on("/api/wifi-off", HTTP_POST, handleWifiOff);
  server.on("/library/test/success.html", handleRedirect);
  server.onNotFound(handleRedirect);           // captive portal: everything else -> the app

  if (WIFI_ALWAYS_ON) startAP();
  if (pairedCount == 0) openPairingWindow(); // nothing paired yet: ready to pair at once
  infoDueMask = 0xFF;                        // tell the paired remotes we are a light
  updateDisplay();
}

void loop() {
  applyRemoteState();        // first: shooting light must not wait
  updateOutput();
  checkButton();
  checkPresetButton();
  readPot();
  processPairing();
  serviceLightInfo();
  updateOutput();
  serviceWifi();
  updateOutput();
  updateDisplay();           // only redraws when the text changed (~15 ms)
}
