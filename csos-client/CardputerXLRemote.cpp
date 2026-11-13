// CS OS remote client, derived from Cardputer XL Remote.
// External ILI9341: scaled Linux desktop. Internal display: local controls.
// Wi-Fi station -> authenticated WSS -> CXL HMAC session; no LAN discovery/AP.
// Existing cloud worker owns connection setup; RX owns the active socket.
// The UI task decodes JPEG/lossless bands and owns display/touch transfers.
// Fn/Aa are toggles; Opt tap cycles Mouse/Tasti/Vista, hold opens local options.
// Negotiated Linux duplex audio; one I2S owner, bounded queues, local controls.
// Build and wiring: docs/SETUP.md, docs/HARDWARE.md, docs/ARCHITETTURA.md.

#include "DiagnosticSerial.h"
#include <Arduino.h>
#include <atomic>
#include "SessionDiagnostic.h"
#include "FpsProbe.h"
#include "FaultDiagnostics.h"
#include <M5Cardputer.h>
#include <SPI.h>
#include <WiFi.h>
#include <Preferences.h>
#include "DeviceSettingsStore.h"
#include <lgfx/v1/panel/Panel_LCD.hpp>
#include "Panel_ILI9341.hpp"
#include "Ili9341Rgbset.h"
#include "ExternalDisplayDevice.h"
#include "Ili9341IndexedRead.h"
#include "CxlWssStream.h"
#ifndef CSOS_CXL_DIAG_TRACE
#define CSOS_CXL_DIAG_TRACE 0
#endif
#if CSOS_CXL_DIAG_TRACE
#include "CxlDiagnosticTrace.h"
#include "CxlDiagnosticAudio.h"
#endif
#include "DuplexAudio.h"
#include "AudioProfile.h"
#include "MediaTelemetry.h"
#include "ReliableKeyboard.h"
#include "CloudControl.h"
#include "CloudConfig_private.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_lcd_panel_io_interface.h"
#include <lgfx/v1/platforms/common.hpp>
#include "calibration.hpp"
#include "TouchCalibrationStore.h"
#ifndef CSOS_ILI9341_ID4_PROBE
#define CSOS_ILI9341_ID4_PROBE 0
#endif
#ifndef CSOS_CALIBRATION_LAB
#define CSOS_CALIBRATION_LAB 0
#endif
#ifndef CSOS_TOUCH_STUDY
#define CSOS_TOUCH_STUDY 0
#endif
#ifndef CSOS_PEN_ONLY
#define CSOS_PEN_ONLY 0
#endif
#if CSOS_TOUCH_STUDY && CSOS_CALIBRATION_LAB
#error Touch study and calibration are separate builds
#endif
#include "DirectTouch.h"
#include "TouchFilter.h"
#include "LocalControls.h"
#include "ControlGesture.h"
#include "TouchTap.h"
#include "CpuWindow.h"
#include "UiPalette.h"
#include <esp_timer.h>
#include <nvs.h>
#include "HudView.h"
#include "LocalPage.h"
#include "WifiHistoryStore.h"
#include <XPT2046_Touchscreen.h>
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <JPEGDEC.h>
#include <miniz.h>
#include <new>
#include <mbedtls/md.h>
#include <vector>

// ---- CS OS wiring v2: docs/CABLAGGIO-V2.md (requires LED/IRQ rewiring) ----
#define TFT_CS 5
#define TFT_RST 3
#define TFT_DC 6
#define TFT_MOSI 14
#define TFT_SCK 40
#define TFT_MISO 39
#define TOUCH_CS 15
#define TFT_BL 13
#define TOUCH_IRQ 4
#define SD_CS 12
#define HAPTIC_PIN 1
#define BTNA_PIN 0

constexpr uint8_t PROTO_VER = 3;
#define CXL_REMOTE_VERSION "1.4.1"
constexpr int OUT_W = 320, OUT_H = 240;
constexpr uint32_t JPEG_MAX = 8 * 1024;      // two slots reserve 16 KiB; server splits JPEG rectangles

constexpr uint16_t C_BG=UiPalette::Background,C_BAR=UiPalette::Ink,C_SEL=UiPalette::Rule,
 C_TXT=UiPalette::Ink,C_DIM=UiPalette::Muted,C_OK=UiPalette::View,C_ERR=UiPalette::Warning;

constexpr uint16_t C_ACC=UiPalette::Keys;

// ---- External ILI9341 through M5GFX's own lgfx classes (no second graphics
// library, so no clash with M5GFX). It gets SPI2 + DMA to itself; the
// built-in screen is on SPI3. Init sequence copied from LovyanGFX's
// Panel_ILI9341 (FreeBSD licence, lovyan03) - M5GFX only ships ILI9342.
struct Panel_ILI9341_XL : public lgfx::Panel_LCD {
  Panel_ILI9341_XL() {
    _cfg.memory_width = _cfg.panel_width = 240;
    _cfg.memory_height = _cfg.panel_height = 320;
  }
  const uint8_t* getInitCommands(uint8_t listno) const override {
    static constexpr uint8_t list0[] = {
      0xEF, 3, 0x03, 0x80, 0x02,
      0xCF, 3, 0x00, 0xC1, 0x30,
      0xED, 4, 0x64, 0x03, 0x12, 0x81,
      0xE8, 3, 0x85, 0x00, 0x78,
      0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
      0xF7, 1, 0x20,
      0xEA, 2, 0x00, 0x00,
      0xC0, 1, 0x23,
      0xC1, 1, 0x10,
      0xC5, 2, 0x3e, 0x28,
      0xC7, 1, 0x86,
      0xB1, 2, 0x00, 0x13,
      0xF2, 1, 0x00,
      CMD_GAMMASET, 1, 0x01,
      0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
      0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
      0xB6, 3, 0x08, 0xC2, 0x27,
      CMD_SLPOUT, 0 + CMD_INIT_DELAY, 120,
      CMD_IDMOFF, 0,
      CMD_DISPON, 0 + CMD_INIT_DELAY, 100,
      0xFF, 0xFF,
    };
    return listno == 0 ? list0 : nullptr;
  }
};

class ExtPanel : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
public:
  ExtPanel() {
    auto b = _bus.config();
    b.spi_host = SPI2_HOST;
    b.spi_mode = 0;
    b.freq_write = 40000000;
    b.freq_read = 16000000;
    b.spi_3wire = false;
    b.use_lock = true;
    // Research builds retain their measured synchronous baseline.
    b.dma_channel = (CSOS_CALIBRATION_LAB || CSOS_TOUCH_STUDY) ? 0 : SPI_DMA_CH_AUTO;
    b.pin_sclk = TFT_SCK;
    b.pin_mosi = TFT_MOSI;
    b.pin_miso = TFT_MISO;
    b.pin_dc = TFT_DC;
    _bus.config(b);
    _panel.setBus(&_bus);
    auto p = _panel.config();
    p.pin_cs = TFT_CS;
    p.pin_rst = TFT_RST;
    p.memory_width=p.panel_width=240;
    p.memory_height=p.panel_height=320;
    p.pin_busy = -1;
    p.readable = false;
    p.invert = false;
    p.rgb_order = false;
    p.dlen_16bit = false;
    p.bus_shared = true;
#if CSOS_ILI9341_ID4_PROBE
    // The measured JTAG RDID4 path returns the first byte without dummy
    // clocks. M5GFX defaults to one dummy bit, so override ONLY in the
    // explicitly selected diagnostic build; ordinary panel reads unchanged.
    p.dummy_read_bits = 0;
#endif
    p.offset_rotation = 2;
    _panel.config(p);
    auto light = _light.config();
    light.pin_bl = TFT_BL;
    light.pwm_channel = 5; // Timer 2; internal M5GFX backlight uses channel 7.
    light.freq = 12000;
    light.invert = false;
    _light.config(light);
    _panel.setLight(&_light);
    setPanel(&_panel);
  }
  Ili9341Id4 readIndexedId4() {
    return readIli9341IndexedId4(_panel);
  }
  // Controller sleep command only; independent of backlight and power rails.
  void setControllerSleep(bool sleep) { _panel.setSleep(sleep); }
};

ExtPanel tft;
static void finishTftTransfer();
static void setExternalBrightness(uint8_t brightness);

M5Canvas hud(&M5Cardputer.Display);
Preferences prefs;
#if CSOS_CXL_DIAG_TRACE
// One recorder only: the CloudControl probing CxlWssStream stays uninstrumented.
static CxlDiag::AckTxRecorder diagAckTrace;
#endif
CxlWssStream conn(true); // TLS MFL candidate; status probes keep the default client.
// Only the display task decodes, and each rectangle completes before the next.
static union {
  JPEGDEC jpeg;
  tinfl_decompressor inflate;
} videoDecoder;
static_assert(sizeof(videoDecoder) == sizeof(JPEGDEC), "Video decoder RAM increased");

// ---- Persistent settings ----
String wifiSsid, wifiPass, serverPin;
String bridgeUser=CSOS_BRIDGE_USER, bridgePassword=CSOS_BRIDGE_PASSWORD;
bool profileOverride=false, configureRequested=false;
uint8_t rotation = 3, brightness = 10, quality = 1, volume = 128;
uint8_t externalBrightness=10;
bool externalDark=false; // Session-only; never restore a black pre-desktop.
constexpr bool CSOS_AUDIO_AVAILABLE=true;
bool soundOn = false;
const char* qualityNames[] = {"FAST", "NORMAL", "SHARP"};
// Frame-rate cap sent to the server ('L'): 0 = none, 255 = the server's setting.
const uint8_t fpsCaps[] = {0, 30, 20, 15, 255};
const char* fpsCapNames[] = {"MAX", "30", "20", "15", "SRV"};
uint8_t fpsCapIdx = 4;
// Server temperatures ('M', every 2 s), written by rxTask, drawn by drawHud().
constexpr int MAX_TEMPS = 12;
char tempNames[MAX_TEMPS][13];
int16_t tempX10[MAX_TEMPS];
volatile uint8_t tempCount = 0;
volatile uint32_t tempsAt = 0;

// ---- Session / HUD state ----
String serverName, statusLine = "Starting", statusLine2;
uint16_t serverScreenW = 0, serverScreenH = 0;
bool inSession = false, hudDirty = true;
bool hudPriorityPending=false;
LocalControls csosControls;
bool connectionPaused=false, csosCxlRejected=false, csosSaveFailed=false;
bool csosPreDesktopActive=true;

bool csosFrameSeen=false;
TransportFailure lastTransportFailure=TransportFailure::None;
uint16_t viewRatioX100 = 0;
uint8_t zoomIdx = 0, zoomCount = 1, monitorNo = 1;
bool stretchOn = false, audioStreaming = false, cursorShown = true;
uint32_t statFrames = 0, statBytes = 0, statT0 = 0;
#if CSOS_FPS_PROBE
FpsProbe fpsProbe;
void serviceFpsProbe(const char* event="sample") {
 const uint32_t now=millis();
 if(!strcmp(event,"sample")&&!fpsProbe.due(now))return;
 char line[224];const int size=fpsProbe.format(line,sizeof(line),event,now);
 if(size>0&&size<int(sizeof(line))&&CsosSerial&&CsosSerial.availableForWrite()>=size){
  CsosSerial.write(reinterpret_cast<const uint8_t*>(line),size);fpsProbe.delivered(now);
 }
}
#endif
float fps = 0, kbps = 0;
#include "DeviceStatus.h"
DeviceStatusData deviceStatus;
CpuWindow cpuWindow;
uint32_t deviceStatusAt=0;
String lastKeyLabel;
uint32_t hapticOffAt = 0, hudLastDraw = 0;

// ============================================================================
// Small hardware helpers
// ============================================================================
void haptic(uint16_t ms = 20) { digitalWrite(HAPTIC_PIN, HIGH); hapticOffAt = millis() + ms; }
void serviceHaptic() { if (hapticOffAt && (int32_t)(millis() - hapticOffAt) >= 0) { digitalWrite(HAPTIC_PIN, LOW); hapticOffAt = 0; } }
void applyBrightness() {
  M5Cardputer.Display.setBrightness(40 + brightness * 10);
  setExternalBrightness(externalDark?0:uint8_t((externalBrightness*255+5)/10));
}

bool saveSettings() {
  if(!prefs.begin("cxlremote",false)){
    recordFault(FaultArea::Nvs,FaultCode::NvsOpen,0,diagnosticTime());
    CSOS_LOGLN("[CSOS] settings_open_failed");return false;
  }
  bool saved=true;
  auto check=[&](const char* key,size_t actual,size_t expected){
    if(actual!=expected){
      if(saved)recordFault(FaultArea::Nvs,FaultCode::NvsWrite,0,diagnosticTime());
      saved=false;
      CSOS_LOGF("[CSOS] settings_key_failed key=%s type=%u free_entries=%u\n",key,unsigned(prefs.getType(key)),unsigned(prefs.freeEntries()));
    }
  };
  if(prefs.getString("ssid")!=wifiSsid)check("ssid",prefs.putString("ssid",wifiSsid),wifiSsid.length());
  if(prefs.getString("pass")!=wifiPass)check("pass",prefs.putString("pass",wifiPass),wifiPass.length());
  if(prefs.getString("pin")!=serverPin)check("pin",prefs.putString("pin",serverPin),serverPin.length());
  if(prefs.getBool("cloud_override")!=profileOverride)check("cloud_override",prefs.putBool("cloud_override",profileOverride),1);
  if(profileOverride) {
    if(prefs.getString("bridge_user")!=bridgeUser)check("bridge_user",prefs.putString("bridge_user",bridgeUser),bridgeUser.length());
    if(prefs.getString("bridge_pass")!=bridgePassword)check("bridge_pass",prefs.putString("bridge_pass",bridgePassword),bridgePassword.length());
  }
  prefs.end();
  if(!saved)CSOS_LOGLN("[CSOS] settings_save_failed");
  return saved;
}


