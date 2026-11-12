// ============================================================
// ESP32_C6_ACCESS.ino — Zarlar buitenverlichting (ACCESS1, ACCESS2, ACCESS3 …)
// Filip Delannoy (FiDel), Zarlardinge — https://github.com/FidelDworp/ESP32C6_ACCESS
// Standaard: docs/ACCESS_STANDAARD.md (één sketch voor alle ACCESS-controllers;
// wat per controller verschilt is een instelling in /settings, bewaard in NVS)
//
// Compileren (Arduino IDE):
//   Board ESP32C6 Dev Module · Flash Size 16MB · Partition Scheme Custom (partitions.csv)
//   USB CDC On Boot: Disabled  (IO12/IO13 zijn op het shield gewone pinnen; seriële
//   monitor via de poort UART). Bibliotheken: ESP32Async/ESPAsyncWebServer + AsyncTCP.
// Updaten: OTA via http://<ip>/update met bin/ESP32_C6_ACCESS_vX.Y.ino.bin
//
// ── VERSIEHISTORIE ──────────────────────────────────────────
// v0.2 (12nov26): ZONES (standaard §2.1, §3.1).
//   - AUTO: beweging → licht aan op de helderheid van de zone, uit na de brandtijd sinds de
//     laatste beweging. Dag/nacht komt in v0.3 (lux): tot dan de instelling "dag/nacht"
//     (standaard ALTIJD NACHT, om te testen). In MANUEEL starten PIR's geen licht; wat door
//     een PIR brandde, gaat uit na zijn brandtijd.
//   - MANUEEL (override, één modus per controller, au): /set_zone?z=<zone>&v=1[&b=0..100] zet
//     een zone manueel aan; na de override-tijd (standaard 30 min) of bij v=0 (zone uit) gaat
//     de hele controller terug naar AUTO.
//   - /settings: per zone aan/uit, helderheid (100 %), brandtijd (30 s); per controller
//     override-tijd en dag/nacht. Statuspagina: knoppen Aan/Uit + helderheidsschuifje per zone,
//     modus en resterende override-tijd.
//   - FIX: PIR-ingangen met interne pull-down (zwevende ingangen gaven duizenden valse
//     bewegingen); slots zonder zonecode of uitgeschakeld worden niet gelezen.
//   - /json: echte l<zone>, au en nt. Geen nieuwe keys. /set_test blijft (test, overschrijft
//     de zone tot /set_test?b=-1 of een /set_zone), niet meer op de pagina.
// v0.1.1 (12nov26): PINVERDELING NA DE PIR-TEST (standaard §1.1 / §6.2). Een AM312 op IO3
//   werkte niet: zijn uitgang kan de 4,7 kΩ pull-up van de DS18B20-bus op het shield niet laag
//   trekken (rust 2,71 V). PIR's nu op pinnen zonder pull-up: slot 1 IO5, slot 2 IO12,
//   slot 3 IO2, slot 4 IO7. PWM-pinnen ongewijzigd (IO19, IO18, IO10, IO1).
//   Reed-contacten (deuren) op IO3 en IO6, die al een pull-up op het shield hebben: deurcode
//   en "contact gesloten = deur dicht/open" per ingang in /settings, ontdenderd, alleen
//   getoond (nog niet in /json; dat is v0.5).
//   Statuspagina ververst vanzelf om de 2 s (/status_json, geen contract) en heeft nu de
//   TEST-schuifjes (weg uit /settings).
// v0.1 (11nov26): BASIS. WiFi (vast IP, geen modemslaap, herverbinden), AP-modus
//   "ACCESS-<naam>" met captive portal als WiFi niet lukt, mDNS <naam>.local, OTA via
//   /update, /settings (naam, WiFi, IP, PWM-frequentie, zonecode en polariteit per zoneslot),
//   Diagnose (herstartreden, opstarts, laatste stap vóór herstart, laatste 3 herstarts,
//   geheugen, WiFi), /json met de keys van ACCESS_STANDAARD §3.1 (m<zone>, l<zone>, au …;
//   alleen de zones die een zonecode hebben), nog zonder zonelogica.
//   Zoneslots 1–4: de PWM-uitgangen staan vanaf de opstart op UIT; de PIR-ingangen worden
//   gelezen en getoond. TEST-schuifjes per zone (niet bewaard, /set_test) om de LED-drivers
//   op 3,3 V en de PWM-frequentie uit te proberen. Nog geen AUTO/MANUEEL (v0.2) en geen
//   dag/nacht of lux (v0.3).
// ============================================================

#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_attr.h>
#include "hal/usb_serial_jtag_ll.h"

#if ARDUINO_USB_CDC_ON_BOOT
#error "ACCESS: zet in de Arduino IDE Tools -> USB CDC On Boot op Disabled (seriele monitor via de poort UART)"
#endif

#define ACCESS_VERSION "v0.2"   // enige plaats met het versienummer

// ── ZONESLOTS (ACCESS_STANDAARD §1.1) ───────────────────────
//            slot:      1    2    3    4
const uint8_t PIR_PIN[4] = { 5,  12,   2,   7 };   // RoomSense 2 · RoomSense 4 · Option 3 · RoomSense 5 (v0.1.1)
const uint8_t PWM_PIN[4] = { 19, 18,  10,   1 };   // Option 5    · Option 4    · Option 2 · RoomSense 6
#define NUM_ZONES   4
// Deurcontacten (reed naar GND); beide pinnen hebben een pull-up op het shield (v0.1.1)
#define NUM_DOORS   2
const uint8_t DOOR_PIN[NUM_DOORS] = { 3, 6 };      // RoomSense 1 · RoomSense 3
#define PWM_BITS    10                              // 0..1023
#define PWM_FULL    (1 << PWM_BITS)              // 1024 = constant hoog (echt 100 %, geen tik laag)
#define I2C_SDA     13                              // v0.3: TSL2561
#define I2C_SCL     11
#define MOTION_RECENT_MS 600000UL                   // "beweging in de laatste 10 min" (z1m…)