void loadSettings() {
  uint8_t savedBright = 0;
  if (prefs.begin("cxlremote", true)) {
    wifiSsid = prefs.getString("ssid", "");
    wifiPass = prefs.getString("pass", "");
    serverPin = prefs.getString("pin", "");
    profileOverride=prefs.getBool("cloud_override",false);
    if(profileOverride) {
      bridgeUser=prefs.getString("bridge_user",CSOS_BRIDGE_USER);
      bridgePassword=prefs.getString("bridge_pass",CSOS_BRIDGE_PASSWORD);
    }
    savedBright = prefs.getUChar("bright", 0);
    externalBrightness=constrain(prefs.getUChar("extbright",10),1,10);
    quality = min<uint8_t>(prefs.getUChar("quality", 1), 2);
    fpsCapIdx = min<uint8_t>(prefs.getUChar("fpscap", 0), 4);
    volume = prefs.getUChar("volume", volume);
    soundOn = prefs.getBool("sound", soundOn);
    CSOS_LOGF("[CSOS] settings_loaded internal=%u external=%u free_entries=%u type_bright=%u ext=%u\n",
      unsigned(savedBright),unsigned(externalBrightness),unsigned(prefs.freeEntries()),
      unsigned(prefs.getType("bright")),unsigned(prefs.getType("extbright")));
    prefs.end();
  }
  // CardputerXL's own settings: Wi-Fi it already knows, panel rotation and
  // brightness the user picked there.
  if (prefs.begin("cyberdeck", true)) {
    if (wifiSsid.isEmpty()) {
      wifiSsid = prefs.getString("wifi_ssid", "");
      wifiPass = prefs.getString("wifi_pass", "");
    }
    rotation = prefs.getUChar("rotation", 3);
    if (!savedBright) savedBright = prefs.getUChar("brightness", 10);
    prefs.end();
  }
  if (rotation != 1 && rotation != 3) rotation = 3;
  brightness = constrain(savedBright ? savedBright : 10, 1, 10);
  if(!profileOverride) {
    wifiSsid=CSOS_WIFI_SSID; wifiPass=CSOS_WIFI_PASSWORD;
    serverPin=CSOS_CXL_PIN;
  }
  fpsCapIdx=4; // Legacy MAX is not imported; new explicit preferences are respected.
  DeviceSettings device;
  const auto error=loadDeviceSettings(device);
  if(error==ESP_OK){brightness=device.brightness;externalBrightness=device.externalBrightness;quality=device.quality;fpsCapIdx=device.fpsCap;}
  CSOS_LOGF("[CSOS] device_settings_load error=%s internal=%u external=%u\n",esp_err_to_name(error),unsigned(brightness),unsigned(externalBrightness));
  // Migrate once from legacy/defaults. Existing namespaces are left intact.
  if(error==ESP_ERR_NVS_NOT_FOUND){
    device.brightness=brightness;device.externalBrightness=externalBrightness;device.quality=quality;device.fpsCap=fpsCapIdx;
    const auto saved=saveDeviceSettings(device);csosSaveFailed=saved!=ESP_OK;
    CSOS_LOGF("[CSOS] device_settings_migrate error=%s\n",esp_err_to_name(saved));
  }else if(error!=ESP_OK)csosSaveFailed=true;
  soundOn=false;
}

// ============================================================================
// Keyboard: raw physical key matrix as a 56-bit mask (bit = row * 14 + col)
// ============================================================================
constexpr int KEY_COLS = 14, KEY_COUNT = 56;
constexpr int IDX_FN = 2 * 14 + 0, IDX_SHIFT = 2 * 14 + 1, IDX_CTRL = 3 * 14 + 0, IDX_OPT = 3 * 14 + 1, IDX_ALT = 3 * 14 + 2;
inline uint64_t kbit(int i) { return 1ULL << i; }
inline uint8_t baseAt(int i) { return (uint8_t)_key_value_map[i / KEY_COLS][i % KEY_COLS].value_first; }
inline uint8_t shiftedAt(int i) { return (uint8_t)_key_value_map[i / KEY_COLS][i % KEY_COLS].value_second; }
inline bool isModifierIdx(int i) { return i == IDX_FN || i == IDX_SHIFT || i == IDX_CTRL || i == IDX_OPT || i == IDX_ALT; }

uint64_t readKeyMask() {
  M5Cardputer.update();
  // ADV consumes one FIFO event per update. Observe this state before
  // consuming the next event: draining first would erase a buffered tap.
  uint64_t m = 0;
  for (const auto& p : M5Cardputer.Keyboard.keyList()) m |= kbit(p.y * KEY_COLS + p.x);
  return m;
}

bool btnAWasDown = false;
static uint32_t csosKeyboardChanges=0;
bool btnAPressed() {
  bool down = digitalRead(BTNA_PIN) == LOW;
  bool edge = down && !btnAWasDown;
  btnAWasDown = down;
  return edge;
}

// One key-press edge for menus / text entry.
struct UiKey { bool opt=false; int idx = -1; uint8_t base = 0, shifted = 0; bool fn = false, shift = false; };
uint64_t uiPrevMask = 0;

UiKey uiPoll() {
  uint64_t cur = readKeyMask(), down = cur & ~uiPrevMask;
  uiPrevMask = cur;
  UiKey k;
  if(down & kbit(IDX_OPT))k.opt=true;
  if(down & kbit(IDX_SHIFT)){csosControls.aa=!csosControls.aa;hudDirty=true;}
  if(down & kbit(IDX_FN)) { csosControls.press(true,false,0); if(csosControls.fn)haptic(20); hudDirty=true; }
  k.fn = csosControls.fn;
  k.shift = csosControls.aa;
  for (int i = 0; i < KEY_COUNT; ++i) {
    if (!(down & kbit(i)) || isModifierIdx(i)) continue;
    k.idx = i; k.base = baseAt(i); k.shifted = shiftedAt(i);
    break;
  }
  return k;
}

// ============================================================================
// Built-in screen HUD
// ============================================================================
void setStatus(const String& a, const String& b = "") {
  statusLine = a; statusLine2 = b; hudDirty = true;
  CSOS_LOGF("[CSOS] state=%s detail=%s\n",a.c_str(),b.c_str());
}

bool persistDeviceSettings(){
 DeviceSettings value;value.brightness=brightness;value.externalBrightness=externalBrightness;
 value.quality=quality;value.fpsCap=fpsCapIdx;
 const auto error=saveDeviceSettings(value);
 CSOS_LOGF("[CSOS] device_settings_save error=%s internal=%u external=%u\n",esp_err_to_name(error),unsigned(brightness),unsigned(externalBrightness));
 csosSaveFailed=error!=ESP_OK;
 if(csosSaveFailed)setStatus("Impostazioni non salvate","Applicate fino al riavvio");
 return !csosSaveFailed;
}

int uiDepth=0;bool uiSessionLost=false;
LocalPage uiPage;LocalPageCache uiPageCache;
void uiDrawCurrent();bool serviceUiBackground();void servicePreDesktopBackground(bool schedule=true);
extern uint8_t csosPendingMods,csosHeldMods,csosDragMods;
extern bool csosDrag,csosPanSupported;
extern String connError;
#include "HudRenderer.h"

// Association recovery is owned by WiFi auto-reconnect and pre-desktop profiles.
bool staWasUp=false;
void serviceWifi(){
 const bool up=WiFi.status()==WL_CONNECTED;
 if(up!=staWasUp)hudDirty=true;
 staWasUp=up;
}

// Temporary bounded stall trace. UI task only; output is always best-effort.
void traceUiBoundary(const char* step) {
#if !CSOS_DIAGNOSTIC_TELEMETRY || CSOS_FPS_PROBE
 (void)step; // Detailed stall tracing is opt-in, never part of normal UI work.
#else
 static uint32_t windowUntil=0,lastCycle=0;
 static bool tracing=false;
 const uint32_t now=millis();
 if(!strcmp(step,"input_changed")){windowUntil=now+5000;tracing=true;}
 if(!strcmp(step,"cycle")){
  if(int32_t(now-windowUntil)>=0){tracing=false;return;}
  if(now-lastCycle<500){tracing=false;return;}
  tracing=true;lastCycle=now;
 }
 if(!tracing)return;
 char line[112];
 const int n=snprintf(line,sizeof(line),"[CSOS] ui_step=%s menu=%u group=%u item=%u ms=%lu\n",
  step,unsigned(csosControls.menu),unsigned(csosControls.group),unsigned(csosControls.systemIndex),(unsigned long)now);
 if(n>0&&n<int(sizeof(line))&&CsosSerial&&CsosSerial.availableForWrite()>=n)
  CsosSerial.write(reinterpret_cast<const uint8_t*>(line),n);
#endif
}

void serviceDeviceStatus(){
 const uint32_t now=millis();if(deviceStatusAt&&uint32_t(now-deviceStatusAt)<1000)return;
 deviceStatusAt=now;
 TaskStatus_t first{},second{};
 vTaskGetInfo(xTaskGetIdleTaskHandleForCore(0),&first,pdFALSE,eInvalid);
 vTaskGetInfo(xTaskGetIdleTaskHandleForCore(1),&second,pdFALSE,eInvalid);
 cpuWindow.sample(uint32_t(esp_timer_get_time()),uint32_t(first.ulRunTimeCounter),uint32_t(second.ulRunTimeCounter));
 deviceStatus.cpu=cpuWindow.valid?cpuWindow.percent:-1;
 const int battery=M5.Power.getBatteryLevel();deviceStatus.battery=battery>=0&&battery<=100?battery:-1;
 deviceStatus.wifi=WiFi.status()==WL_CONNECTED;
 deviceStatus.rssi=deviceStatus.wifi?WiFi.RSSI():-127;
 deviceStatus.freeHeap=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
 if(csosControls.details&&(csosControls.page==5||csosControls.page==6)){
  const uint32_t memory=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
  deviceStatus.heapMinimum=heap_caps_get_minimum_free_size(memory);
  deviceStatus.heapLargest=heap_caps_get_largest_free_block(memory);
  deviceStatus.uptimeSeconds=uint32_t(esp_timer_get_time()/1000000);
  deviceStatus.resetReason=int(esp_reset_reason());
  deviceStatus.wifiChannel=deviceStatus.wifi?WiFi.channel():0;
  deviceStatus.wifiAddress=deviceStatus.wifi?uint32_t(WiFi.localIP()):0;
  static uint32_t chipSampleAt=0;
  if(!chipSampleAt||uint32_t(now-chipSampleAt)>=5000){
   chipSampleAt=now;
   const float temperature=temperatureRead();
   deviceStatus.chipTemperatureValid=isfinite(temperature);
   if(deviceStatus.chipTemperatureValid)deviceStatus.chipTemperature=int(roundf(temperature));
  }
 }
#if CSOS_DIAGNOSTIC_TELEMETRY
 static uint32_t logged=0;if(uint32_t(now-logged)>=10000){logged=now;CSOS_LOGF("[CSOS] status cpu=%d battery=%d rssi=%d heap=%lu mode=%u uptime_ms=%lu\n",deviceStatus.cpu,deviceStatus.battery,deviceStatus.rssi,(unsigned long)deviceStatus.freeHeap,unsigned(csosControls.mode),(unsigned long)now);}
#endif
 hudDirty=true;
}
void serviceHud(bool force = false) {
  traceUiBoundary("status_begin");
  serviceDeviceStatus();
  traceUiBoundary("status_end");
  serviceWifi();
  if(uiDepth){uiDrawCurrent();return;}
  uint32_t now = millis();
  if (force || hudPriorityPending || (hudDirty && now - hudLastDraw > 120) ||
      (hudAnimating && now - hudLastDraw > 80) || now - hudLastDraw > 1000) {
    traceUiBoundary("hud_begin");
    drawHud();
    traceUiBoundary("hud_end");
    hudDirty = false;
    hudPriorityPending=false;
    hudLastDraw = millis();
  }
}

// ============================================================================
// External-panel menu UI (setup screens)
// ============================================================================
void invalidatePreDesktopView();
struct LocalPageScope {
 LocalPageScope(){++uiDepth;uiPageCache.invalidate();invalidatePreDesktopView();}
 ~LocalPageScope(){--uiDepth;uiPageCache.invalidate();invalidatePreDesktopView();invalidateOperationalHud();hudDirty=true;hudPriorityPending=true;}
};
void uiDrawCurrent(){
 if(inSession){const bool changed=drawLocalPage(hud,uiPage,uiPageCache,millis(),240,135);if(changed||hudDirty||hudPriorityPending){invalidateOperationalHud();hudStatus(uiPage.editor);hud.pushSprite(0,0);hudDirty=false;hudPriorityPending=false;}}
 else drawLocalPage(tft,uiPage,uiPageCache,millis(),320,240);
}
void uiHeader(const char* title){uiPage={};uiPage.title=title;uiPageCache.invalidate();}
void uiFooter(const char*){}
void uiText(int,int,const String& value,uint16_t,uint8_t=2){uiPage.lines.push_back(value);uiDrawCurrent();}
std::vector<String> uiDocumentLines(){
 if(inSession||!uiPage.summary.length())return localWrap(hud,uiPage.label,inSession?194:292,inSession?.80f:.9f);
 auto lines=localWrap(hud,uiPage.summary,292,.9f);
 if(uiPage.detail.length()){const auto detail=localWrap(hud,uiPage.detail,292,.9f);lines.insert(lines.end(),detail.begin(),detail.end());}
 return lines;
}
void uiMessage(const char* title,const String& first,const String& second="",uint16_t color=C_TXT,const char* =nullptr){
 LocalPageScope scope;uiHeader(title);uiPage.accent=color==C_TXT?UiPalette::Blue:color;uiPage.summary=first;uiPage.detail=second;
 const String text=first+(second.length()?String(". ")+second:String(""));
 uiPage.label=text;const auto lines=uiDocumentLines();
 const int rows=inSession?3:4;uiPage.total=std::max(1,int((lines.size()+rows-1)/rows));
 uiPage.lines.assign(lines.begin(),lines.begin()+std::min(rows,int(lines.size())));
 uiPage.primaryRows=localWrap(hud,first,inSession?194:292,inSession?.80f:.9f).size();
 uiPage.label=text;setStatus(title,first);uiDrawCurrent();
}
void uiDocumentPage(){
 const auto lines=uiDocumentLines();const int rows=inSession?3:4;
 uiPage.total=std::max(1,int((lines.size()+rows-1)/rows));uiPage.position%=uiPage.total;
 const int start=uiPage.position*rows;
 uiPage.primaryRows=uiPage.summary.length()?std::max(0,int(localWrap(hud,uiPage.summary,inSession?194:292,inSession?.80f:.9f).size())-start):0;
 uiPage.lines.assign(lines.begin()+start,lines.begin()+std::min(start+rows,int(lines.size())));
 uiDrawCurrent();
}
void waitAnyKey(uint32_t timeoutMs){
 LocalPageScope scope;uint32_t started=millis();
 while(uiPage.total>1||millis()-started<timeoutMs){
  if(!serviceUiBackground())return;const auto key=uiPoll();
  if(key.opt||key.base==KEY_ENTER||key.base=='`')return;
  if(key.base==';'||key.base=='.'||key.base==KEY_TAB){uiPage.position=(uiPage.position+1)%uiPage.total;uiDocumentPage();started=millis();}
  else if(key.idx>=0)return;delay(8);
 }
}
bool localPanelTap(int& x,int& y);
void uiDrawList(const std::vector<String>& items,int selected,int& top){
 const int visible=4;top=std::max(0,std::min(selected-1,int(items.size())-visible));
 uiPage.lines=items;uiPage.selected=selected;uiDrawCurrent();
}
int listMenu(const char* title,const std::vector<String>& items,const char*,const char* hotkeys,int selected=0){
 LocalPageScope scope;uiHeader(title);uiPage.help=!strcmp(title,"RETI WI-FI")?String("Invio: scegli; R: nuova ricerca"):!strcmp(title,"DIMENTICA RETE")?String("Invio: scegli la rete da eliminare"):String("Invio: apri; Esc: indietro");int top=0;
 while(true){
  uiDrawList(items,selected,top);if(!serviceUiBackground())return -1;
  const auto key=uiPoll();int x,y;
  if(!inSession&&localPanelTap(x,y)&&y>=43&&y<195){const int chosen=top+(y-43)/38;if(chosen<int(items.size()))return chosen;}
  if(key.opt||key.base=='`')return -1;
  if(key.base==KEY_ENTER&&!items.empty())return selected;
  if(key.base==';'&&selected>0)--selected;
  if((key.base=='.'||key.base==KEY_TAB)&&selected+1<int(items.size()))++selected;
  if(hotkeys&&key.base&&strchr(hotkeys,key.base))return -(100+key.base);
  delay(5);
 }
}
bool textInput(const char* title,const String& label,String& value,bool secret,size_t maxLen){
 LocalPageScope scope;uiHeader(title);uiPage.editor=true;uiPage.accent=UiPalette::Blue;uiPage.secret=secret;uiPage.label=label;uiPage.help="Invio continua   Opt annulla";
 String draft=value;bool show=!secret;
 while(true){
  uiPage.value="";if(show)uiPage.value=draft;else for(unsigned i=0;i<draft.length();i++)uiPage.value+='*';
  uiDrawCurrent();if(!serviceUiBackground())return false;const auto key=uiPoll();
  if(key.opt)return false;
  if(key.idx<0){delay(5);continue;}
  if(key.base==KEY_ENTER){value=draft;return true;}
  if(key.base=='`'&&key.fn)return false;
  if(key.base==KEY_BACKSPACE){if(draft.length())draft.remove(draft.length()-1);continue;}
  if(key.base==KEY_TAB){if(secret)show=!show;continue;}
  const char c=char(key.shift?key.shifted:key.base);
  if(c>=32&&c<127&&draft.length()<maxLen)draft+=c;
 }
}