// ── INSTELLINGEN (NVS "access-cfg") ─────────────────────────
Preferences prefs;
char ctrl_name[24]  = "ACCESS1";
char wifi_ssid[33]  = "netwerknaam";
char wifi_pass[65]  = "paswoord";
char static_ip[16]  = "192.168.0.82";
char mdns_name[24]  = "access1";
uint16_t pwm_freq   = 1000;                         // Hz (standaard §6: starten op 1 kHz)
bool active_low[NUM_ZONES] = { true, true, true, true };  // 0 V = licht aan (zoals R8-ACCESS)
// Zonecode per slot (ACCESS_STANDAARD §1.1.1) = de enige naam van de zone overal.
// Standaard die van ACCESS1: slot 1 D (voordeur), slot 2 W4 (veldkant), slot 3 S (terras), slot 4 vrij.
char zone_code[NUM_ZONES][4] = { "D", "W4", "S", "" };
// Deurcode per reed-ingang (standaard §4.1). Standaard die van ACCESS1: SD (schuifdeur), VD (voordeur).
char door_code[NUM_DOORS][4] = { "SD", "VD" };
bool door_closed_low[NUM_DOORS] = { true, true };   // contact gesloten (0 V) = deur dicht
// v0.2: zones en modus
bool     zone_en[NUM_ZONES]     = { true, true, true, true };   // zone aan/uit
uint8_t  zone_bright[NUM_ZONES] = { 100, 100, 100, 100 };       // helderheid in AUTO (%)
uint16_t zone_burn_s[NUM_ZONES] = { 30, 30, 30, 30 };           // brandtijd na de laatste beweging (s)
uint16_t override_min = 30;                                     // MANUEEL → terug naar AUTO na … min
bool     force_night  = true;                                   // tot v0.3: dag/nacht vast (true = altijd nacht)

// ── TOESTAND ────────────────────────────────────────────────
uint8_t  out_pct[NUM_ZONES]   = { 0, 0, 0, 0 };    // helderheid nu (0–100 %); v0.1: alleen TEST
bool     pir_now[NUM_ZONES]   = { false, false, false, false };
bool     pir_seen[NUM_ZONES]  = { false, false, false, false };
uint32_t pir_last[NUM_ZONES]  = { 0, 0, 0, 0 };
uint32_t pir_count[NUM_ZONES] = { 0, 0, 0, 0 };    // aantal keer beweging sinds opstart
bool     door_open[NUM_DOORS]  = { false, false };   // ontdenderde toestand
// v0.2: modus en zonetoestand
bool     auto_mode = true;                           // au: true = AUTO, false = MANUEEL
uint32_t override_until = 0;                         // millis waarop MANUEEL afloopt
bool     zone_manual[NUM_ZONES]   = { false, false, false, false };  // manueel aan
uint8_t  zone_manual_b[NUM_ZONES] = { 100, 100, 100, 100 };
bool     pir_lit[NUM_ZONES]       = { false, false, false, false };  // brandt door een PIR
uint32_t pir_off_at[NUM_ZONES]    = { 0, 0, 0, 0 };
int8_t   test_pct[NUM_ZONES]      = { -1, -1, -1, -1 };  // /set_test: −1 = geen test
uint8_t  door_raw_n[NUM_DOORS] = { 0, 0 };           // aantal opeenvolgende afwijkende lezingen
bool     door_seen[NUM_DOORS]  = { false, false };
uint32_t door_last[NUM_DOORS]  = { 0, 0 };           // laatste opening (millis)
uint32_t door_count[NUM_DOORS] = { 0, 0 };           // aantal openingen sinds opstart
bool ap_mode = false;
bool mdns_ok = false;
DNSServer dnsServer;
AsyncWebServer server(80);
volatile bool restart_requested = false;
uint32_t restart_at = 0;

// ── DIAGNOSE: herstartreden, opstarts, laatste stap (zoals ROOM v3.3+) ──
esp_reset_reason_t boot_reason = ESP_RST_UNKNOWN;
uint32_t boot_count = 0;
char prev_step[96] = "";
char hist[3][160] = { "", "", "" };

const char* resetReasonText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "Stroom aangezet";
    case ESP_RST_EXT:       return "Reset-knop / extern";
    case ESP_RST_SW:        return "Software (OTA, instellingen of herstart)";
    case ESP_RST_PANIC:     return "CRASH (fout in de software)";
    case ESP_RST_INT_WDT:   return "Watchdog (interrupt)";
    case ESP_RST_TASK_WDT:  return "Watchdog (taak hing vast)";
    case ESP_RST_WDT:       return "Watchdog (andere)";
    case ESP_RST_DEEPSLEEP: return "Uit diepe slaap";
    case ESP_RST_BROWNOUT:  return "Spanningsdip (brown-out)";
    default:                return "Onbekend";
  }
}

enum CrumbLoop : uint8_t { CL_NONE = 0, CL_SETUP, CL_WIFI, CL_PIR, CL_OUT, CL_IDLE, CL_COUNT };
const char* const CRUMB_LOOP_TXT[CL_COUNT] = {
  "onbekend", "opstarten (setup)", "WiFi-controle", "PIR lezen", "uitgangen", "wachten (rust)"
};
enum CrumbWeb : uint8_t { CW_NONE = 0, CW_HOME, CW_JSON, CW_SETTINGS, CW_SAVE, CW_OTA, CW_TEST, CW_OTHER, CW_ZONE, CW_COUNT };
const char* const CRUMB_WEB_TXT[CW_COUNT] = {
  "geen", "/ (status)", "/json", "/settings", "/save_settings", "/update (OTA)", "/set_test", "andere opdracht", "/set_zone"
};
#define CRUMB_MAGIC 0x5A414331UL   // "ZAC1"
RTC_NOINIT_ATTR uint32_t crumb_magic;
RTC_NOINIT_ATTR uint8_t  crumb_loop;
RTC_NOINIT_ATTR uint8_t  crumb_web;
RTC_NOINIT_ATTR uint32_t crumb_web_s;
RTC_NOINIT_ATTR uint32_t crumb_up_s;
static inline void crumbL(uint8_t c) { crumb_loop = c; }
static inline void crumbW(uint8_t c) { crumb_web = c; crumb_web_s = millis() / 1000; }

// IO12/IO13 los van de ingebouwde USB-PHY (zoals ROOM v3.4.2)
void releaseUsbPins() {
  usb_serial_jtag_pull_override_vals_t none = {};
  usb_serial_jtag_ll_phy_enable_pull_override(&none);
  usb_serial_jtag_ll_phy_enable_pad(false);
}

// ── UITGANGEN ───────────────────────────────────────────────
// Licht UIT = het niveau dat de driver uitzet: bij actief laag is dat 3,3 V (HIGH).
void outputsSafeEarly() {
  for (int z = 0; z < NUM_ZONES; z++) {
    pinMode(PWM_PIN[z], OUTPUT);
    digitalWrite(PWM_PIN[z], HIGH);     // standaard actief laag → HIGH = uit
  }
}