// ============================================================================
// Wi-Fi
// ============================================================================
String pendingWifiSsid,pendingWifiPass;
std::atomic<int> pendingWifiDisconnect{-1};
void serviceWifiFaults();
void serviceFaultTraces();
int scanWifiNetworks(){
  LocalPageScope scope;
  WiFi.scanDelete();WiFi.scanNetworks(true);
  const uint32_t started=millis();
  int n;
  while((n=WiFi.scanComplete())==WIFI_SCAN_RUNNING){
    if(!serviceUiBackground())return -1;const auto key=uiPoll();
    if(key.opt||key.base=='`'||millis()-started>12000){WiFi.scanDelete();return -1;}
    delay(10);
  }
  return n;
}
bool confirmAccess(const char*,const String&,const String&);
void pauseAutoWifi();
void resumeAutoWifi();
struct WifiSetupScope {
 WifiSetupScope(){pauseAutoWifi();}
 ~WifiSetupScope(){resumeAutoWifi();}
};
void wifiSetup() {
  WifiSetupScope wifiScope;LocalPageScope scope;
  while (true) {
    uiMessage("RETI WI-FI", "Ricerca reti", "La connessione attuale resta attiva");
    if(!inSession)WiFi.mode(WIFI_STA);
    int n = scanWifiNetworks();
    if(n<0)return;
    std::vector<String> names, labels;
    std::vector<bool> open;
    for (int i = 0; i < n; ++i) {
      String s = WiFi.SSID(i);
      if (s.isEmpty()) continue;
      bool dup = false;
      for (auto& e : names) if (e == s) dup = true;
      if (dup) continue;
      names.push_back(s);
      open.push_back(WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
      labels.push_back(s+"  "+String(WiFi.RSSI(i))+" dBm");
    }
    WiFi.scanDelete();
    for(int i=0;i<knownWifi.count;i++){
      bool present=false;for(const auto& name:names)if(name==knownWifi.items[i].ssid)present=true;
      if(!present){names.push_back(knownWifi.items[i].ssid);open.push_back(!knownWifi.items[i].password[0]);labels.push_back(String("Salvata: ")+knownWifi.items[i].ssid);}
    }
    const int manualIndex=labels.size();labels.push_back("Nome rete manuale");
    const int forgetIndex=labels.size();labels.push_back("Dimentica una rete");
    setStatus("Reti Wi-Fi",String(names.size())+" reti disponibili");
    int sel = listMenu("RETI WI-FI",labels,"Scegli una rete","rR");
    if (sel == -1) return;
    if (sel <= -100) continue;
    if(sel==forgetIndex){
      std::vector<String> saved;for(int i=0;i<knownWifi.count;i++)saved.push_back(knownWifi.items[i].ssid);
      if(saved.empty())continue;
      const int remove=listMenu("DIMENTICA RETE",saved,"Invio elimina | Opt annulla",nullptr);
      if(remove>=0&&confirmAccess("DIMENTICARE?",saved[remove],"Il profilo viene eliminato")){
        const bool current=saved[remove]==wifiSsid;
        if(!forgetKnownWifi(remove)){uiMessage("WI-FI","Salvataggio fallito");waitAnyKey(2000);}
        else if(current){wifiSsid="";wifiPass="";WiFi.setAutoReconnect(false);if(!inSession)WiFi.disconnect();}
      }
      continue;
    }
    String name=sel==manualIndex?String(""):names[sel];
    if(sel==manualIndex){
      if(!textInput("RETE WI-FI","Nome rete",name,false,32))continue;
      if(name.isEmpty()){
        uiMessage("RETE WI-FI","Inserisci il nome della rete");waitAnyKey(1800);continue;
      }
    }
    const int known=knownWifi.find(name.c_str());
    String pass=known>=0?String(knownWifi.items[known].password):(name==wifiSsid?wifiPass:String(""));
    const bool isOpen=sel!=manualIndex&&open[sel];
    if(!isOpen&&!textInput("PASSWORD WI-FI",name,pass,true,64))continue;
    if(isOpen)pass="";
    if(known<0&&knownWifi.count==WifiProfiles::MAX){uiMessage("RETI PIENE","Dimentica prima una rete");waitAnyKey(2000);continue;}
    if(inSession&&!confirmAccess("CAMBIARE RETE?",name,"Applicata quando esci da Linux"))return;
    pendingWifiSsid=name;pendingWifiPass=pass;
    if(inSession){uiMessage("CAMBIO IN ATTESA",name,"La sessione attuale continua");waitAnyKey(1800);}
    return;
  }
}

// ============================================================================
// Session connection + handshake
// ============================================================================
String connError;
DiagnosticRecord lastSessionFault;
bool sessionFaultNotice=false;
ConnectionAttempt connectionAttempt;

bool readExact(uint8_t* b, size_t n, uint32_t timeoutMs) {
  size_t got = 0;
  uint32_t t0 = millis();
  while (got < n) {
    if(connectionAttempt.cancelled())return false;
    int avail = conn.available();
    if (avail > 0) {
      int r = conn.read(b + got, min<size_t>(avail, n - got));
      if (r > 0) got += r;
      continue;
    }
    if (!conn.connected()) return false;
    if (millis() - t0 > timeoutMs) return false;
    delay(1);
  }
  return true;
}

ConnResult remoteConnect() {
  csosCxlRejected=false;
  connError = "";
  conn.claimOwner();
  conn.stop();
  conn.setCredentials(bridgeUser,bridgePassword);
#if CSOS_CXL_DIAG_TRACE
  conn.setDiagnosticTx(&diagAckTrace);
#endif
  if (!conn.connect()) {
    lastTransportFailure=conn.failure();
    connError=transportFailureText(lastTransportFailure); return CONN_FAIL;
  }
  lastTransportFailure=TransportFailure::None;
  if(!connectionAttempt.update(ConnectionPhase::Cxl)){conn.stop();return CONN_CANCELLED;}
  conn.setNoDelay(true);
  uint8_t hello[21];
  if (!readExact(hello, sizeof(hello), 3000) || memcmp(hello, "CXLS", 4) != 0) { connError = "Not a CXL Remote server"; conn.stop(); return CONN_FAIL; }
  if (hello[4] != PROTO_VER) { connError = "Server protocol v" + String(hello[4]) + ", need v" + String(PROTO_VER); conn.stop(); return CONN_FAIL; }

  uint8_t mac[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const uint8_t*)serverPin.c_str(), serverPin.length(), hello + 5, 16, mac);
  uint8_t reply[45] = {'C', 'X', 'L', 'R', PROTO_VER, OUT_W & 0xFF, OUT_W >> 8, OUT_H & 0xFF, OUT_H >> 8,
                       (uint8_t)(JPEG_MAX), (uint8_t)(JPEG_MAX >> 8), (uint8_t)(JPEG_MAX >> 16), (uint8_t)(JPEG_MAX >> 24)};
  memcpy(reply + 13, mac, 32);
  conn.write(reply, sizeof(reply));

  uint8_t verdict;
  if (!readExact(&verdict, 1, 4000)) { connError = "Handshake timed out"; conn.stop(); return CONN_FAIL; }
  if (verdict == 'X') {
    uint8_t reason = 0; readExact(&reason, 1, 500);
    conn.stop();
    if (reason == 1) { csosCxlRejected=true; connError = "PIN CXL non accettato"; return CONN_BADPIN; }
    connError = "Server refused (" + String(reason) + ")";
    return CONN_FAIL;
  }
  uint8_t info[5];
  if (verdict != 'O' || !readExact(info, 5, 2000)) { connError = "Bad handshake"; conn.stop(); return CONN_FAIL; }
  serverScreenW = info[0] | (info[1] << 8);
  serverScreenH = info[2] | (info[3] << 8);
  char name[64] = {0};
  uint8_t nl = min<uint8_t>(info[4], 63);
  if (nl && !readExact((uint8_t*)name, nl, 2000)) { connError = "Bad handshake"; conn.stop(); return CONN_FAIL; }
  if (info[4] > nl) { uint8_t skip[256]; readExact(skip, info[4] - nl, 500); }
  serverName = name;
  return CONN_OK;
}

// ============================================================================
// Session: outgoing input messages  [type][len][payload]
// ============================================================================
#if CSOS_CXL_DIAG_TRACE
bool sendMsg(uint8_t type, const uint8_t* payload, uint8_t len) {
  uint8_t buf[2 + 32];
  if(len>sizeof(buf)-2){
    recordFault(FaultArea::Input,FaultCode::InputSize,len,diagnosticTime());return false;
  }
  buf[0] = type; buf[1] = len;
  if (len) memcpy(buf + 2, payload, len);
  if(conn.write(buf, 2 + len)!=size_t(2 + len)){
    recordFault(FaultArea::Input,FaultCode::InputQueue,type,diagnosticTime());
    return false;
  }
  return true;
}
#else
// Preserve the exact uninstrumented function (including its return type).
void sendMsg(uint8_t type, const uint8_t* payload, uint8_t len) {
  uint8_t buf[2 + 32];
  if(len>sizeof(buf)-2){
    recordFault(FaultArea::Input,FaultCode::InputSize,len,diagnosticTime());return;
  }
  buf[0] = type; buf[1] = len;
  if (len) memcpy(buf + 2, payload, len);
  if(conn.write(buf, 2 + len)!=size_t(2 + len))
    recordFault(FaultArea::Input,FaultCode::InputQueue,type,diagnosticTime());
}
#endif
void sendKey(const char* name, bool down) {
  uint8_t p[24];
  size_t n = min<size_t>(strlen(name), 22);
  p[0] = down;
  memcpy(p + 1, name, n);
  sendMsg('K', p, 1 + n);
  if (down) { lastKeyLabel = name; hudDirty = true; }
}
void sendButton(uint8_t b, bool down) { uint8_t p[2] = {b, (uint8_t)down}; sendMsg('B', p, 2); }
void sendWheel(int8_t v, int8_t h) { uint8_t p[2] = {(uint8_t)v, (uint8_t)h}; sendMsg('W', p, 2); }
bool sendMove(int16_t dx8, int16_t dy8) { return conn.queueMotion(dx8,dy8); }
void sendZoom(int8_t z) { uint8_t p[1] = {(uint8_t)z}; sendMsg('Z', p, 1); }
void sendQuality() { uint8_t p[1] = {quality}; sendMsg('Q', p, 1); }
void sendFpsCap() {
 uint8_t p[1] = {fpsCaps[fpsCapIdx]}; sendMsg('L', p, 1);
#if CSOS_FPS_PROBE
 fpsProbe.select(millis(),p[0]);serviceFpsProbe("select");
#endif
}