void setOutput(int z, int pct) {
  pct = constrain(pct, 0, 100);
  out_pct[z] = pct;
  uint32_t duty = (uint32_t)pct * PWM_FULL / 100;    // aandeel "aan"
  if (active_low[z]) duty = PWM_FULL - duty;          // aan = laag; 0 % → constant hoog
  ledcWrite(PWM_PIN[z], duty);
}

void outputsInit() {
  for (int z = 0; z < NUM_ZONES; z++) {
    ledcAttach(PWM_PIN[z], pwm_freq, PWM_BITS);
    setOutput(z, 0);
  }
}

// ── NVS ─────────────────────────────────────────────────────
void makeMdnsName() {   // "ACCESS1" → "access1" (alleen letters, cijfers en '-')
  size_t j = 0;
  for (size_t i = 0; ctrl_name[i] && j < sizeof(mdns_name) - 1; i++) {
    char c = ctrl_name[i];
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') mdns_name[j++] = c;
  }
  mdns_name[j] = 0;
  if (!j) strlcpy(mdns_name, "access", sizeof(mdns_name));
}

void loadSettings() {
  prefs.begin("access-cfg", false);
  prefs.getString("name", ctrl_name, sizeof(ctrl_name));
  prefs.getString("ssid", wifi_ssid, sizeof(wifi_ssid));
  prefs.getString("pass", wifi_pass, sizeof(wifi_pass));
  prefs.getString("ip",   static_ip, sizeof(static_ip));
  pwm_freq = constrain(prefs.getUShort("pwm_hz", 1000), 100, 20000);
  for (int z = 0; z < NUM_ZONES; z++) {
    char k[8]; snprintf(k, sizeof(k), "pol%d", z + 1);
    active_low[z] = prefs.getBool(k, true);
    snprintf(k, sizeof(k), "zc%d", z + 1);
    if (prefs.isKey(k)) prefs.getString(k, zone_code[z], sizeof(zone_code[z]));
    snprintf(k, sizeof(k), "ze%d", z + 1); zone_en[z]     = prefs.getBool(k, true);
    snprintf(k, sizeof(k), "zb%d", z + 1); zone_bright[z] = constrain(prefs.getUChar(k, 100), 1, 100);
    snprintf(k, sizeof(k), "zt%d", z + 1); zone_burn_s[z] = constrain(prefs.getUShort(k, 30), 5, 3600);
  }
  override_min = constrain(prefs.getUShort("ov_min", 30), 1, 240);
  force_night  = prefs.getBool("nf", true);
  for (int d = 0; d < NUM_DOORS; d++) {
    char k[8]; snprintf(k, sizeof(k), "dc%d", d + 1);
    if (prefs.isKey(k)) prefs.getString(k, door_code[d], sizeof(door_code[d]));
    snprintf(k, sizeof(k), "dl%d", d + 1);
    door_closed_low[d] = prefs.getBool(k, true);
  }
  makeMdnsName();
}

// ── WIFI ────────────────────────────────────────────────────
void startWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                 // geen modemslaap: stabieler en sneller antwoord
  WiFi.setHostname(mdns_name);
  IPAddress ip;
  if (ip.fromString(static_ip)) {
    IPAddress gw = ip; gw[3] = 1;
    WiFi.config(ip, gw, IPAddress(255, 255, 255, 0), gw);
  }
  WiFi.begin(wifi_ssid, wifi_pass);
  Serial.printf("[WiFi] Verbinden met %s ", wifi_ssid);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print('.'); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Verbonden: %s (%d dBm)\n", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    mdns_ok = MDNS.begin(mdns_name);
    if (mdns_ok) { MDNS.addService("http", "tcp", 80); Serial.printf("[mDNS] http://%s.local\n", mdns_name); }
    return;
  }
  // Geen WiFi: eigen toegangspunt met captive portal, instellen via http://192.168.4.1/settings
  WiFi.mode(WIFI_AP_STA);
  char ap[40]; snprintf(ap, sizeof(ap), "ACCESS-%s", ctrl_name);
  WiFi.softAP(ap);
  IPAddress ap_ip(192, 168, 4, 1);
  WiFi.softAPConfig(ap_ip, ap_ip, IPAddress(255, 255, 255, 0));
  dnsServer.start(53, "*", ap_ip);
  ap_mode = true;
  Serial.printf("\n[WiFi] Mislukt → toegangspunt %s, http://192.168.4.1/settings\n", ap);
}

void checkWiFi() {                      // elke 10 s; herverbinden hoogstens om de 30 s
  static uint32_t last_check = 0, last_try = 0;
  if (ap_mode || millis() - last_check < 10000UL) return;
  last_check = millis();
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - last_try < 30000UL) return;
  last_try = millis();
  Serial.println("[WiFi] Verbinding weg → opnieuw verbinden");
  WiFi.reconnect();
}

// ── PIR ─────────────────────────────────────────────────────
void readDoors();                       // hieronder
bool zoneUsed(int z) { return zone_code[z][0] && zone_en[z]; }

void readPIRs() {                       // elke 50 ms
  static uint32_t last = 0;
  if (millis() - last < 50) return;
  last = millis();
  for (int z = 0; z < NUM_ZONES; z++) {
    bool v = zoneUsed(z) && digitalRead(PIR_PIN[z]) == HIGH;   // v0.2: alleen gebruikte zones
    if (v && !pir_now[z]) pir_count[z]++;
    if (v) { pir_seen[z] = true; pir_last[z] = millis(); }
    pir_now[z] = v;
  }
  readDoors();
}

// ── DEURCONTACTEN ───────────────────────────────────────────
bool doorReadOpen(int d) {
  bool low = digitalRead(DOOR_PIN[d]) == LOW;
  return door_closed_low[d] ? !low : low;
}

void readDoors() {                      // samen met de PIR's, elke 50 ms; 2 gelijke lezingen = ontdenderd
  for (int d = 0; d < NUM_DOORS; d++) {
    if (!door_code[d][0]) { door_open[d] = false; continue; }
    bool o = doorReadOpen(d);
    if (o == door_open[d]) { door_raw_n[d] = 0; continue; }
    if (++door_raw_n[d] < 2) continue;
    door_raw_n[d] = 0;
    door_open[d] = o;
    if (o) { door_seen[d] = true; door_last[d] = millis(); door_count[d]++; }
    Serial.printf("[DEUR] %s %s\n", door_code[d], o ? "open" : "dicht");
  }
}

int motionRecent(int z) {
  return (pir_seen[z] && millis() - pir_last[z] < MOTION_RECENT_MS) ? 1 : 0;
}

// ── ZONES (v0.2, standaard §2.1) ────────────────────────────
bool isNight() { return force_night; }      // v0.3: lux, andere ACCESS of berekende zon