static XPT2046_Touchscreen csosTouch(TOUCH_CS,TOUCH_IRQ);
static ExternalDisplayDevice<ExtPanel,XPT2046_Touchscreen> externalDisplay(tft,csosTouch,OUT_W,OUT_H);
#if defined(CSOS_DISPLAY_LIFECYCLE_COMPILE_PROBE)
// Compile-only backend contract check: instantiates suspend/resume with the
// actual ExtPanel and Arduino delay(), but is never called or flashed as a test.
[[maybe_unused]] static void compileCheckExternalDisplayLifecycle() {
  auto settle = [](unsigned milliseconds) { delay(milliseconds); };
  (void)externalDisplay.suspend(settle);
  (void)externalDisplay.resume(settle);
}
#endif
static void finishTftTransfer() {
  const uint32_t started=micros();
  externalDisplay.finishTransfer([&] {
    MediaTelemetry::dmaWaitUs+=micros()-started;
  });
}
static void setExternalBrightness(uint8_t brightness) {
  externalDisplay.setBrightness(brightness);
}
static Calibration csosCalibration;
static DirectTouch csosDirectTouch;
static TouchFilter csosTouchFilter;
static bool csosTouchSupported=false;
static uint32_t csosDisplayedFrame=0;
static uint32_t csosTouchSamples=0,csosTouchErrors=0,csosTouchEvents=0;
static bool initCsosTouch() {
  // SPI is already initialized by M5GFX on14/40/39. begin() reuses that
  // initialized SPIClass instead of creating a second IDF device.
  if(!csosTouch.begin(SPI))return false;
  csosTouch.setRotation(1); // Raw ADC axes; calibrated by the measured affine map.
#if !CSOS_TOUCH_STUDY && (!CSOS_CALIBRATION_LAB || CSOS_PEN_ONLY)
  csosTouch.setPressureThreshold(TouchFilter::Pressure);
  csosTouch.setSPIFrequency(1000000);
  CSOS_LOGLN("[CSOS] touch_filter=median3 pressure=300 irq=required release_samples=3 spi_hz=1000000");
#endif
  // Measured on this exact panel in the five-target physical probe.
#if CSOS_CALIBRATION_LAB && !CSOS_PEN_ONLY
  const Point raw[3]={{672,615},{714,3560},{3366,655}};
  const Point targets[3]={{30,30},{290,30},{30,210}};
  csosCalibration.fit(raw,targets);
  const bool stored=loadTouchCalibration(csosCalibration,rotation);
  CSOS_LOGF("[CSOS] touch_calibration=%s\n",stored?"saved_pen":"baseline_three_points");
#else
  // Nine-point stylus profile: research/touch-pen-recalibration-spi1mhz-2026-11-04/comparison.json.
  // Embedded stylus-only fit; independently checked at eight edge targets, SPI 1 MHz.
  csosCalibration.a=-0.000291871479;
  csosCalibration.b=0.088288218273;
  csosCalibration.c=-25.818201710776;
  csosCalibration.d=0.066732643108;
  csosCalibration.e=0.000172229094;
  csosCalibration.f=-17.253566867538;
  csosCalibration.valid=true;
  CSOS_LOGLN("[CSOS] touch_calibration=embedded_pen_20261104_spi1mhz points=9 validation_rms_px=2.23");
#endif
  return true;
}
static FilteredTouch readFilteredTouch(TouchFilter& filter){
 const bool irqBefore=digitalRead(TOUCH_IRQ)==LOW;
 // Re-arm polling if the controller still signals contact after a weak sample.
 if(irqBefore)externalDisplay.armTouch();
 TS_Point raw;
 if(!externalDisplay.readTouch(raw))return {};
 const bool contact=irqBefore&&digitalRead(TOUCH_IRQ)==LOW;
 const Point pixel=csosCalibration.map({double(raw.y),double(raw.x)});
 return filter.sample(contact,raw.z,int(pixel.x),int(pixel.y));
}
bool localPanelTap(int& x,int& y){
 static TouchFilter filter;static TouchTap tap;
 static uint32_t last=0;
 if(millis()-last<20)return false;
 last=millis();
 const auto point=readFilteredTouch(filter);
 const uint64_t context=uint64_t(uiDepth)|(uint64_t(csosControls.menu)<<8)|(uint64_t(csosControls.system)<<9)|
  (uint64_t(csosControls.confirm)<<10)|(uint64_t(csosControls.guide)<<11)|(uint64_t(csosControls.details)<<12)|
  (uint64_t(csosControls.group)<<16)|(uint64_t(csosControls.page)<<24)|(uint64_t(csosControls.detailPage)<<32)|
  (uint64_t(csosControls.guidePage)<<40)|(uint64_t(csosControls.guideDetail)<<48)|(uint64_t(inSession)<<56);
 return tap.sample(point,context,x,y);
}
static void sendModifiedTouch(const uint8_t* payload,uint8_t length);
static bool csosTouchAllowed();
static void emitCsosTouch(const TouchClick& click) {
  if(!click.valid || !csosTouchSupported) return;
  uint8_t p[9]={(uint8_t)click.frame,(uint8_t)(click.frame>>8),(uint8_t)(click.frame>>16),(uint8_t)(click.frame>>24),
    (uint8_t)click.x,(uint8_t)(click.x>>8),(uint8_t)click.y,(uint8_t)(click.y>>8),click.button};
  sendModifiedTouch(p,sizeof(p));
  csosTouchEvents++;
#if CSOS_DIAGNOSTIC_TELEMETRY
  CSOS_LOGF("[CSOS] direct_click button=%u pixel=%u,%u displayed_frame=%u\n",click.button,click.x,click.y,(unsigned)click.frame);
#endif
}
static void serviceCsosTouch() {
  if(!csosTouchAllowed()){csosDirectTouch.cancel();csosTouchFilter.reset();return;}
  static uint32_t last=0;
  if(millis()-last<20) return;
  last=millis();
  // The same pressure/IRQ/median/release policy is used by pre-desktop taps.
  const auto point=readFilteredTouch(csosTouchFilter);
  if(point.pending)return; // Settling must not unblock a cancelled, still-held touch.
  if(point.down)csosTouchSamples++;
  if(csosTouchSupported)emitCsosTouch(csosDirectTouch.sample(point.down,point.x,point.y,last,csosDisplayedFrame));
  else {csosDirectTouch.cancel();csosTouchFilter.reset();}
}

// ============================================================================
// Session: keyboard -> actions. Each physical key remembers what its press
// sent, so its release sends the matching "up" even if Fn/Opt/mode changed.
// ============================================================================
ControlGesture csosOptGesture;
enum class PendingSetup:uint8_t {None,Access,Wifi,Management,Start,Stop};
PendingSetup pendingSetup=PendingSetup::None;
void confirmCodespaceStop();
LocalCommand sessionSetup=LocalCommand::None;

enum ActKind : uint8_t { A_NONE,A_KEY,A_MOVE,A_BTN,A_SCROLL,A_MOD,A_PAN,A_CLICK };
struct Act { ActKind kind;uint8_t arg;const char* name;uint32_t downAt,lastRep;uint8_t mods=0;bool used=false,cancel=false; };
Act acts[KEY_COUNT];
constexpr uint32_t AA_TAP_MS=350;
uint64_t sessPrevMask=0;
bool exitRequested=false;
float moveAccX=0,moveAccY=0;
uint32_t lastMoveSend=0,lastMoveTick=0;
char charNames[KEY_COUNT][2];
uint8_t csosPendingMods=0,csosHeldMods=0,csosRemoteMods=0;
uint8_t csosDragMods=0;
uint32_t lastPanSend=0;
bool csosDrag=false,csosCancelling=false,csosOfflineRetry=false;
bool csosPanSupported=false;
void sendPanDelta(int16_t dx,int16_t dy){
 if(!inSession||!csosFrameSeen||!csosPanSupported||zoomIdx==0||csosDrag||(!dx&&!dy))return;
 uint8_t payload[8];
 for(int n=0;n<4;n++)payload[n]=(csosDisplayedFrame>>(n*8))&255;
 payload[4]=uint16_t(dx)&255;payload[5]=uint16_t(dx)>>8;
 payload[6]=uint16_t(dy)&255;payload[7]=uint16_t(dy)>>8;
 sendMsg('V',payload,8);
}
void sendPan(uint8_t direction){sendPanDelta(direction==2?-32:direction==3?32:0,direction==0?-32:direction==1?32:0);}

const char* baseName(uint8_t c){
 switch(c){case KEY_BACKSPACE:return "backspace";case KEY_TAB:return "tab";
 case KEY_ENTER:return "enter";case ' ':return "space";
 case KEY_LEFT_SHIFT:return "shift";case KEY_LEFT_CTRL:return "ctrl";case KEY_LEFT_ALT:return "alt";}
 return nullptr;
}
const char* fnName(uint8_t c){
 switch(c){case ';':return "up";case '.':return "down";case ',':return "left";case '/':return "right";
 case '`':return "esc";case KEY_BACKSPACE:return "delete";
 case '1':return "f1";case '2':return "f2";case '3':return "f3";case '4':return "f4";
 case '5':return "f5";case '6':return "f6";case '7':return "f7";case '8':return "f8";
 case '9':return "f9";case '0':return "f10";case '-':return "f11";case '=':return "f12";
 case '[':return "page_up";case ']':return "page_down";case 'h':return "home";case 'e':return "end";
 case 'i':return "insert";case 'p':return "print_screen";case 'm':return "menu";
 case KEY_ENTER:return "enter";case ' ':return "space";case KEY_TAB:return "tab";}
 return nullptr;
}
bool isRepeatable(const char* n){return strcmp(n,"shift")&&strcmp(n,"ctrl")&&strcmp(n,"alt")&&strcmp(n,"cmd");}
const char* modName(uint8_t bit){return bit==1?"ctrl":bit==2?"alt":bit==4?"shift":"cmd";}
uint8_t aaShift(){return csosControls.aa&&csosControls.mode==InputMode::Keys?4:0;}
void ensureMods(uint8_t mask){
 for(uint8_t bit=1;bit<=8;bit<<=1)if((mask&bit)&&!(csosRemoteMods&bit)){sendKey(modName(bit),true);csosRemoteMods|=bit;}
}
void releaseUnusedMods(){
 uint8_t wanted=(csosDrag?csosDragMods:0)|(csosHeldMods&4);
 for(const auto &a:acts){if(a.kind==A_MOD&&a.used)wanted|=a.arg;if(a.kind==A_KEY||a.kind==A_BTN)wanted|=a.mods;}
 for(uint8_t bit=1;bit<=8;bit<<=1)if((csosRemoteMods&bit)&&!(wanted&bit)){sendKey(modName(bit),false);csosRemoteMods&=~bit;}
}
uint8_t beginPointerAction(){
 for(auto &a:acts)if(a.kind==A_MOD)a.used=true;
 const uint8_t mask=csosPendingMods|csosHeldMods|aaShift();
 ensureMods(mask);csosPendingMods=0;return mask;
}
static bool csosTouchAllowed(){
 return inSession&&csosFrameSeen&&csosControls.pointerAllowed()&&!csosControls.movingWindow&&!csosDrag&&!externalDark;
}
static void sendModifiedTouch(const uint8_t* payload,uint8_t length){
 for(auto &a:acts)if(a.kind==A_MOD)a.used=true;
 ensureMods(csosPendingMods|csosHeldMods|aaShift());
 sendMsg('D',payload,length);
 csosPendingMods=0;releaseUnusedMods();hudDirty=true;
}
void releaseKey(int i){
 Act &a=acts[i];
 if(a.kind==A_KEY)sendKey(a.name,false);
 else if(a.kind==A_BTN)sendButton(a.arg,false);
 else if(a.kind==A_CLICK&&!csosCancelling&&!csosDrag&&inSession){
  const uint8_t button=millis()-a.downAt>=600?1:0;
  beginPointerAction();
  sendButton(button,true);sendButton(button,false);
 }
 else if(a.kind==A_MOD){
  csosHeldMods&=~a.arg;
  if(a.arg==4){
   if(!csosCancelling&&!a.used&&uint32_t(millis()-a.downAt)<AA_TAP_MS)csosControls.aa=!csosControls.aa;
  }else if(!csosCancelling&&!a.used&&!a.cancel)csosPendingMods|=a.arg;
 }
 a.kind=A_NONE;
 releaseUnusedMods();
 hudDirty=true;
}
static void finishWindowMove(const char* key){
 if(!csosControls.movingWindow)return;
 csosControls.movingWindow=false;
 if(inSession){sendKey(key,true);sendKey(key,false);}
 hudDirty=true;
}
void cancelInputActions(){
 finishWindowMove("esc");
 csosCancelling=true;
 for(int k=0;k<KEY_COUNT;k++)releaseKey(k);
 csosCancelling=false;
 if(csosDrag){sendButton(0,false);csosDrag=false;}csosDragMods=0;
 csosPendingMods=csosHeldMods=0;releaseUnusedMods();
 moveAccX=moveAccY=0;csosDirectTouch.cancel();
}
void releaseAll(){cancelInputActions();csosOptGesture.reset();sessPrevMask=0;}
void resetSessionInput(){
 csosControls.movingWindow=false;
 csosOptGesture.reset();
 for(auto &a:acts)a={};
 csosPendingMods=csosHeldMods=csosRemoteMods=csosDragMods=0;
 csosDrag=csosCancelling=csosOfflineRetry=false;
 moveAccX=moveAccY=0;lastPanSend=0;
 csosDirectTouch.cancel();
}
void cycleInputMode(){cancelInputActions();csosControls.cycle();hudDirty=true;}
void cycleBrightness(){brightness=brightness>=10?1:min(10,brightness+1);applyBrightness();persistDeviceSettings();}
void cycleExternalBrightness(){externalBrightness=externalBrightness>=10?1:externalBrightness+1;externalDark=false;applyBrightness();persistDeviceSettings();}
void sendSoundWanted();
void executeLocalCommand(LocalCommand command){
 if(command==LocalCommand::None||command==LocalCommand::Remote)return;
 if((command==LocalCommand::Brightness||command==LocalCommand::ExternalBrightness)&&csosControls.system){
  csosControls.system=false;csosControls.adjustingLight=true;
  csosControls.lightExternal=command==LocalCommand::ExternalBrightness;
  hudDirty=hudPriorityPending=true;return;
 }
 if(command==LocalCommand::LightIncrease||command==LocalCommand::LightDecrease){
  auto& value=csosControls.lightExternal?externalBrightness:brightness;
  const int next=std::max(1,std::min(10,int(value)+(command==LocalCommand::LightIncrease?1:-1)));
  if(next!=value){value=next;if(csosControls.lightExternal)externalDark=false;applyBrightness();persistDeviceSettings();hudDirty=true;}
  return;
 }
 if(inSession&&(command==LocalCommand::Configure||command==LocalCommand::Wifi||command==LocalCommand::Management)){
  sessionSetup=command;csosControls.close();hudDirty=true;return;
 }
 if(command==LocalCommand::CancelInput){cancelInputActions();hudDirty=true;return;}
 if(command==LocalCommand::Start&&inSession){csosControls.unavailable=true;hudDirty=true;return;}
 if(command==LocalCommand::Wifi||command==LocalCommand::Management||command==LocalCommand::Start||command==LocalCommand::Stop){
  if(command==LocalCommand::Stop)confirmCodespaceStop();
  else pendingSetup=command==LocalCommand::Wifi?PendingSetup::Wifi:command==LocalCommand::Management?PendingSetup::Management:PendingSetup::Start;
  if(inSession){exitRequested=true;}csosControls.close();hudDirty=true;return;
 }
 if(command==LocalCommand::Retry){
  if(inSession){csosControls.close();csosControls.details=true;csosControls.page=5;csosControls.detailPage=0;}
  else{connectionPaused=false;csosOfflineRetry=true;csosControls.close();}
  hudDirty=true;return;
 }
 if(command==LocalCommand::Brightness){cycleBrightness();hudDirty=true;return;}
 if(command==LocalCommand::ExternalBrightness){cycleExternalBrightness();hudDirty=true;return;}
 if(command==LocalCommand::ExternalLight){
  if(!inSession)csosControls.unavailable=true;
  else{externalDark=!externalDark;csosDirectTouch.cancel();applyBrightness();}
  hudDirty=true;return;
 }
 if(command==LocalCommand::Configure){pendingSetup=PendingSetup::Access;configureRequested=true;exitRequested=inSession;csosControls.close();return;}
 if(!inSession){csosControls.unavailable=true;hudDirty=true;return;}
 switch(command){
 case LocalCommand::AudioToggle:
  if(csosControls.audioIndex==0){DuplexAudio::listen=!DuplexAudio::listen.load();sendSoundWanted();}
  else if(csosControls.audioIndex==1)DuplexAudio::microphone(!DuplexAudio::micWanted.load());
#if CSOS_USB_CONSOLE
  else if(csosControls.audioIndex==5)DuplexAudio::traceWanted=DuplexAudio::micActive&&!DuplexAudio::traceWanted.load();
#endif
  break;
 case LocalCommand::AudioUp:case LocalCommand::AudioDown:{
  int d=command==LocalCommand::AudioUp?1:-1;
  if(csosControls.audioIndex==2)DuplexAudio::volume=std::max(0,std::min(10,DuplexAudio::volume.load()+d));
  if(csosControls.audioIndex==3)DuplexAudio::micDb=std::max(0,std::min(20,DuplexAudio::micDb.load()+d));
  if(csosControls.audioIndex==4)DuplexAudio::outDb=std::max(0,std::min(20,DuplexAudio::outDb.load()+d));
  break;}
 case LocalCommand::WindowMove:
  if(csosFrameSeen&&!externalDark&&!csosDrag){
   cancelInputActions();
   sendKey("alt",true);sendKey("f7",true);sendKey("f7",false);sendKey("alt",false);
   csosControls.movingWindow=true;
  }
  break;
 case LocalCommand::ZoomIn:if(csosFrameSeen)sendZoom(1);break;
 case LocalCommand::ZoomOut:if(csosFrameSeen)sendZoom(-1);break;
 case LocalCommand::WheelUp:sendWheel(1,0);break;
 case LocalCommand::WheelDown:sendWheel(-1,0);break;
 case LocalCommand::PanUp:case LocalCommand::PanDown:case LocalCommand::PanLeft:case LocalCommand::PanRight:
  lastPanSend=0;break; // Aggregate all held EASD directions once in serviceInput.
 case LocalCommand::Fit:if(zoomIdx!=0||stretchOn)sendZoom(0);break;
 case LocalCommand::ViewToggle:
  if(csosFrameSeen){if(zoomIdx==0){if(stretchOn)sendZoom(0);for(uint8_t z=1;z<zoomCount;z++)sendZoom(1);}else sendZoom(0);}break;
 case LocalCommand::Mouse:cycleInputMode();csosControls.close();break;
 case LocalCommand::Refresh:sendMsg('R',nullptr,0);break;
 case LocalCommand::Quality:quality=(quality+1)%3;sendQuality();persistDeviceSettings();break;
 case LocalCommand::Cursor:{uint8_t show=!cursorShown;sendMsg('C',&show,1);break;}
 case LocalCommand::Monitor:sendMsg('N',nullptr,0);break;
 case LocalCommand::Fps:fpsCapIdx=(fpsCapIdx+1)%5;sendFpsCap();persistDeviceSettings();break;
 case LocalCommand::SuperTap:sendKey("cmd",true);sendKey("cmd",false);csosControls.close();break;
 case LocalCommand::SuperNext:csosPendingMods|=8;csosControls.close();break;
 case LocalCommand::Disconnect:connectionPaused=true;exitRequested=true;csosControls.close();break;
 default:break;
 }
 hudDirty=true;haptic();
}
void pressKey(int i,uint64_t cur){
 uint8_t c=baseAt(i);Act &a=acts[i];a={A_NONE,0,nullptr,millis(),millis()};
 if(i==IDX_FN){csosControls.press(true,false,0);if(csosControls.fn)haptic(20);hudDirty=true;return;}
 if(i==IDX_SHIFT){
  if(csosControls.mode==InputMode::Keys&&!csosControls.local()){
   if(cur&kbit(IDX_OPT))return;
   a.kind=A_MOD;a.arg=4;a.used=(cur&~(kbit(IDX_SHIFT)|kbit(IDX_OPT)))!=0;
   csosHeldMods|=4;ensureMods(4);
  }else csosControls.toggleAa();
  hudDirty=true;return;
 }
 if(i==IDX_OPT)return; // Gesture is resolved before dispatching key edges.
 if(csosControls.movingWindow){
  if(c==KEY_ENTER||c=='`'){finishWindowMove(c==KEY_ENTER?"enter":"esc");return;}
  const char* direction=c==';'?"up":c=='.'?"down":c==','?"left":c=='/'?"right":nullptr;
  if(direction){a.kind=A_KEY;a.name=direction;sendKey(direction,true);}
  return;
 }
 const uint8_t mod=i==IDX_CTRL?1:i==IDX_ALT?2:csosControls.mode==InputMode::Keys&&!csosControls.local()&&csosControls.fn&&c==KEY_TAB?8:0;
 const bool wasLocal=csosControls.local();
 // Vista keeps remote modifiers available, while ordinary typing stays local.
 const auto command=csosControls.vista()&&mod?LocalCommand::Remote:
  csosControls.press(false,false,c==KEY_ENTER?13:c==KEY_TAB?9:c);
 if(command!=LocalCommand::Remote){
  if(wasLocal&&!csosControls.local())cancelInputActions();
  executeLocalCommand(command);
  if(inSession&&(command==LocalCommand::WheelUp||command==LocalCommand::WheelDown)){
   a.kind=A_SCROLL;a.arg=command==LocalCommand::WheelUp?1:0;
  }
  if(inSession&&command>=LocalCommand::PanUp&&command<=LocalCommand::PanRight){
   a.kind=A_PAN;a.arg=uint8_t(command)-uint8_t(LocalCommand::PanUp);
  }
  hudDirty=true;return;
 }
 if(csosControls.mouse()&&!mod){
  switch(c){
  case ';':case '.':case ',':case '/':
   a.kind=A_MOVE;a.arg=c==';'?0:c=='.'?1:c==','?2:3;
   return;
  case KEY_ENTER:if(!csosDrag)a.kind=A_CLICK;return;
  case ' ':
   if(!csosDrag){csosDragMods=beginPointerAction();csosDrag=true;sendButton(0,true);}
   else{sendButton(0,false);csosDrag=false;csosDragMods=0;releaseUnusedMods();}
   hudDirty=true;return;
  case '\\':a.kind=A_BTN;a.arg=1;a.mods=beginPointerAction();sendButton(1,true);return;
  case '\'':a.kind=A_BTN;a.arg=2;a.mods=beginPointerAction();sendButton(2,true);return;
  case '[':a.kind=A_SCROLL;a.arg=1;sendWheel(1,0);return;
  case ']':a.kind=A_SCROLL;a.arg=0;sendWheel(-1,0);return;
  default:return;
  }
 }
 if(mod){a.kind=A_MOD;a.arg=mod;a.cancel=csosPendingMods&mod;csosPendingMods&=~mod;csosHeldMods|=mod;hudDirty=true;return;}
 const char* name=csosControls.fn?fnName(c):nullptr;
 if(!name)name=baseName(c);
 if(!name){charNames[i][0]=(char)c;charNames[i][1]=0;name=charNames[i];}
 a.kind=A_KEY;a.name=name;a.mods=(csosPendingMods&~csosHeldMods)|aaShift();
 for(auto &m:acts)if(m.kind==A_MOD)m.used=true;
 ensureMods(csosPendingMods|csosHeldMods|aaShift());csosPendingMods=0;
 sendKey(name,true);hudDirty=true;(void)cur;
}
uint64_t controlVisualState(){
 const uint64_t flags=uint64_t(csosControls.fn)|(uint64_t(csosControls.aa)<<1)|(uint64_t(csosControls.precise)<<2)|
  (uint64_t(csosControls.local())<<3)|(uint64_t(csosControls.movingWindow)<<4)|(uint64_t(csosDrag)<<5)|
  (uint64_t(csosControls.menu)<<6)|(uint64_t(csosControls.system)<<7)|(uint64_t(csosControls.guide)<<8)|
  (uint64_t(csosControls.details)<<9)|(uint64_t(csosControls.confirm)<<10);
 return flags|(uint64_t(csosControls.mode)<<12)|(uint64_t(csosPendingMods)<<16)|(uint64_t(csosHeldMods)<<20)|
  (uint64_t(csosControls.page)<<24)|(uint64_t(csosControls.systemIndex)<<32)|(uint64_t(csosControls.guidePage)<<40)|
  (uint64_t(csosControls.guideDetail)<<48)|(uint64_t(csosControls.detailPage)<<56);
}
void processInputEdges(uint64_t cur){
 const auto visualBefore=controlVisualState();
 const bool batchLocal=csosControls.local();
 const uint64_t down=cur&~sessPrevMask,up=sessPrevMask&~cur;
 if(acts[IDX_SHIFT].kind==A_MOD&&(down&~kbit(IDX_SHIFT)))acts[IDX_SHIFT].used=true;
 if(down&kbit(IDX_OPT))cancelInputActions(); // Cancel before releasing a held click.
 for(int i=0;i<KEY_COUNT;i++)if((up&kbit(i))&&i!=IDX_OPT)releaseKey(i);
 const auto gesture=csosOptGesture.update(bool(cur&kbit(IDX_OPT)),millis(),csosControls.local());
 if(gesture==ControlGesture::Cycle)cycleInputMode();
 if(gesture==ControlGesture::Open){cancelInputActions();if(!csosControls.local())csosControls.openOptions();hudDirty=true;}
 if(gesture==ControlGesture::Close){cancelInputActions();csosControls.close();hudDirty=true;}
 if(down&kbit(IDX_FN))pressKey(IDX_FN,cur);
 if(down&kbit(IDX_SHIFT))pressKey(IDX_SHIFT,cur);
 bool suppress=bool(cur&kbit(IDX_OPT))||gesture!=ControlGesture::None;
 if(!suppress){
  if(down&kbit(IDX_CTRL))pressKey(IDX_CTRL,cur);
  if(down&kbit(IDX_ALT))pressKey(IDX_ALT,cur);
 }
 for(int i=0;i<KEY_COUNT;i++){
  if(!(down&kbit(i))||i==IDX_OPT||i==IDX_FN||i==IDX_SHIFT||i==IDX_CTRL||i==IDX_ALT||suppress)continue;
  const bool wasLocal=csosControls.local();
  const bool wasMoving=csosControls.movingWindow;
  pressKey(i,cur);
  if((wasLocal&&!csosControls.local())||wasMoving!=csosControls.movingWindow)suppress=true;
 }
 (void)batchLocal;sessPrevMask=cur;
 if(visualBefore!=controlVisualState()){traceUiBoundary("input_changed");hudDirty=true;hudPriorityPending=true;}
}

void serviceInput() {
  uint64_t cur = readKeyMask();
  if(cur!=sessPrevMask) {
    csosKeyboardChanges++;
#if CSOS_DIAGNOSTIC_TELEMETRY
    CSOS_LOGF("[CSOS] keyboard_change=%u pressed_count=%u mode=%s irq=%d\n",(unsigned)csosKeyboardChanges,(unsigned)__builtin_popcountll(cur),csosControls.mode==InputMode::Mouse?"MOUSE":csosControls.mode==InputMode::View?"VISTA":"TASTI",digitalRead(11));
#endif
  }
  processInputEdges(cur);
  // The hardware GPIO button is not a mode selector.
  // Taken only after pressKey() stamped downAt: an earlier "now" would make
  // now - downAt wrap around and fire the auto-repeat instantly.
  uint32_t now = millis();

  // Typematic repeat: injected key-downs are not auto-repeated by the PC.
  float vx = 0, vy = 0;int panX=0,panY=0;
  uint32_t moveSince = 0;
  for (int i = 0; i < KEY_COUNT; ++i) {
    if (!(cur & kbit(i))) continue;
    Act& a = acts[i];
    if (a.kind == A_KEY && isRepeatable(a.name) && now - a.downAt > 450 && now - a.lastRep >= 40) {
      a.lastRep = now;
      sendKey(a.name, true);
    } else if (a.kind == A_SCROLL && now - a.downAt > 350 && now - a.lastRep >= 70) {
      a.lastRep = now;
      sendWheel(a.arg ? 1 : -1, 0);
    } else if (a.kind == A_PAN) {
      if(a.arg==0)--panY;else if(a.arg==1)++panY;else if(a.arg==2)--panX;else ++panX;
    } else if (a.kind == A_MOVE) {
      if (a.arg == 0) vy -= 1; else if (a.arg == 1) vy += 1; else if (a.arg == 2) vx -= 1; else vx += 1;
      if (!moveSince || a.downAt < moveSince) moveSince = a.downAt;
    }
  }

  if(panX||panY){if(!lastPanSend||now-lastPanSend>=200){sendPanDelta(panX*32,panY*32);lastPanSend=now;}}
  else lastPanSend=0;

  // Mouse movement in 1/8 view-pixel units; speed ramps up while held.
  float dt = lastMoveTick ? (now - lastMoveTick) / 1000.0f : 0;
  lastMoveTick = now;
  if (vx != 0 || vy != 0) {
    float held = (now - moveSince) / 1000.0f;
    float speed = csosControls.precise ? 18.0f : 70.0f + min(held / 0.8f, 1.0f) * 480.0f;  // view px/s
    if (vx && vy) speed *= 0.7071f;
    moveAccX += vx * speed * dt * 8;
    moveAccY += vy * speed * dt * 8;
    if (now - lastMoveSend >= 15) {
      int16_t dx = int16_t(std::max(-32767.0f,std::min(32767.0f,moveAccX)));
      int16_t dy = int16_t(std::max(-32767.0f,std::min(32767.0f,moveAccY)));
      if ((dx || dy) && sendMove(dx, dy)) { moveAccX -= dx; moveAccY -= dy; lastMoveSend = now; }
    }
  } else {
    moveAccX = moveAccY = 0;
  }
}

// ============================================================================
// Session: incoming stream, read by rxTask() on core 0 so TCP keeps flowing
// while loop() on core 1 decodes and draws.
//   'T' tile | 'F' frame end | 'S' audio | 'H' heartbeat | 'X' bye
// ============================================================================
constexpr int TILE_SLOTS = 2;  // one being filled from the network, one being drawn
uint8_t* tileSlot[TILE_SLOTS];
struct RxItem {
  uint8_t type, slot;
  uint16_t x, y, width, height;
  uint32_t len;
  uint8_t hdr[11];
#if CSOS_CXL_DIAG_TRACE
  uint32_t diagFirstTileRxUs, diagLastTileRxUs, diagFrameRxUs;
  uint16_t diagTileCount;
#endif
};
#if CSOS_CXL_DIAG_TRACE
// The UI is the only writer of this ring. RX passes frame timestamps in F's
// already-existing RxItem queue; no cross-core mutation of trace buffers.
static CxlDiag::Recorder diagFrameTrace;
static CxlDiag::AudioGapRecorder diagAudioTrace;
static uint16_t diagUiTiles=0;
static uint32_t diagUiLastDrawUs=0;