int zoneByCode(const String& c) {
  for (int z = 0; z < NUM_ZONES; z++)
    if (zone_code[z][0] && c.equalsIgnoreCase(zone_code[z])) return z;
  return -1;
}

void backToAuto(const char* why) {
  auto_mode = true;
  for (int z = 0; z < NUM_ZONES; z++) zone_manual[z] = false;
  Serial.printf("[MODUS] AUTO (%s)\n", why);
}

void updateZones() {                    // elke 50 ms, na readPIRs()
  static uint32_t last = 0;
  if (millis() - last < 50) return;
  last = millis();
  uint32_t now = millis();
  if (!auto_mode && (int32_t)(now - override_until) >= 0) backToAuto("override-tijd voorbij");
  bool night = isNight();
  for (int z = 0; z < NUM_ZONES; z++) {
    int target = 0;
    if (zoneUsed(z)) {
      if (pir_now[z] && auto_mode && night) {               // AUTO: beweging + nacht
        pir_lit[z] = true;
        pir_off_at[z] = now + (uint32_t)zone_burn_s[z] * 1000UL;
      } else if (pir_now[z] && pir_lit[z]) {                // brandt al: brandtijd verlengen
        pir_off_at[z] = now + (uint32_t)zone_burn_s[z] * 1000UL;
      }
      if (pir_lit[z] && (int32_t)(now - pir_off_at[z]) >= 0) pir_lit[z] = false;
      if (zone_manual[z])  target = zone_manual_b[z];
      else if (pir_lit[z]) target = zone_bright[z];
    } else {
      pir_lit[z] = false; zone_manual[z] = false;
    }
    if (test_pct[z] >= 0) target = test_pct[z];             // /set_test gaat voor
    if (target != out_pct[z]) setOutput(z, target);
  }
}

// ── WEB: gedeelde opmaak ────────────────────────────────────
void pageHead(AsyncResponseStream* p, const char* title, const char* active) {
  p->print(F("<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>"));
  p->print(title);
  p->print(F("</title><style>"
    "body{font-family:Arial,sans-serif;background:#fff;margin:0}"
    ".hd{display:flex;background:#ffcc00;color:#000;padding:10px 15px;font-size:18px;font-weight:bold}"
    ".hd .r{flex:1;text-align:right;font-size:14px;font-weight:normal}"
    ".nav{display:flex;flex-wrap:wrap;gap:6px;padding:8px 10px;border-bottom:3px solid #c00}"
    ".nav a{background:#369;color:#fff;padding:7px 12px;text-decoration:none;font-weight:bold;font-size:13px;border-radius:6px}"
    ".nav a.on{background:#c00}"
    ".m{padding:12px 15px;max-width:760px}"
    "h3{color:#369;margin:18px 0 6px}"
    "table{border-collapse:collapse;width:100%}td,th{padding:5px 6px;border-bottom:1px solid #ddd;font-size:14px;text-align:left}"
    "th{background:#f3f3f3}input[type=text],input[type=password],input[type=number]{width:100%;box-sizing:border-box;padding:5px}"
    ".btn{background:#369;color:#fff;padding:9px 18px;border:none;border-radius:7px;font-size:15px;cursor:pointer;margin:6px 4px 0 0}"
    ".red{background:#c00}.ok{color:#080;font-weight:bold}.bad{color:#c00;font-weight:bold}.dim{color:#888}"
    "</style></head><body><div class=\"hd\"><div>"));
  p->print(ctrl_name);
  p->print(F("<span style=\"font-size:12px;font-weight:normal;opacity:.75\"> &middot; ACCESS " ACCESS_VERSION "</span></div><div class=\"r\">"));
  p->printf("%lu s", (unsigned long)(millis() / 1000));
  p->print(F("</div></div><div class=\"nav\">"));
  const char* links[][2] = { {"/", "Status"}, {"/settings", "Settings"}, {"/json", "JSON"}, {"/update", "OTA"} };
  for (auto& l : links)
    p->printf("<a href=\"%s\"%s>%s</a>", l[0], strcmp(l[0], active) == 0 ? " class=\"on\"" : "", l[1]);
  p->print(F("</div><div class=\"m\">"));
}

void pageFoot(AsyncResponseStream* p) {
  p->print(F("</div></body></html>"));
}

// ── WEB: status ("/") — ververst zichzelf via /status_json ──
void handleStatus(AsyncWebServerRequest* req) {
  crumbW(CW_HOME);
  AsyncResponseStream* p = req->beginResponseStream("text/html; charset=utf-8");
  pageHead(p, "ACCESS", "/");
  p->print(F("<h3>Zones</h3><p>Modus: <b id=\"mo\">&hellip;</b> &middot; <span id=\"nt\"></span></p>"
             "<table><tr><th>Zone</th><th>PIR</th><th>Beweging</th><th>Licht</th></tr>"));
  for (int z = 0; z < NUM_ZONES; z++) {
    if (!zone_code[z][0]) continue;                   // vrije slots niet tonen
    p->printf("<tr><td><b>%s</b><br><span class=\"dim\">slot %d%s</span></td><td>IO%d: <span id=\"p%d\">&hellip;</span></td><td id=\"m%d\">&hellip;</td>"
              "<td><span id=\"b%d\">%d %%</span> <button class=\"btn\" style=\"padding:4px 10px\" onclick=\"zs(%d,1)\">Aan</button>"
              "<button class=\"btn red\" style=\"padding:4px 10px\" onclick=\"zs(%d,0)\">Uit</button><br>"
              "<input type=\"range\" min=\"1\" max=\"100\" value=\"%d\" id=\"r%d\" style=\"width:90%%\" "
              "oninput=\"document.getElementById('b%d').textContent=this.value+' %%'\" onchange=\"zs(%d,1,this.value)\"></td></tr>",
              zone_code[z], z + 1, zone_en[z] ? "" : " &middot; uit", PIR_PIN[z], z, z,
              z, out_pct[z], z, z, zone_manual_b[z], z, z, z);
  }
  p->print(F("</table><h3>Deuren</h3><table><tr><th>Ingang</th><th>Deur</th><th>Toestand</th><th>Laatst open</th></tr>"));
  for (int d = 0; d < NUM_DOORS; d++)
    p->printf("<tr><td>IO%d</td><td><b>%s</b></td><td id=\"d%d\">&hellip;</td><td id=\"e%d\">&hellip;</td></tr>",
              DOOR_PIN[d], door_code[d][0] ? door_code[d] : "(vrij)", d, d);
  p->print(F("</table><p class=\"dim\">Aan = zone manueel aan (de controller gaat naar MANUEEL); Uit = zone uit en terug naar AUTO. "
             "Dag/nacht met lux volgt in v0.3, deuren in /json in v0.5.</p>"));
  p->print(F("<h3>Netwerk</h3><table>"));
  p->printf("<tr><td>IP</td><td>%s</td></tr><tr><td>Naam</td><td>%s</td></tr><tr><td>WiFi</td><td id=\"w\">%d dBm</td></tr>",
            ap_mode ? "192.168.4.1 (toegangspunt)" : WiFi.localIP().toString().c_str(),
            mdns_ok ? (String(mdns_name) + ".local").c_str() : "&mdash;", (int)WiFi.RSSI());
  p->print(F("</table><script>var CODES=["));
  for (int z = 0; z < NUM_ZONES; z++) p->printf("%s'%s'", z ? "," : "", zone_code[z]);
  p->print(F("];"
    "function ago(s){return s<0?'<span class=\"dim\">nog niet</span>':(s<120?s+' s':Math.round(s/60)+' min')+' geleden';}"
    "function zs(i,v,b){var c=CODES[i];fetch('/set_zone?z='+c+'&v='+v+(b?'&b='+b:'')).then(upd);}"
    "function upd(){fetch('/status_json').then(r=>r.json()).then(d=>{"
      "document.getElementById('mo').innerHTML=d.au?'AUTO':'<span class=\"bad\">MANUEEL</span> (nog '+Math.ceil(d.ov/60)+' min)';"
      "document.getElementById('nt').textContent=(d.nt?'nacht':'dag')+' (vast, tot v0.3)';"
      "d.z.forEach((z,i)=>{if(!document.getElementById('p'+i))return;"
        "document.getElementById('p'+i).innerHTML=z.p?'<span class=\"ok\">beweging</span>':'rust';"
        "document.getElementById('m'+i).innerHTML=ago(z.a)+(z.n?' &middot; '+z.n+'&times;':'');"
        "document.getElementById('b'+i).innerHTML=z.l+' %'+(z.m?' <span class=\"bad\">manueel</span>':(z.k?' <span class=\"ok\">PIR</span>':''))+(z.t?' <span class=\"bad\">TEST</span>':'');"
      "});"
      "d.d.forEach((x,i)=>{"
        "document.getElementById('d'+i).innerHTML=x.c?(x.o?'<span class=\"bad\">OPEN</span>':'<span class=\"ok\">dicht</span>'):'<span class=\"dim\">niet gebruikt</span>';"
        "document.getElementById('e'+i).innerHTML=x.c?ago(x.a)+(x.n?' &middot; '+x.n+'&times;':''):'';"
      "});"
      "document.getElementById('w').textContent=d.w+' dBm';"
    "}).catch(()=>{});}"
    "upd();setInterval(upd,2000);</script>"));
  pageFoot(p);
  req->send(p);
}

// Gegevens voor de statuspagina (geen contract: kan wijzigen)
void handleStatusJson(AsyncWebServerRequest* req) {
  char b[700];
  int n = snprintf(b, sizeof(b), "{\"z\":[");
  for (int z = 0; z < NUM_ZONES; z++)
    n += snprintf(b + n, sizeof(b) - n, "%s{\"p\":%d,\"a\":%ld,\"n\":%lu,\"l\":%d,\"m\":%d,\"k\":%d,\"t\":%d}", z ? "," : "",
                  pir_now[z] ? 1 : 0, pir_seen[z] ? (long)((millis() - pir_last[z]) / 1000) : -1L,
                  (unsigned long)pir_count[z], out_pct[z], zone_manual[z] ? 1 : 0, pir_lit[z] ? 1 : 0,
                  test_pct[z] >= 0 ? 1 : 0);
  n += snprintf(b + n, sizeof(b) - n, "],\"d\":[");
  for (int d = 0; d < NUM_DOORS; d++)
    n += snprintf(b + n, sizeof(b) - n, "%s{\"c\":%d,\"o\":%d,\"a\":%ld,\"n\":%lu}", d ? "," : "",
                  door_code[d][0] ? 1 : 0, door_open[d] ? 1 : 0,
                  door_seen[d] ? (long)((millis() - door_last[d]) / 1000) : -1L, (unsigned long)door_count[d]);
  long ov = auto_mode ? 0 : (long)((int32_t)(override_until - millis()) / 1000);
  snprintf(b + n, sizeof(b) - n, "],\"w\":%d,\"au\":%d,\"ov\":%ld,\"nt\":%d}", (int)WiFi.RSSI(),
           auto_mode ? 1 : 0, ov < 0 ? 0 : ov, isNight() ? 1 : 0);
  req->send(200, "application/json", b);
}