static void dumpCxlDiagnostic() {
  unsigned count=0;
  mz_ulong crc=MZ_CRC32_INIT;
#if CSOS_USB_CONSOLE
  bool complete=CSOS_LOGLN("[CXL-DIAG BEGIN v1]")>0;
#else
  bool complete=false;
#endif
  auto row=[&](const char* format,auto... values){
    char line[128];
    const int size=snprintf(line,sizeof(line),format,unsigned(values)...);
    if(size<=0 || size>=int(sizeof(line))){
      complete=false;
      return;
    }
    const auto* bytes=reinterpret_cast<const uint8_t*>(line);
    crc=mz_crc32(crc,bytes,size);
    if(CsosSerial.write(bytes,size)!=size_t(size))complete=false;
    ++count;
  };
  const auto frames=diagFrameTrace.stats();
  const auto tx=diagAckTrace.stats();
  const auto audio=diagAudioTrace.stats();
  row("frame_stats,%u,%u,%u,%u,%u,%u,%u\n",frames.total,frames.sampled,frames.anomalies,
      frames.sampled_overwritten,frames.anomalies_overwritten,frames.invalid,frames.saturated);
  row("tx_stats,%u,%u,%u,%u,%u,%u\n",tx.total,tx.sampled,tx.failures,
      tx.sampled_overwritten,tx.failures_overwritten,tx.duration_saturated);
  row("audio_stats,%u,%u,%u,%u,%u,%u\n",audio.messages,audio.bytes,audio.intervals,
      audio.long_count,audio.overwritten,audio.hist_saturated);
  auto frameRow=[&](const CxlDiag::FrameTrace16& f,unsigned anomalous){
    row("frame,%u,%u,%u,%u,%u,%u,%u\n",f.frame_id,f.first_tile_rx_us,
        f.last_tile_rx_64,f.f_rx_64,f.draw_done_64,f.ack_queued_64,anomalous);
  };
  for(uint32_t i=frames.sampled_overwritten;i<frames.sampled;++i)
    frameRow(diagFrameTrace.sampledAt(i),0);
  for(uint32_t i=frames.anomalies_overwritten;i<frames.anomalies;++i)
    frameRow(diagFrameTrace.anomalyAt(i),1);
  auto txRow=[&](const CxlDiag::AckTx12& a){
    row("ack_tx,%u,%u,%u,%u\n",a.frame_id,a.started_us,a.duration_64,a.status);
  };
  for(uint32_t i=tx.sampled_overwritten;i<tx.sampled;++i)txRow(diagAckTrace.sampledAt(i));
  for(uint32_t i=tx.failures_overwritten;i<tx.failures;++i)txRow(diagAckTrace.failedAt(i));
  for(unsigned i=0;i<256;++i)
    if(const auto hits=diagAudioTrace.histogramAt(i))row("audio_hist,%u,%u\n",i,hits);
  for(uint32_t i=audio.overwritten;i<audio.long_count;++i){
    const auto gap=diagAudioTrace.longAt(i);
    row("audio_gap,%u,%u\n",gap.at_us,gap.gap_us);
  }
  CSOS_LOGF("[CXL-DIAG END count=%u crc32=%08lx complete=%u]\n",
      count,static_cast<unsigned long>(crc),unsigned(complete));
}
#endif
constexpr uint8_t LOSSLESS_VIDEO_CAPABILITY = 0x20;
constexpr uint8_t LOSSLESS_VIDEO_VERSION = 1;
static bool losslessVideoRequested = false;
QueueHandle_t rxQueue = nullptr, freeSlots = nullptr;
std::atomic<bool> rxStop{false}, rxRunning{false};
std::atomic<uint32_t> rxBytes{0};
uint32_t lastRxAt = 0;
// Written by the receiver and read only after acquiring its release of rxRunning.
SessionExit rxError = SessionExit::None;
SessionExit sessionExit = SessionExit::None; // Session/UI task only.
int rxGoodbye = -1;
uint32_t lastPingAt = 0;
uint8_t byeReason = 255;  // 255 = no goodbye received (connection just dropped)

static inline uint16_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static inline uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// ---- Audio: shared 16 kHz ADV duplex audio ------------------------------------------
bool audioHandshakeAsked=false;
bool startAudio(){soundOn=true;DuplexAudio::listen=true;return DuplexAudio::begin(&conn);}
void stopAudio(){DuplexAudio::end();}
void sendSoundWanted(){uint8_t p[1]={uint8_t(DuplexAudio::negotiated&&DuplexAudio::listen)};sendMsg('U',p,1);}

// ---- Network task --------------------------------------------------------------
SessionExit transportSessionExit(TransportFailure failure) {
  switch (failure) {
    case TransportFailure::TxQueue: return SessionExit::TxQueue;
    case TransportFailure::TxWrite: return SessionExit::TxWrite;
    case TransportFailure::RxBlock: return SessionExit::WssReceive;
    case TransportFailure::Protocol: return SessionExit::WssProtocol;
    case TransportFailure::Owner: return SessionExit::TransportOwner;
    default: return SessionExit::WssLost;
  }
}
bool rxRead(uint8_t* dst, size_t n) {
  RxAudioProbe::Timer timer(RxAudioProbe::probe.readUs,[]{return uint32_t(micros());});
  while (n) {
    if (rxStop) return false;
    int r = conn.read(dst, n);
    if (r > 0) { dst += r; n -= r; rxBytes += r; RxAudioProbe::probe.rxBytes+=r; lastRxAt = millis(); continue; }
    if (!conn.connected()) { rxError = transportSessionExit(conn.failure()); return false; }
    if (millis() - lastRxAt > 8000) { rxError = SessionExit::RxIdle; return false; }
    const uint32_t idleAt=micros();vTaskDelay(1);
    RxAudioProbe::probe.idleUs+=uint32_t(micros()-idleAt);
  }
  return true;
}

bool rxPush(const RxItem& it) {
  RxAudioProbe::Timer timer(RxAudioProbe::probe.queueUs,[]{return uint32_t(micros());});
  const uint32_t queueStartedUs=micros();
  const uint32_t started=millis();
  while (xQueueSend(rxQueue, &it, pdMS_TO_TICKS(10)) != pdTRUE) {
    conn.service(false);
    if (rxStop) return false;
    if (!conn.connected()) { rxError = transportSessionExit(conn.failure()); return false; }
    if (millis()-started>2000) { rxError = SessionExit::RxQueue; return false; }
  }
  MediaTelemetry::peak(MediaTelemetry::queueWaitUs,micros()-queueStartedUs);
  return true;
}

void rxTask(void*) {
  conn.claimOwner();
  rxGoodbye = -1;
  rxError = SessionExit::None;
  RxItem it = {};
#if CSOS_CXL_DIAG_TRACE
  uint32_t diagRxFirst=0,diagRxLast=0;
  uint16_t diagRxTiles=0;
#endif
  uint8_t type, hdr[12], scratch[256];
  bool ok = true;
  while (ok && !rxStop) {
    if (!rxRead(&type, 1)) break;
    switch (type) {
      case 'T':
      case 'J': {
        if (!(ok = rxRead(hdr, 12))) break;
        uint32_t len = rd32(hdr + 8);
        if (!len || len > JPEG_MAX) { rxError = SessionExit::TileSize; ok = false; break; }
        uint8_t slot;
        const uint32_t slotStartedUs=micros();
        const uint32_t waiting=millis();
        while (xQueueReceive(freeSlots, &slot, pdMS_TO_TICKS(10)) != pdTRUE) {
          conn.service(false);
          if (rxStop) { ok = false; break; }
          if (!conn.connected()) { rxError = transportSessionExit(conn.failure()); ok = false; break; }
          if (millis()-waiting>2000) { rxError = SessionExit::RxSlot; ok = false; break; }
        }
        const uint32_t slotUs=micros()-slotStartedUs;
        RxAudioProbe::probe.slotUs+=slotUs;
        MediaTelemetry::peak(MediaTelemetry::slotWaitUs,slotUs);
        if (!ok) break;
        if (!(ok = rxRead(tileSlot[slot], len))) { xQueueSend(freeSlots, &slot, 0); break; }
#if CSOS_CXL_DIAG_TRACE
        // Timestamp is end of this full tile's RX payload, not first byte.
        const uint32_t tileRxEndUs=micros();
        if(!diagRxTiles)diagRxFirst=tileRxEndUs;
        diagRxLast=tileRxEndUs;
        if(diagRxTiles!=UINT16_MAX)++diagRxTiles;
#endif
        it.type = type; it.slot = slot; it.x = rd16(hdr); it.y = rd16(hdr + 2); it.len = len;
        it.width = rd16(hdr + 4); it.height = rd16(hdr + 6);
        ok = rxPush(it);
        RxAudioProbe::probe.videoRecords++;RxAudioProbe::probe.videoBytes+=len;
        if (!ok) xQueueSend(freeSlots, &slot, 0);
        break;
      }
      case 'F':
        if (!(ok = rxRead(it.hdr, 11))) break;
        it.type = 'F';
#if CSOS_CXL_DIAG_TRACE
        it.diagFirstTileRxUs=diagRxFirst;
        it.diagLastTileRxUs=diagRxLast;
        it.diagFrameRxUs=micros();
        it.diagTileCount=diagRxTiles;
        diagRxFirst=diagRxLast=0;diagRxTiles=0;
#endif
        ok = rxPush(it);
        break;
#if CSOS_CXL_DIAG_TRACE
      case 'E': {
        uint8_t nonce[4];
        if(!(ok=rxRead(nonce,4)))break;
        // Nonfatal: diagnostic echo never takes a frame ACK's reserved slot.
        conn.queueDiagnosticEcho(nonce);
        break;
      }
#endif
      case 'a': {
        uint8_t p[5];if(!(ok=rxRead(p,5)))break;
        DuplexAudio::accept(p[0],rd16(p+1),rd16(p+3));sendSoundWanted();hudDirty=true;break;
      }
      case 'i': {
        uint8_t p[5];if(!(ok=rxRead(p,5)))break;
        if(rd16(p)==DuplexAudio::token&&rd16(p+2)==DuplexAudio::captureEpoch){
          DuplexAudio::remoteState=p[4];DuplexAudio::hostMic=p[4]==2&&DuplexAudio::micWanted;
          if(p[4]==3){DuplexAudio::errorCode=6;DuplexAudio::micWanted=false;}
        }hudDirty=true;break;
      }
      case 'S': {
        if(!(ok=rxRead(hdr,2)))break;uint32_t len=rd16(hdr);
#if CSOS_CXL_DIAG_TRACE
        const uint16_t packetBytes=len;
#endif
        while(len&&ok){uint32_t n=min<uint32_t>(len,sizeof(scratch));ok=rxRead(scratch,n);if(ok)DuplexAudio::receive(scratch,n);len-=n;}
#if CSOS_CXL_DIAG_TRACE
        if(ok && packetBytes)diagAudioTrace.record(micros(),packetBytes);
#endif
        break;
      }
      case 'M': {  // temperatures
        uint8_t cnt;
        if (!(ok = rxRead(&cnt, 1))) break;
        uint8_t stored = 0;
        for (uint8_t i = 0; i < cnt && ok; ++i) {
          uint8_t nl, name[32], tv[2];
          if (!(ok = rxRead(&nl, 1))) break;
          if (nl > sizeof(name)) { rxError = SessionExit::CxlProtocol; ok = false; break; }
          if (!(ok = rxRead(name, nl)) || !(ok = rxRead(tv, 2))) break;
          if (stored < MAX_TEMPS) {
            uint8_t k = min<uint8_t>(nl, 12);
            memcpy(tempNames[stored], name, k); tempNames[stored][k] = 0;
            tempX10[stored] = (int16_t)(tv[0] | (tv[1] << 8));
            ++stored;
          }
        }
        if (ok) { tempCount = stored; tempsAt = millis(); }
        break;
      }
      case 'H':
        break;
      case 'X':
        if (!(ok = rxRead(hdr, 1))) break;
        rxGoodbye = hdr[0];
        ok = false;
        break;
      default:
        rxError = SessionExit::CxlProtocol;
        ok = false;
    }
  }
  rxRunning.store(false, std::memory_order_release);
  vTaskDelete(nullptr);
}

// Draw each JPEG band in one window. Splitting a wide band into adjacent
// windows produced persistent gaps on the external TFT during zoom.
constexpr int JPEG_BAND_ROWS = 16;
// Own both complete MCU-row bands: 4:2:0 produces 16 rows, too. Decode/copy
// the next band while DMA reads the previous one, without borrowing JPEGDEC RAM.
alignas(4) static uint16_t jpegBand[2 * OUT_W * JPEG_BAND_ROWS];
static unsigned jpegBandIndex;
static int jpegLeft, jpegRight, jpegTop, jpegBottom;
static int jpegNextX, jpegBandHeight;
static bool jpegDrawFailed;

int jpegDraw(JPEGDRAW* block) {
  const int width = block->iWidthUsed;
  const int height = block->iHeight;
  if (block->x != jpegNextX || block->y != jpegTop || width < 1 ||
      width > block->iWidth || height < 1 || height > JPEG_BAND_ROWS ||
      block->x + width > jpegRight || jpegTop + height > jpegBottom ||
      (jpegNextX != jpegLeft && height != jpegBandHeight)) {
    jpegDrawFailed = true;
    return 0;
  }

  const int bandWidth = jpegRight - jpegLeft;
  uint16_t* band = jpegBand + (jpegBandIndex & 1) * (OUT_W * JPEG_BAND_ROWS);
  // Always own the pixels: JPEGDEC may overwrite its callback buffer immediately.
  for (int row = 0; row < height; ++row) {
    memcpy(band + row * bandWidth + block->x - jpegLeft,
           block->pPixels + row * block->iWidth, width * sizeof(uint16_t));
  }
  jpegBandHeight = height;
  jpegNextX += width;
  if (jpegNextX == jpegRight) {
    const uint32_t waitStartedUs=micros();
    externalDisplay.waitComplete();
    MediaTelemetry::dmaWaitUs+=micros()-waitStartedUs;
    if(!externalDisplay.present(jpegLeft, jpegTop, bandWidth, height,
                     reinterpret_cast<const lgfx::swap565_t*>(band))){
      jpegDrawFailed=true;return 0;
    }
    ++jpegBandIndex;
    jpegTop += height;
    jpegNextX = jpegLeft;
  }
  return 1;
}

bool drawTile(const RxItem& it) {
  auto& jpeg = *new (&videoDecoder.jpeg) JPEGDEC;
  if (!jpeg.openRAM(tileSlot[it.slot], it.len, jpegDraw)) return false;
  const int width = jpeg.getWidth();
  const int height = jpeg.getHeight();
  if (width < 1 || height < 1 || it.x + width > OUT_W || it.y + height > OUT_H) {
    jpeg.close();
    return false;
  }
  externalDisplay.waitComplete();
  jpegBandIndex = 0;
  jpegLeft = jpegNextX = it.x;
  jpegRight = it.x + width;
  jpegTop = it.y;
  jpegBottom = it.y + height;
  jpegDrawFailed = false;
  jpeg.setPixelType(RGB565_BIG_ENDIAN);
  if(!externalDisplay.beginBatch()){jpeg.close();return false;}
  const uint32_t decodeStartedUs=micros();
  const bool decoded = jpeg.decode(it.x, it.y, 0) != 0;
  MediaTelemetry::jpegDecodeUs+=micros()-decodeStartedUs;
  // Preserve the DMA-only timing boundary while the LCD batch still owns SPI.
  // endBatch() releases TFT CS and the host lock; only then may we perform
  // the shared-bus handoff/NOP before an Arduino XPT2046 transaction.
  const uint32_t waitStartedUs=micros();
  const bool drained=externalDisplay.waitComplete();
  MediaTelemetry::dmaWaitUs+=micros()-waitStartedUs;
  const bool batchClosed=externalDisplay.endBatch();
  if(batchClosed)finishTftTransfer();
  jpeg.close();
  return drained && batchClosed && decoded && !jpegDrawFailed && jpegTop == jpegBottom;
}