// ── WEB: /json (ACCESS_STANDAARD §3.1) ──────────────────────
void handleJson(AsyncWebServerRequest* req) {
  crumbW(CW_JSON);
  char b[420];
  int n = snprintf(b, sizeof(b), "{\"a\":%lu", (unsigned long)(millis() / 1000));
  for (int z = 0; z < NUM_ZONES; z++) {             // alleen zones met een code (§3.1)
    if (!zone_code[z][0]) continue;
    char lc[4]; strlcpy(lc, zone_code[z], sizeof(lc));
    for (char* c = lc; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 'a' - 'A';
    n += snprintf(b + n, sizeof(b) - n, ",\"m%s\":%d,\"l%s\":%d", lc, motionRecent(z), lc, out_pct[z]);
  }
  uint32_t heap = ESP.getFreeHeap(), total = ESP.getHeapSize();
  n += snprintf(b + n, sizeof(b) - n,
    ",\"au\":%d,\"lux\":-1,\"nt\":%d,\"lb\":-1,\"ac\":%d,\"ad\":%u,\"ae\":%u,\"af\":%u}",
    auto_mode ? 1 : 0, isNight() ? 1 : 0, (int)WiFi.RSSI(), total ? (unsigned)(heap * 100UL / total) : 0,
    (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getMinFreeHeap());
  req->send(200, "application/json", b);
}

// ── WEB: /settings ──────────────────────────────────────────
void handleSettings(AsyncWebServerRequest* req) {
  crumbW(CW_SETTINGS);
  AsyncResponseStream* p = req->beginResponseStream("text/html; charset=utf-8");
  pageHead(p, "Settings", "/settings");
  p->print(F("<form action=\"/save_settings\" method=\"GET\"><h3>Controller en netwerk</h3><table>"));
  p->printf("<tr><td>Naam</td><td><input type=\"text\" name=\"name\" maxlength=\"23\" value=\"%s\"></td></tr>", ctrl_name);
  p->printf("<tr><td>WiFi-netwerk</td><td><input type=\"text\" name=\"ssid\" maxlength=\"32\" value=\"%s\"></td></tr>", wifi_ssid);
  p->print(F("<tr><td>WiFi-paswoord</td><td><input type=\"password\" name=\"pass\" maxlength=\"64\" placeholder=\"(ongewijzigd)\"></td></tr>"));
  p->printf("<tr><td>Vast IP</td><td><input type=\"text\" name=\"ip\" maxlength=\"15\" value=\"%s\"></td></tr>", static_ip);
  p->print(F("</table><h3>Uitgangen</h3><table>"));
  p->printf("<tr><td>PWM-frequentie (Hz)</td><td><input type=\"number\" name=\"hz\" min=\"100\" max=\"20000\" value=\"%u\"></td></tr>", pwm_freq);
  for (int z = 0; z < NUM_ZONES; z++)
    p->printf("<tr><td>Slot %d (PIR IO%d, licht IO%d)</td><td>zonecode <input type=\"text\" name=\"zc%d\" maxlength=\"3\" value=\"%s\" "
              "style=\"width:4em\" placeholder=\"vrij\"> &middot; licht aan bij "
              "<label><input type=\"radio\" name=\"pol%d\" value=\"1\"%s> 0 V</label> "
              "<label><input type=\"radio\" name=\"pol%d\" value=\"0\"%s> 3,3 V</label><br>"
              "zone <select name=\"ze%d\"><option value=\"1\"%s>aan</option><option value=\"0\"%s>uit</option></select> &middot; "
              "helderheid <input type=\"number\" name=\"zb%d\" min=\"1\" max=\"100\" value=\"%u\" style=\"width:4.5em\"> %% &middot; "
              "brandtijd <input type=\"number\" name=\"zt%d\" min=\"5\" max=\"3600\" value=\"%u\" style=\"width:5em\"> s</td></tr>",
              z + 1, PIR_PIN[z], PWM_PIN[z], z + 1, zone_code[z],
              z + 1, active_low[z] ? " checked" : "", z + 1, active_low[z] ? "" : " checked",
              z + 1, zone_en[z] ? " selected" : "", zone_en[z] ? "" : " selected",
              z + 1, zone_bright[z], z + 1, zone_burn_s[z]);
  p->print(F("<tr><td colspan=\"2\" class=\"dim\">Zonecode = de naam van de zone overal (ACCESS1: D, W4, S; ACCESS2: W1, N1; ACCESS3: N2, E). Leeg = slot niet gebruikt.</td></tr>"));
  p->printf("<tr><td>Override-tijd (MANUEEL → AUTO)</td><td><input type=\"number\" name=\"ov\" min=\"1\" max=\"240\" value=\"%u\" style=\"width:5em\"> min</td></tr>", override_min);
  p->printf("<tr><td>Dag/nacht (tot v0.3, lux)</td><td><select name=\"nf\"><option value=\"1\"%s>altijd nacht (PIR's werken)</option>"
            "<option value=\"0\"%s>altijd dag (AUTO doet niets)</option></select></td></tr>",
            force_night ? " selected" : "", force_night ? "" : " selected");
  p->print(F("</table><h3>Deuren (reed-contacten)</h3><table>"));
  for (int d = 0; d < NUM_DOORS; d++)
    p->printf("<tr><td>Ingang %d (IO%d)</td><td>deurcode <input type=\"text\" name=\"dc%d\" maxlength=\"3\" value=\"%s\" "
              "style=\"width:4em\" placeholder=\"vrij\"> &middot; contact gesloten = deur "
              "<label><input type=\"radio\" name=\"dl%d\" value=\"1\"%s> dicht</label> "
              "<label><input type=\"radio\" name=\"dl%d\" value=\"0\"%s> open</label></td></tr>",
              d + 1, DOOR_PIN[d], d + 1, door_code[d],
              d + 1, door_closed_low[d] ? " checked" : "", d + 1, door_closed_low[d] ? "" : " checked");
  p->print(F("<tr><td colspan=\"2\" class=\"dim\">ACCESS1: SD, VD · ACCESS3: AD, BD · ACCESS2: leeg.</td></tr>"));
  p->print(F("</table><button class=\"btn\" type=\"submit\">Opslaan</button>"
             "<span class=\"dim\"> Bij een andere naam, WiFi, IP of frequentie herstart de controller.</span></form>"));

  // DIAGNOSE
  uint32_t up = millis() / 1000;
  p->print(F("<h3>Diagnose</h3><table>"));
  p->printf("<tr><td>Versie</td><td>ACCESS " ACCESS_VERSION "</td></tr>");
  p->printf("<tr><td>Laatste herstart</td><td>%s (opstart #%u)</td></tr>", resetReasonText(boot_reason), (unsigned)boot_count);
  p->printf("<tr><td>Laatste stap ervoor</td><td>%s</td></tr>", prev_step[0] ? prev_step : "<span class=\"dim\">niet gekend (stroom aan of eerste start)</span>");
  p->printf("<tr><td>Vorige herstarts</td><td>%s<br>%s<br>%s</td></tr>", hist[0], hist[1], hist[2]);
  p->printf("<tr><td>Uptime</td><td>%lu d %02lu:%02lu:%02lu</td></tr>",
            (unsigned long)(up / 86400), (unsigned long)(up / 3600 % 24), (unsigned long)(up / 60 % 60), (unsigned long)(up % 60));
  p->printf("<tr><td>Geheugen vrij / laagste / grootste blok</td><td>%u / %u / %u KB</td></tr>",
            (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getMinFreeHeap() / 1024), (unsigned)(ESP.getMaxAllocHeap() / 1024));
  p->printf("<tr><td>WiFi</td><td>%s &middot; %d dBm &middot; MAC %s</td></tr>",
            ap_mode ? "toegangspunt" : (WiFi.status() == WL_CONNECTED ? "verbonden" : "niet verbonden"),
            (int)WiFi.RSSI(), WiFi.macAddress().c_str());
  p->print(F("</table><button class=\"btn red\" onclick=\"if(confirm('Herstarten?'))location.href='/reboot'\">Herstarten</button>"));
  pageFoot(p);
  req->send(p);
}

void handleSave(AsyncWebServerRequest* req) {
  crumbW(CW_SAVE);
  bool restart = false;
  auto getStr = [&](const char* key, char* dst, size_t n, const char* nvs) {
    if (!req->hasParam(key)) return;
    String v = req->getParam(key)->value(); v.trim();
    if (!v.length() || v == dst) return;
    strlcpy(dst, v.c_str(), n); prefs.putString(nvs, dst); restart = true;
  };
  getStr("name", ctrl_name, sizeof(ctrl_name), "name");
  getStr("ssid", wifi_ssid, sizeof(wifi_ssid), "ssid");
  getStr("ip",   static_ip, sizeof(static_ip), "ip");
  if (req->hasParam("pass") && req->getParam("pass")->value().length()) {
    strlcpy(wifi_pass, req->getParam("pass")->value().c_str(), sizeof(wifi_pass));
    prefs.putString("pass", wifi_pass); restart = true;
  }
  if (req->hasParam("hz")) {
    uint16_t hz = constrain(req->getParam("hz")->value().toInt(), 100, 20000);
    if (hz != pwm_freq) { pwm_freq = hz; prefs.putUShort("pwm_hz", hz); restart = true; }
  }
  for (int z = 0; z < NUM_ZONES; z++) {
    char k[8]; snprintf(k, sizeof(k), "pol%d", z + 1);
    if (!req->hasParam(k)) continue;
    bool al = req->getParam(k)->value() == "1";
    if (al != active_low[z]) { active_low[z] = al; prefs.putBool(k, al); setOutput(z, out_pct[z]); }
  }
  for (int z = 0; z < NUM_ZONES; z++) {             // zonecode: hoofdletters/cijfers, max 3 tekens
    char k[8]; snprintf(k, sizeof(k), "zc%d", z + 1);
    if (!req->hasParam(k)) continue;
    String v = req->getParam(k)->value(); v.trim(); v.toUpperCase();
    char c[4] = ""; size_t j = 0;
    for (size_t i = 0; i < v.length() && j < 3; i++) { char ch = v[i]; if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) c[j++] = ch; }
    c[j] = 0;
    if (strcmp(c, zone_code[z])) { strlcpy(zone_code[z], c, sizeof(zone_code[z])); prefs.putString(k, c); }
  }
  for (int z = 0; z < NUM_ZONES; z++) {             // v0.2: zone aan/uit, helderheid, brandtijd
    char k[8];
    snprintf(k, sizeof(k), "ze%d", z + 1);
    if (req->hasParam(k)) { bool v = req->getParam(k)->value() == "1"; if (v != zone_en[z]) { zone_en[z] = v; prefs.putBool(k, v); } }
    snprintf(k, sizeof(k), "zb%d", z + 1);
    if (req->hasParam(k)) { uint8_t v = constrain(req->getParam(k)->value().toInt(), 1, 100); if (v != zone_bright[z]) { zone_bright[z] = v; prefs.putUChar(k, v); } }
    snprintf(k, sizeof(k), "zt%d", z + 1);
    if (req->hasParam(k)) { uint16_t v = constrain(req->getParam(k)->value().toInt(), 5, 3600); if (v != zone_burn_s[z]) { zone_burn_s[z] = v; prefs.putUShort(k, v); } }
  }
  if (req->hasParam("ov")) { uint16_t v = constrain(req->getParam("ov")->value().toInt(), 1, 240); if (v != override_min) { override_min = v; prefs.putUShort("ov_min", v); } }
  if (req->hasParam("nf")) { bool v = req->getParam("nf")->value() == "1"; if (v != force_night) { force_night = v; prefs.putBool("nf", v); } }
  for (int d = 0; d < NUM_DOORS; d++) {             // deurcode + betekenis van het contact
    char k[8]; snprintf(k, sizeof(k), "dc%d", d + 1);
    if (req->hasParam(k)) {
      String v = req->getParam(k)->value(); v.trim(); v.toUpperCase();
      char c[4] = ""; size_t j = 0;
      for (size_t i = 0; i < v.length() && j < 3; i++) { char ch = v[i]; if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) c[j++] = ch; }
      c[j] = 0;
      if (strcmp(c, door_code[d])) { strlcpy(door_code[d], c, sizeof(door_code[d])); prefs.putString(k, c); }
    }
    snprintf(k, sizeof(k), "dl%d", d + 1);
    if (req->hasParam(k)) {
      bool cl = req->getParam(k)->value() == "1";
      if (cl != door_closed_low[d]) { door_closed_low[d] = cl; prefs.putBool(k, cl); }
    }
  }
  req->send(200, "text/html; charset=utf-8", restart
    ? "<meta http-equiv=\"refresh\" content=\"12;url=/settings\"><h3>Opgeslagen. De controller herstart&hellip;</h3>"
    : "<meta http-equiv=\"refresh\" content=\"1;url=/settings\"><h3>Opgeslagen.</h3>");
  if (restart) { restart_requested = true; restart_at = millis() + 1500; }
}