bool drawLosslessTile(const RxItem& it) {
  if (!losslessVideoRequested || !it.width || !it.height || it.height > JPEG_BAND_ROWS ||
      it.x + it.width > OUT_W || it.y + it.height > OUT_H) return false;
  auto& inflate = *new (&videoDecoder.inflate) tinfl_decompressor;
  tinfl_init(&inflate);
  externalDisplay.waitComplete();
  auto* output = reinterpret_cast<uint8_t*>(jpegBand);
  const size_t expected = size_t(it.width) * it.height * sizeof(uint16_t);
  size_t inSize = it.len, outSize = expected;
  const auto status = tinfl_decompress(&inflate, tileSlot[it.slot], &inSize,
      output, output, &outSize, TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  if (status != TINFL_STATUS_DONE || inSize != it.len || outSize != expected) return false;
  if(!externalDisplay.present(it.x, it.y, it.width, it.height,
                   reinterpret_cast<const lgfx::swap565_t*>(jpegBand)))return false;
  finishTftTransfer();
  return true;
}

// Returns false when the session is over.
bool serviceRx() {
  if (!rxRunning.load(std::memory_order_acquire)) {
    if (rxGoodbye >= 0) {
      byeReason = rxGoodbye;
      sessionExit = byeReason == 2 ? SessionExit::Replaced : (byeReason == 0 ? SessionExit::Peer : SessionExit::ServerClosed);
    } else sessionExit = rxError;
    connError = sessionExitText(sessionExit);
    return false;
  }
  RxItem it;
  bool inputServiced=false;
  uint32_t t0 = millis();
  while (millis() - t0 < 12 && xQueueReceive(rxQueue, &it, 0) == pdTRUE) {
    switch (it.type) {
      case 'T':
      case 'J': {
        const uint32_t drawStartedUs=micros();
        const bool drawn = it.type == 'T' ? drawTile(it) : drawLosslessTile(it);
        const uint32_t drawElapsedUs=micros()-drawStartedUs;
        MediaTelemetry::peak(MediaTelemetry::drawUs,drawElapsedUs);
        MediaTelemetry::drawSumUs+=drawElapsedUs;
        MediaTelemetry::tiles++;
        xQueueSend(freeSlots, &it.slot, 0);
        if (!drawn) {
          sessionExit = it.type == 'T' ? SessionExit::Jpeg : SessionExit::Lossless;
          connError = sessionExitText(sessionExit);
          return false;
        }
#if CSOS_CXL_DIAG_TRACE
        if(diagUiTiles!=UINT16_MAX)++diagUiTiles;
        diagUiLastDrawUs=micros();
#endif
        // Safe point: the tile DMA is finished and its slot returned. Do not
        // move touch sampling or frame ACKs ahead of their displayed geometry.
        if(!rxRunning.load(std::memory_order_acquire))return serviceRx();
        if(!uiDepth&&!inputServiced){serviceInput();inputServiced=true;if(hudPriorityPending)serviceHud();}
        if(exitRequested||sessionSetup!=LocalCommand::None)return true;
        break;
      }
      case 'F': {
        csosFrameSeen=true;
        csosDisplayedFrame=rd32(it.hdr);
        const bool directSupported=(it.hdr[10]&8)!=0;
#if CSOS_DIAGNOSTIC_TELEMETRY
        if(directSupported!=csosTouchSupported) CSOS_LOGF("[CSOS] direct_touch_supported=%d\n",directSupported);
#endif
        csosTouchSupported=directSupported;
        if(csosPanSupported!=bool(it.hdr[10]&16))hudDirty=true;
        csosPanSupported=(it.hdr[10]&16)!=0;
#if CSOS_CXL_DIAG_TRACE
        const bool diagAckQueued=sendMsg('A', it.hdr, 4);
        diagFrameTrace.recordFrame(rd32(it.hdr),it.diagFirstTileRxUs,
          it.diagLastTileRxUs,it.diagFrameRxUs,diagUiLastDrawUs,micros(),
          it.diagTileCount,diagUiTiles,diagAckQueued);
        diagUiTiles=0;diagUiLastDrawUs=0;
#else
        sendMsg('A', it.hdr, 4);  // ack only after every tile of the frame is on screen
#endif
        if (!audioHandshakeAsked && (it.hdr[10] & 0x40)) {
          audioHandshakeAsked = true;
          if (startAudio()) {
            const uint8_t version = AudioProfile::Version;
            sendMsg('a', &version, 1);
          } else {
            soundOn = false;
            CSOS_LOGLN("[AUDIO] task allocation failed");
          }
        }
        if (!losslessVideoRequested && (it.hdr[10] & LOSSLESS_VIDEO_CAPABILITY)) {
          const uint8_t version = LOSSLESS_VIDEO_VERSION;
          sendMsg('Y', &version, 1);
          losslessVideoRequested = true;
        }
        uint16_t ratio = rd16(it.hdr + 4);
        bool stretch = it.hdr[10] & 1, audio = it.hdr[10] & 2, cursor = !(it.hdr[10] & 4);
        if (ratio != viewRatioX100 || it.hdr[6] != zoomIdx || it.hdr[9] != monitorNo || stretch != stretchOn || audio != audioStreaming || cursor != cursorShown) hudDirty = true;
        stretchOn = stretch; audioStreaming = audio; cursorShown = cursor;
        viewRatioX100 = ratio; zoomIdx = it.hdr[6]; zoomCount = it.hdr[7]; monitorNo = it.hdr[9];
        if (it.hdr[8] != quality && it.hdr[8] < 3) { quality = it.hdr[8]; hudDirty = true; }
        ++statFrames;
        MediaTelemetry::frames++;
#if CSOS_FPS_PROBE
        fpsProbe.frame();
#endif
        break;
      }
    }
  }
  return true;
}

bool serviceUiBackground(){
 serviceHud();serviceHaptic();
 if(inSession){
  if(!serviceRx()){uiSessionLost=true;return false;}
#if CSOS_FPS_PROBE
  serviceFpsProbe();
#endif
  if(millis()-lastPingAt>2000){sendMsg('P',nullptr,0);lastPingAt=millis();}
 }else servicePreDesktopBackground();
 return true;
}
void serviceSessionSetup();
void retainSessionExit(SessionExit cause,uint32_t atMs) {
  const auto severity = sessionExitSeverity(cause);
  lastSessionFault.retain({atMs,uint16_t(cause),DiagnosticArea::Session,severity});
  if (severity != DiagnosticSeverity::Notice) sessionFaultNotice=true;
  char line[80];
  const int size=snprintf(line,sizeof(line),"[CSOS] session_exit code=%u severity=%u uptime_ms=%lu\n",
                          unsigned(cause),unsigned(severity),(unsigned long)atMs);
  if (CsosSerial && size>0 && size<int(sizeof(line)) && CsosSerial.availableForWrite()>=size)
    CsosSerial.write(reinterpret_cast<const uint8_t*>(line),size);
}
void runSession() {
  sessionExit=SessionExit::None;
  csosFrameSeen=false;
  losslessVideoRequested = false;
  csosTouchSupported=false; csosPanSupported=false; csosDisplayedFrame=0; csosDirectTouch.cancel();
  inSession = true; exitRequested = false; byeReason = 255;
  csosControls.mode=InputMode::Keys; audioStreaming = false;
  sessPrevMask = readKeyMask();  // keys still held from the menu are ignored
  resetSessionInput();
  csosControls.close();
  lastRxAt = lastPingAt = statT0 = millis();
  statFrames = 0; rxBytes = 0; fps = kbps = 0;
#if CSOS_CXL_DIAG_TRACE
  diagFrameTrace.reset();
  diagAudioTrace.reset();
  diagUiTiles=0;diagUiLastDrawUs=0;
#endif
#if CSOS_FPS_PROBE
  fpsProbe={};
#endif
  deviceStatus.streamSample=false;
  lastKeyLabel = "";
  audioHandshakeAsked = false;
  RxAudioProbe::probe.reset();

  xQueueReset(rxQueue);
  xQueueReset(freeSlots);
  for (uint8_t i = 0; i < TILE_SLOTS; ++i) xQueueSend(freeSlots, &i, 0);
  rxStop = false; rxRunning = true;
  if(xTaskCreatePinnedToCore(rxTask, "cxl_rx", 8192, nullptr, 5, nullptr, 0)!=pdPASS) {
    rxRunning=false; inSession=false; connError=sessionExitText(SessionExit::RxTask); conn.stop();
    retainSessionExit(SessionExit::RxTask,millis());return;
  }

  externalDisplay.clear(C_BG);
  sendQuality();
  sendFpsCap();
  sendSoundWanted();
#if CSOS_CXL_DIAG_TRACE
  const uint8_t diagProtocolVersion=1;
  sendMsg('D',&diagProtocolVersion,1); // One capability announcement per session.
#endif
  hudDirty = true;
  haptic(40);
  while (true) {
    traceUiBoundary("cycle");
    serviceWifiFaults();
    traceUiBoundary("rx_begin");
    if (!serviceRx()) break;
    traceUiBoundary("rx_end");
    traceUiBoundary("input_begin");
    serviceInput();
    traceUiBoundary("input_end");
    if(sessionSetup!=LocalCommand::None){serviceSessionSetup();if(uiSessionLost)break;}
    traceUiBoundary("touch_begin");
    serviceCsosTouch();
    traceUiBoundary("touch_end");
#if CSOS_FPS_PROBE
    serviceFpsProbe();
#endif
    if (exitRequested) { sessionExit=SessionExit::User;connError = ""; break; }
    uint32_t now = millis();
    if (now - lastPingAt > 2000) { sendMsg('P', nullptr, 0); lastPingAt = now; }
    if (now - statT0 >= 1000) {
      fps = statFrames * 1000.0f / (now - statT0);
      kbps = rxBytes / 1.024f / (now - statT0);
      deviceStatus.receivedKiBs=uint32_t(kbps+.5f);
      deviceStatus.streamSample=csosFrameSeen;
      statFrames = 0; rxBytes = 0; statT0 = now;
      hudDirty = true;
#if CSOS_DIAGNOSTIC_TELEMETRY
      CSOS_LOGF("[CSOS] session fps=%.1f heap=%u min=%u largest=%u touch_samples=%u touch_events=%u touch_errors=%u\n",
        fps,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)csosTouchSamples,(unsigned)csosTouchEvents,(unsigned)csosTouchErrors);
      CSOS_LOGF("[CSOS] input keyboard_changes=%u mode=%s i2c_sda=%d i2c_scl=%d irq=%d tca_pending=%u tx_queued=%u tx_sent=%u tca_overflow=%u\n",
        (unsigned)csosKeyboardChanges,csosControls.mode==InputMode::Mouse?"MOUSE":csosControls.mode==InputMode::View?"VISTA":"TASTI",(int)M5.In_I2C.getSDA(),(int)M5.In_I2C.getSCL(),digitalRead(11),
        (unsigned)(M5.In_I2C.readRegister8(0x34,0x03,100000)&0x0f),(unsigned)conn.queuedCount(),(unsigned)conn.sentCount(),
        (unsigned)((M5.In_I2C.readRegister8(0x34,0x02,100000)&0x08)!=0));
#endif
    }
    serviceHud();
    serviceHaptic();
    if (uxQueueMessagesWaiting(rxQueue) == 0) delay(1);
  }
  const uint32_t endedAt=millis();
#if CSOS_FPS_PROBE
  serviceFpsProbe("exit");
#endif
  rxStop = true;
  while (rxRunning.load(std::memory_order_acquire)) delay(1);
  stopAudio();
  conn.claimOwner();
  csosDirectTouch.cancel();
  releaseAll();
  conn.service(false);
  conn.stop();
  externalDark=false;applyBrightness();
  inSession = false;
#if CSOS_CXL_DIAG_TRACE
  dumpCxlDiagnostic();
#endif
  retainSessionExit(sessionExit,endedAt);
  hudDirty = true;
}

// ============================================================================
void beginPreDesktop();
bool servicePreDesktop();
void preDesktopSessionEnded();
void beginTouchCalibrationLab();
void serviceTouchCalibrationLab();
void beginTouchStudy();
void serviceTouchStudy();
void setup() {
#if CSOS_USB_CONSOLE
  CsosSerial.setTxBufferSize(2048);
  CsosSerial.begin(115200);
  CsosSerial.setTxTimeoutMs(1);
#endif
  CSOS_LOGF("[CSOS] wiring=v2 tft_bl=%d touch_irq=%d pwm_channel=5\n",TFT_BL,TOUCH_IRQ);
  pinMode(TFT_BL,OUTPUT);digitalWrite(TFT_BL,LOW);
  CSOS_LOGLN("[CSOS] operator_rework=v3 tabs=mouse,tasti,vista opt_hold_ms=1000");
  CSOS_LOGLN("[CSOS] hud_typography=Font4 proportional_keys=1 local_telemetry=1");
  CSOS_LOGF("[CSOS] boot reset_reason=%d\n",(int)esp_reset_reason());
  for (int cs : {TFT_CS, TOUCH_CS, SD_CS}) {
    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);
  }
  CSOS_LOGF("[CSOS] initial_sd_cs=%d (must be 1)\n",digitalRead(SD_CS));
  auto cfg = M5.config();
  cfg.internal_mic=false; cfg.internal_spk=false;
  M5Cardputer.begin(cfg, true);
  if(M5.getBoard()==m5::board_t::board_M5CardputerADV){
    detachInterrupt(digitalPinToInterrupt(11));
    M5Cardputer.Keyboard.begin(std::make_unique<CsosKeyboard::ReliableReader>());
    CSOS_LOGLN("[INPUT] keyboard_reader=fifo_poll interrupt_flag_dependency=0");
  }
  CSOS_LOGF("[CSOS] board=%d\n",(int)M5.getBoard());
#if CSOS_TOUCH_STUDY
  // Read existing brightness only; no migrations or writes in a research build.
  DeviceSettings studySettings;
  if(loadDeviceSettings(studySettings)==ESP_OK){brightness=studySettings.brightness;externalBrightness=studySettings.externalBrightness;}
#else
  loadSettings();