// ── WEB: OTA ────────────────────────────────────────────────
void handleUpdatePage(AsyncWebServerRequest* req) {
  AsyncResponseStream* p = req->beginResponseStream("text/html; charset=utf-8");
  pageHead(p, "OTA", "/update");
  p->print(F("<h3>Nieuwe firmware (.bin)</h3>"
    "<form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\">"
    "<input type=\"file\" name=\"update\" accept=\".bin\"><br><button class=\"btn\" type=\"submit\">Uploaden</button></form>"
    "<p class=\"dim\">Gebruik bin/ESP32_C6_ACCESS_vX.Y.ino.bin uit de repo.</p>"));
  pageFoot(p);
  req->send(p);
}

void setupWeb() {
  server.on("/", HTTP_GET, handleStatus);
  server.on("/json", HTTP_GET, handleJson);
  server.on("/status_json", HTTP_GET, handleStatusJson);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/save_settings", HTTP_GET, handleSave);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest* req) {
    bool ok = !Update.hasError();
    req->send(200, "text/html; charset=utf-8", ok
      ? "<meta http-equiv=\"refresh\" content=\"15;url=/\"><h3>Update gelukt, de controller herstart&hellip;</h3>"
      : "<h3 style=\"color:#c00\">Update mislukt.</h3><a href=\"/update\">Opnieuw</a>");
    if (ok) { restart_requested = true; restart_at = millis() + 1000; }
  }, [](AsyncWebServerRequest* req, String fn, size_t index, uint8_t* data, size_t len, bool final) {
    if (!index) { crumbW(CW_OTA); Serial.printf("[OTA] %s\n", fn.c_str()); Update.begin(UPDATE_SIZE_UNKNOWN); }
    if (Update.write(data, len) != len) Serial.println("[OTA] schrijffout");
    if (final) { if (Update.end(true)) Serial.printf("[OTA] klaar: %u bytes\n", (unsigned)(index + len));
                 else Serial.printf("[OTA] fout: %s\n", Update.errorString()); }
  });
  server.on("/set_test", HTTP_GET, [](AsyncWebServerRequest* req) {   // TEST, geen contract
    crumbW(CW_TEST);
    if (!req->hasParam("z") || !req->hasParam("b")) { req->send(400, "text/plain", "z=1..4&b=0..100"); return; }
    int z = req->getParam("z")->value().toInt() - 1;
    if (z < 0 || z >= NUM_ZONES) { req->send(400, "text/plain", "z=1..4"); return; }
    int b = req->getParam("b")->value().toInt();
    test_pct[z] = b < 0 ? -1 : constrain(b, 0, 100);   // b=-1: test stoppen
    req->send(200, "text/plain", "OK");
  });
  // v0.2: bediening (contract, standaard §3.1): /set_zone?z=<zone>&v=1|0[&b=0..100]
  server.on("/set_zone", HTTP_GET, [](AsyncWebServerRequest* req) {
    crumbW(CW_ZONE);
    if (!req->hasParam("z") || !req->hasParam("v")) { req->send(400, "text/plain", "z=<zone>&v=1|0[&b=0..100]"); return; }
    int z = zoneByCode(req->getParam("z")->value());
    if (z < 0 || !zone_en[z]) { req->send(404, "text/plain", "zone onbekend of uit"); return; }
    for (int i = 0; i < NUM_ZONES; i++) test_pct[i] = -1;   // een echte opdracht stopt elke test
    if (req->getParam("v")->value() == "1") {
      uint8_t b = req->hasParam("b") ? constrain(req->getParam("b")->value().toInt(), 0, 100) : zone_bright[z];
      zone_manual[z] = true; zone_manual_b[z] = b;
      auto_mode = false; override_until = millis() + (uint32_t)override_min * 60000UL;
      Serial.printf("[MODUS] MANUEEL: %s aan op %u %% (%u min)\n", zone_code[z], b, override_min);
    } else {
      zone_manual[z] = false; pir_lit[z] = false;
      backToAuto("zone manueel uit");
    }
    req->send(200, "text/plain", "OK");
  });
  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest* req) {
    crumbW(CW_OTHER);
    req->send(200, "text/html; charset=utf-8", "<meta http-equiv=\"refresh\" content=\"12;url=/\"><h3>Herstarten&hellip;</h3>");
    restart_requested = true; restart_at = millis() + 1000;
  });
  // captive portal (toegangspunt): alles naar /settings
  server.onNotFound([](AsyncWebServerRequest* req) {
    if (ap_mode) req->redirect("http://192.168.4.1/settings");
    else req->send(404, "text/plain", "Niet gevonden");
  });
  server.begin();
}

// ── DIAGNOSE bij opstart ────────────────────────────────────
void bootDiagnose() {
  boot_reason = esp_reset_reason();
  Preferences d;
  d.begin("crash-log", false);
  boot_count = d.getUInt("boots", 0) + 1;
  d.putUInt("boots", boot_count);
  bool crumbs_ok = (crumb_magic == CRUMB_MAGIC) && (boot_reason != ESP_RST_POWERON);
  char entry[160];
  if (crumbs_ok) {
    uint8_t cl = crumb_loop < CL_COUNT ? crumb_loop : CL_NONE;
    uint8_t cw = crumb_web  < CW_COUNT ? crumb_web  : CW_NONE;
    uint32_t ago = (crumb_up_s >= crumb_web_s) ? crumb_up_s - crumb_web_s : 0;
    if (cw == CW_NONE) snprintf(prev_step, sizeof(prev_step), "loop: %s", CRUMB_LOOP_TXT[cl]);
    else snprintf(prev_step, sizeof(prev_step), "loop: %s &middot; web: %s (%lu s ervoor)",
                  CRUMB_LOOP_TXT[cl], CRUMB_WEB_TXT[cw], (unsigned long)ago);
    snprintf(entry, sizeof(entry), "#%u %s &middot; na %.1f u &middot; %s",
             (unsigned)boot_count, resetReasonText(boot_reason), crumb_up_s / 3600.0f, prev_step);
  } else {
    snprintf(entry, sizeof(entry), "#%u %s", (unsigned)boot_count, resetReasonText(boot_reason));
  }
  String h1 = d.getString("h1", ""), h2 = d.getString("h2", "");
  d.putString("h3", h2); d.putString("h2", h1); d.putString("h1", entry);
  strlcpy(hist[0], entry, sizeof(hist[0]));
  strlcpy(hist[1], h1.c_str(), sizeof(hist[1]));
  strlcpy(hist[2], h2.c_str(), sizeof(hist[2]));
  d.end();
  crumb_magic = CRUMB_MAGIC;
  crumb_loop = CL_SETUP; crumb_web = CW_NONE; crumb_web_s = 0; crumb_up_s = 0;
  Serial.printf("\n=== ESP32-C6 ACCESS %s ===\n[BOOT] Opstart #%u — %s\n", ACCESS_VERSION,
                (unsigned)boot_count, resetReasonText(boot_reason));
  if (crumbs_ok) Serial.printf("[BOOT] Laatste stap ervoor: %s\n", prev_step);
}

// ── SETUP / LOOP ────────────────────────────────────────────
void setup() {
  outputsSafeEarly();                   // eerst: lichten uit
  releaseUsbPins();                     // IO12 (PIR zone 4) en IO13 (I2C) vrij
  Serial.begin(115200);
  delay(300);
  bootDiagnose();
  loadSettings();
  for (int z = 0; z < NUM_ZONES; z++) pinMode(PIR_PIN[z], INPUT_PULLDOWN);   // v0.2: geen zwevende ingangen
  for (int d = 0; d < NUM_DOORS; d++) pinMode(DOOR_PIN[d], INPUT_PULLUP);   // + pull-up op het shield
  delay(5);
  for (int d = 0; d < NUM_DOORS; d++) door_open[d] = door_code[d][0] && doorReadOpen(d);   // begintoestand, telt niet als opening
  outputsInit();
  Serial.printf("[CFG] %s · IP %s · PWM %u Hz\n", ctrl_name, static_ip, pwm_freq);
  startWiFi();
  setupWeb();
}

void loop() {
  crumb_up_s = millis() / 1000;
  crumbL(CL_WIFI);  checkWiFi();
  if (ap_mode) dnsServer.processNextRequest();
  crumbL(CL_PIR);   readPIRs();
  crumbL(CL_OUT);   updateZones();
  crumbL(CL_IDLE);
  if (restart_requested && (int32_t)(millis() - restart_at) >= 0) {
    for (int z = 0; z < NUM_ZONES; z++) setOutput(z, 0);
    delay(100);
    ESP.restart();
  }
  delay(5);
}