#endif
  nvs_stats_t nvsStats={};const auto nvsStatsError=nvs_get_stats(nullptr,&nvsStats);
  CSOS_LOGF("[CSOS] nvs_stats error=%s used=%u free=%u total=%u\n",esp_err_to_name(nvsStatsError),unsigned(nvsStats.used_entries),unsigned(nvsStats.free_entries),unsigned(nvsStats.total_entries));
  if(!CSOS_CALIBRATION_LAB&&!CSOS_TOUCH_STUDY)loadKnownWifi(wifiSsid,wifiPass);
  pinMode(BTNA_PIN, INPUT_PULLUP);
  pinMode(HAPTIC_PIN, OUTPUT);
  digitalWrite(HAPTIC_PIN, LOW);

  const bool panelReady=externalDisplay.initialize(
    [&](ExtPanel& panel) {
      initializeIli9341Rgbset(panel);
      panel.setRotation(rotation);
      panel.setColorDepth(24);
      panel.setTextWrap(false);
    },
    [&](XPT2046_Touchscreen&) { return initCsosTouch(); });
  if(!panelReady) {
    CSOS_LOGF("[CSOS] external_display_init_failed error=%u\n",
                  unsigned(externalDisplay.error()));
    while(true)delay(1000); // Fatal hardware init: no automatic reset or retry.
  }
#if CSOS_ILI9341_ID4_PROBE
  // Optional SPI controller observation only; never a boot-readiness gate.
  Ili9341Id4 observedId{};
  const bool inspected = externalDisplay.inspectIdentity(observedId);
  CSOS_LOGF("[CSOS][ID4] attempted=%u raw=%02X,%02X,%02X,%02X id=0x%04X signature=%u hardware_qualified=0\n",
                unsigned(inspected), unsigned(observedId.bytes[0]), unsigned(observedId.bytes[1]),
                unsigned(observedId.bytes[2]), unsigned(observedId.bytes[3]),
                unsigned(observedId.controllerId()), unsigned(observedId.matchesIli9341Signature()));
#endif
  CSOS_LOGF("[CSOS] panel=%dx%d expected=%dx%d rotation=%u\n",
      (int)tft.width(),(int)tft.height(),OUT_W,OUT_H,(unsigned)rotation);
  externalDisplay.clear(C_BG);
  applyBrightness();

  hud.setColorDepth(8);
  const bool hudAllocated=hud.createSprite(240, 135)!=nullptr;
  if(!hudAllocated)recordFault(FaultArea::Boot,FaultCode::BootHud,0,diagnosticTime());
#if CSOS_TOUCH_STUDY
  if(!hudAllocated){CSOS_LOGLN("[TOUCH_STUDY] HUD allocation failed");while(true)delay(1000);}
  beginTouchStudy();return;
#endif
#if CSOS_CALIBRATION_LAB
  {
    if(!hudAllocated){CSOS_LOGLN("[LAB] HUD allocation failed");while(true)delay(1000);}
    beginTouchCalibrationLab();return;
  }
#endif

  bool memOk = true;
  for (int i = 0; i < TILE_SLOTS; ++i) {
    tileSlot[i]=(uint8_t*)malloc(JPEG_MAX);
    if(!tileSlot[i]){memOk=false;recordFault(FaultArea::Boot,FaultCode::BootVideo,i,diagnosticTime());}
  }
  rxQueue = xQueueCreate(8, sizeof(RxItem));
  freeSlots = xQueueCreate(TILE_SLOTS, 1);
  if(!rxQueue||!freeSlots)
    recordFault(FaultArea::Boot,FaultCode::BootQueue,(!rxQueue?1:0)|(!freeSlots?2:0),diagnosticTime());
  memOk &= hudAllocated && rxQueue!=nullptr && freeSlots!=nullptr;

  for (int i = 0; i < KEY_COUNT; ++i) acts[i].kind = A_NONE;

  // The pre-desktop is the normal startup surface.
  if (!memOk) {
    const auto& fault=areaFaults[size_t(FaultArea::Boot)];
    uiMessage("AVVIO FALLITO",fault.event.code?faultText(FaultCode(fault.event.code)):"Memoria insufficiente",
              "Riavvia il Cardputer",C_ERR);
    while (true) delay(1000);
  }

  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED){
      pendingWifiDisconnect.store(info.wifi_sta_disconnected.reason);
#if CSOS_DIAGNOSTIC_TELEMETRY
      CSOS_LOGF("[CSOS] wifi_disconnected reason=%u\n",info.wifi_sta_disconnected.reason);
#endif
    }
    if(event==ARDUINO_EVENT_WIFI_STA_GOT_IP)
      CSOS_LOGF("[CSOS] wifi_connected ssid=%s channel=%d ip=%s reset_reason=%d\n",WiFi.SSID().c_str(),WiFi.channel(),WiFi.localIP().toString().c_str(),(int)esp_reset_reason());
  });
  beginPreDesktop();
}


// Local recovery uses the already-present editor; drafts are committed only
// after every required field has been accepted. Never log credential values.
bool confirmAccess(const char* title,const String& first,const String& second){
 LocalPageScope scope;uiMessage(title,first,second);uiPage.confirmation=true;uiPage.accent=UiPalette::Orange;uiPageCache.invalidate();
 while(true){
  uiDocumentPage();if(!serviceUiBackground())return false;const auto key=uiPoll();
  if(key.opt||key.base=='`')return false;
  if(key.base==';'||key.base=='.'||key.base==KEY_TAB){uiPage.position=(uiPage.position+1)%uiPage.total;continue;}
  if(key.idx>=0&&key.base==KEY_ENTER)return true;delay(5);
 }
}
void configureCloudAccess(){
  configureRequested=false;
  csosControls.close();
  String ssid=wifiSsid,password=wifiPass,user=bridgeUser,bridgePass=bridgePassword,pin=serverPin;
  const std::vector<String> items={"Rete Wi-Fi","Servizio remoto","Codice del desktop"};
  int selected=listMenu("ACCESSO REMOTO",items,"Frecce Invio | Esc/Opt: annulla",nullptr);
  bool accepted=false,changed=false,wifiChanged=false;
  if(selected==0){
    wifiSetup();uiPrevMask=readKeyMask();hudDirty=true;return;
  }else if(selected==1){
    accepted=textInput("SERVIZIO REMOTO","Nome utente",user,false,64)&&
      textInput("PASSWORD WSS","Password del ponte",bridgePass,true,128)&&user.length()&&bridgePass.length();
    changed=user!=bridgeUser||bridgePass!=bridgePassword;
    if(accepted&&changed)accepted=confirmAccess("SALVARE WSS?",user==bridgeUser?"Utente: invariato":"Utente: modificato",bridgePass==bridgePassword?"Password: invariata":"Password: modificata");
  }else if(selected==2){
    accepted=textInput("CODICE DESKTOP","Codice di accesso CXL",pin,true,64)&&pin.length();changed=pin!=serverPin;
    if(accepted&&changed)accepted=confirmAccess("SALVARE IL CODICE?","Aggiorna il codice di accesso CXL","Valido dal prossimo collegamento");
  }
  if(accepted&&changed){
    const String oldSsid=wifiSsid,oldPassword=wifiPass,oldUser=bridgeUser,oldBridge=bridgePassword,oldPin=serverPin;
    const bool oldOverride=profileOverride;
    wifiSsid=ssid;wifiPass=password;bridgeUser=user;bridgePassword=bridgePass;serverPin=pin;profileOverride=true;
    if(saveSettings()){
      csosSaveFailed=false;
      if(wifiChanged)WiFi.disconnect();connectionPaused=false;csosCxlRejected=false;
      lastTransportFailure=TransportFailure::None;
      uiMessage("Accesso salvato",inSession?"Usato al prossimo collegamento":"Pronto al collegamento");waitAnyKey(1800);
    }else{
      csosSaveFailed=true;
      wifiSsid=oldSsid;wifiPass=oldPassword;bridgeUser=oldUser;bridgePassword=oldBridge;serverPin=oldPin;profileOverride=oldOverride;
      connectionPaused=!inSession;uiMessage("Salvataggio fallito","Credenziali precedenti conservate","Riprova il salvataggio",C_ERR);waitAnyKey(1800);
    }
  }else setStatus("Nessuna modifica","Profilo precedente conservato");
  uiPrevMask=readKeyMask();hudDirty=true;
}

bool serviceOfflineInput() {
  const uint64_t cur=readKeyMask(),down=cur & ~uiPrevMask;
  uiPrevMask=cur;
  if(down & kbit(IDX_FN)) { csosControls.press(true,false,0); if(csosControls.fn)haptic(20); hudDirty=true; }
  if(down & kbit(IDX_SHIFT)){csosControls.aa=!csosControls.aa;hudDirty=true;}
  if(down & kbit(IDX_OPT)) { csosControls.press(false,true,0); if(csosControls.menu)csosControls.page=2; hudDirty=true; }
  bool retry=false;
  for(int i=0;i<KEY_COUNT;i++) {
    if(!(down & kbit(i)) || isModifierIdx(i)) continue;
    const uint8_t c=baseAt(i);
    if(csosControls.local()) executeLocalCommand(csosControls.press(false,false,c==KEY_ENTER?13:c==KEY_TAB?9:c));
    else if(c==KEY_ENTER) { connectionPaused=false; retry=true; }
    else if(c=='1') { csosControls.details=true; }
    else if(c=='2') { configureRequested=true; }
    hudDirty=true;
  }
  serviceHud(); serviceHaptic();
  return retry;
}

#include "PreDesktop.h"
void serviceFaultTraces(){
 if(inSession||!CsosSerial)return;
 for(size_t i=0;i<size_t(FaultArea::Count);++i){
  const uint8_t mask=uint8_t(1u<<i);if(!(pendingFaultTraceMask&mask))continue;
  const auto& trace=pendingFaultTraces[i];
  char line[100];
  const int size=snprintf(line,sizeof(line),"[CSOS] fault area=%u code=%u severity=%u detail=%ld uptime_ms=%lu\n",
   unsigned(i),unsigned(trace.event.code),unsigned(trace.event.severity),long(trace.detail),(unsigned long)trace.event.atMs);
  if(size>0&&size<int(sizeof(line))&&CsosSerial.availableForWrite()>=size){
   if(CsosSerial.write(reinterpret_cast<const uint8_t*>(line),size)==size_t(size))
    pendingFaultTraceMask&=uint8_t(~mask);
  }
  return;
 }
}
void serviceWifiFaults(){
 const int reason=pendingWifiDisconnect.exchange(-1);
 if(reason>=0&&!wifiAutoCancelled&&wifiChange==WifiChangePhase::Idle&&wifiSsid.length())
  recordFault(FaultArea::Wifi,pre.wifi||inSession?FaultCode::WifiLost:FaultCode::WifiAssociation,
              reason,millis(),DiagnosticSeverity::Warning);
}
bool reportConnectionProgress(ConnectionPhase phase){return connectionAttempt.update(phase);}
ConnResult connectDesktopOnWorker(){
 conn.setConnectProgress(reportConnectionProgress);
 const auto result=remoteConnect();
 conn.setConnectProgress(nullptr);
 if(connectionAttempt.cancelled()){
  conn.stop();lastTransportFailure=TransportFailure::None;csosCxlRejected=false;
  connError="Collegamento annullato";return CONN_CANCELLED;
 }
 return result;
}
ConnResult connectDesktopAsync(){
 connectionAttempt.begin();
 if(!controlWorker||jobActive){connError="Controlli cloud occupati o non disponibili";return CONN_FAIL;}
 setDesktopConnector(connectDesktopOnWorker);
 CloudJob job;job.kind=CloudJobKind::Connect;job.generation=pre.generation;
 if(!submitCloudJob(job)){connError="Collegamento non avviato";return CONN_FAIL;}
 invalidatePreDesktopView();
 uint32_t paintedAt=0;const uint32_t started=millis();
 for(;;){
  const auto key=uiPoll();
  if(key.opt||key.base=='`')connectionAttempt.cancel();
  int x=0,y=0;
  if(localPanelTap(x,y)&&preDesktopHit(x,y)==1)connectionAttempt.cancel();
  CloudResult result;
  if(pollCloudResult(result)&&result.kind==CloudJobKind::Connect){
   conn.claimOwner();
   if(connectionAttempt.cancelled()){
    conn.stop();lastTransportFailure=TransportFailure::None;csosCxlRejected=false;
    connError="Collegamento annullato";return CONN_CANCELLED;
   }
   if(result.connection!=CONN_OK&&!connError.length())connError=result.detail;
   return result.connection;
  }
  const uint32_t now=millis();
  if(!paintedAt||uint32_t(now-paintedAt)>=100){
   PreDesktopScreen screen;screen.wifi=wifiSsid;screen.github=githubCaption();
   screen.cxl=connectionPhaseText(connectionAttempt.phase());
   screen.note=connectionAttempt.cancelled()?String("Attendo chiusura del collegamento"):
    String("Attesa ")+String((now-started)/1000)+" s; Esc per annullare";
   screen.connecting=true;screen.busy=true;screen.selected=1;
   screen.cancelling=connectionAttempt.cancelled();preDesktopPrimaryAction()=1;
   drawPreDesktop(tft,screen,now);paintedAt=now;
  }
  serviceHud();serviceHaptic();delay(3);
 }
}
void serviceSessionSetup(){
 const auto command=sessionSetup;sessionSetup=LocalCommand::None;
 cancelInputActions();uiSessionLost=false;
 if(command==LocalCommand::Configure)configureCloudAccess();
 else if(command==LocalCommand::Wifi)wifiSetup();
 else if(command==LocalCommand::Management)configureManagement();
 cancelInputActions();sessPrevMask=readKeyMask();hudDirty=true;
}
#if CSOS_CALIBRATION_LAB
#include "TouchCalibrationLab.h"
#else
void beginTouchCalibrationLab(){}
void serviceTouchCalibrationLab(){}
#endif
#if CSOS_TOUCH_STUDY
#include "TouchStudy.h"
#endif
void loop() {
  serviceWifiFaults();
#if CSOS_TOUCH_STUDY
  serviceTouchStudy();return;
#endif
#if CSOS_CALIBRATION_LAB
  serviceTouchCalibrationLab();return;
#endif
  if(!servicePreDesktop())return;
  csosControls.close();connectionPaused=false;
  // Entra keeps the pre-desktop visible until the streaming session starts.
  setStatus("Desktop remoto","Connessione WSS..."); serviceHud(true);
  ConnResult result=connectDesktopAsync();
  if(result!=CONN_OK&&result!=CONN_CANCELLED)
    recordFault(FaultArea::Cloud,FaultCode::CloudAccess,int(lastTransportFailure),millis());
  if(result==CONN_OK) runSession();
  else if(result==CONN_BADPIN) connectionPaused=true;
  if(transportNeedsManualRetry(lastTransportFailure)) connectionPaused=true;
  if(connectionPaused && !connError.length()) setStatus("Disconnesso","Invio per collegare");
  else setStatus("Non connesso",connError);

  csosFrameSeen=false;
  uiPrevMask=readKeyMask();
  preDesktopSessionEnded();
  serviceHud(true);
}
