/* ESP32S board - 8048S043C -  4.3-inch TFT 800x480 - Capacitive touch - 8M PSRAM 16M Flash
pio pkg update 
pio run -t clean 
pio run 
pio run --upload-port  /dev/ttyUSB0 -t upload
pio run --monitor-port /dev/ttyUSB0 -t monitor
https://formatter.org/cpp-formatter

// TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO TODO 

Use StaticJsonDocument with a fixed size (e.g., StaticJsonDocument<512> doc;) instead of dynamic JsonDocument to prevent heap fragmentation on ESP32. Avoid large stack buffers (e.g., char buffer[128] in update_label is fine, but profile with ESP.getFreeHeap() logs). Free resources explicitly (e.g., call http.end() in all paths, even on success).

Add wind direction (from METAR "wdir") and gusts if available. Fetch additional data like visibility, cloud cover, or flight category (VFR/IFR) from the API. Integrate forecasts: Use NOAA's API for TAF (Terminal Aerodrome Forecast) alongside METAR.

Multi-Airport Support: Allow multiple METAR IDs in settings (e.g., via a dropdown), cycle through them.

OTA Updates: Integrate ArduinoOTA for wireless firmware updates.

Internationalization: Support non-English airports by handling UTF-8 in labels (LVGL supports it).
*/

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp32_smartdisplay.h>
#include <lvgl.h>
#include <math.h>
#include <strings.h>
#include <cmath>

struct  {
  lv_obj_t *mainScreen;
  lv_obj_t *mainPanel;
  lv_obj_t *temperatureLabel;
  lv_obj_t *humidityLabel;
  lv_obj_t *windSpeedLabel;
  lv_obj_t *pressureLabel;
  lv_obj_t *timeDateLabel;
  lv_obj_t *wifiStatusLabel;
  lv_obj_t *airportNameLabel;
  lv_obj_t *dataAgeLabel;
  lv_obj_t *sunriseLabel;
  lv_obj_t *sunsetLabel;
  lv_obj_t *bigTimeLabel;
  lv_obj_t *bigDateLabel;
  lv_obj_t *settingScreen;
  lv_obj_t *ssidTextArea;
  lv_obj_t *passwordTextArea;
  lv_obj_t *metarIdTextArea;
  lv_obj_t *timeOffsetTextArea;
  lv_obj_t *keyboard;
} uiElements;

struct  {
  char ssid[64] = "";
  char password[64] = "";
  char metarId[10] = "";
  long timeOffset = 0;
} config;

// NTP and time client
WiFiUDP ntpUdp;
NTPClient timeClient(ntpUdp);

enum WifiState { DISCONNECTED, CONNECTING, CONNECTED, RECONNECTING };

struct {
  WifiState state = DISCONNECTED;
  unsigned long lastConnectAttempt = 0;
  unsigned long connectStartTime = 0;
} wifiManagement;

struct {
  String sunrise;
  String sunset;
  char airportName[100] = "";
  float lat = 0;
  float lon = 0;
  unsigned long obsTime = 0;
  unsigned long epochTime = 0;
  unsigned long timeOfLastUpdate = 0;
  long localTimeOffset = 0;
  int dataAgeMin = 0;
  int temperature = 0;
  int dewPoint = 0;
  int windSpeedKnots = 0;
  int pressure = 0;
  int relativeHumidity = 0;
  int windSpeedKmh = 0;
  bool weatherIsValid = false;
  bool utcOffsetIsValid = false;
} weather;

// Normalize string by replacing accented characters
String normalizeString(String str) {
  static const char *replacements[][2] = {
      {"á", "a"}, {"à", "a"}, {"â", "a"},  {"ã", "a"},  {"ä", "ae"}, {"æ", "ae"}, {"Á", "A"},  {"À", "A"},  {"Â", "A"},  {"Ã", "A"},  {"Ä", "Ae"}, {"Æ", "AE"},
      {"é", "e"}, {"è", "e"}, {"ê", "e"},  {"ë", "e"},  {"É", "E"},  {"È", "E"},  {"Ê", "E"},  {"Ë", "E"},  {"í", "i"},  {"ì", "i"},  {"î", "i"},  {"ï", "i"},
      {"Í", "I"}, {"Ì", "I"}, {"Î", "I"},  {"Ï", "I"},  {"ó", "o"},  {"ò", "o"},  {"ô", "o"},  {"õ", "o"},  {"ö", "oe"}, {"œ", "oe"}, {"Ó", "O"},  {"Ò", "O"},
      {"Ô", "O"}, {"Õ", "O"}, {"Ö", "Oe"}, {"Œ", "OE"}, {"ú", "u"},  {"ù", "u"},  {"û", "u"},  {"ü", "ue"}, {"Ú", "U"},  {"Ù", "U"},  {"Û", "U"},  {"Ü", "Ue"},
      {"ñ", "n"}, {"Ñ", "N"}, {"ç", "c"},  {"Ç", "C"},  {"ÿ", "y"},  {"Ÿ", "Y"},  {"ß", "ss"}, {"ẞ", "SS"}};
  String result = str;
  for (auto &pair : replacements) result.replace(pair[0], pair[1]);
  return result;
}

// Update label text with formatted string
void updateLabel(lv_obj_t *label, const char *format, ...) {
  char buffer[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  lv_label_set_text(label, buffer);
}

// Save configuration settings
void saveConfigurations() {
  Preferences preferences;
  preferences.begin("config", false);
  preferences.putString("ssid", config.ssid);
  preferences.putString("password", config.password);
  preferences.putString("metar_id", config.metarId);
  preferences.putInt("time_offset", config.timeOffset);
  preferences.end();
  log_i("Saved configurations: SSID: \"%s\", Password: \"%s\", METAR ID: \"%s\", Time Offset: \"%i\"", config.ssid, config.password, config.metarId,
        config.timeOffset);
}

// Load configuration settings
void loadConfigurations() {
  Preferences preferences;
  preferences.begin("config", true);
  preferences.getString("ssid", config.ssid, sizeof(config.ssid));
  preferences.getString("password", config.password, sizeof(config.password));
  preferences.getString("metar_id", config.metarId, sizeof(config.metarId));
  config.timeOffset = preferences.getInt("time_offset");
  preferences.end();
  log_i("Loaded configurations: SSID: \"%s\", Password: \"%s\", METAR ID: \"%s\", Time Offset: \"%i\"", config.ssid, config.password, config.metarId,
        config.timeOffset);
}

// Event handler for settings button
void settingsButtonEvent(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) lv_disp_load_scr(uiElements.settingScreen);
}

void backButtonEvent(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    String tempSsid = lv_textarea_get_text(uiElements.ssidTextArea);
    tempSsid.trim();
    strlcpy(config.ssid, tempSsid.c_str(), sizeof(config.ssid));
    strlcpy(config.password, lv_textarea_get_text(uiElements.passwordTextArea) ? lv_textarea_get_text(uiElements.passwordTextArea) : "", sizeof(config.password));
    String rawMetar = lv_textarea_get_text(uiElements.metarIdTextArea);
    rawMetar.trim();
    rawMetar.toUpperCase();
    strlcpy(config.metarId, rawMetar.substring(0, 4).c_str(), sizeof(config.metarId));
    config.timeOffset = lv_textarea_get_text(uiElements.timeOffsetTextArea) ? strtol(lv_textarea_get_text(uiElements.timeOffsetTextArea), nullptr, 10) : 0;
    saveConfigurations();
    timeClient.setTimeOffset(config.timeOffset);
    lv_disp_load_scr(uiElements.mainScreen);
  }
}

void wifiManagementCallback(lv_timer_t *timer) {
  lv_obj_t *currentScreen = lv_disp_get_scr_act(NULL);
  if (currentScreen == uiElements.mainScreen) {
    switch (wifiManagement.state) {
      case DISCONNECTED:
        if (millis() - wifiManagement.lastConnectAttempt >= 10000) {
          log_i("Starting WiFi connection to %s", config.ssid);
          WiFi.begin(config.ssid, config.password);
          wifiManagement.connectStartTime = millis();
          wifiManagement.lastConnectAttempt = millis();
          wifiManagement.state = CONNECTING;
          updateLabel(uiElements.wifiStatusLabel, LV_SYMBOL_WIFI " Connecting...");
        } else
          updateLabel(uiElements.wifiStatusLabel, LV_SYMBOL_CLOSE " Disconnected");
        break;
      case CONNECTING:
        if (WiFi.status() == WL_CONNECTED) {
          wifiManagement.state = CONNECTED;
          log_i("WiFi: Connected to %s with %d dBm", WiFi.SSID().c_str(), WiFi.RSSI());
        } else if (millis() - wifiManagement.connectStartTime >= 10000) {
          log_i("WiFi connection timeout");
          WiFi.disconnect();
          wifiManagement.state = DISCONNECTED;
          updateLabel(uiElements.wifiStatusLabel, LV_SYMBOL_CLOSE " Connection Failed");
        } else
          updateLabel(uiElements.wifiStatusLabel, LV_SYMBOL_WIFI " Connecting...");
        break;
      case CONNECTED:
        if (WiFi.status() != WL_CONNECTED) {
          wifiManagement.state = RECONNECTING;
          log_i("WiFi connection lost");
          updateLabel(uiElements.wifiStatusLabel, LV_SYMBOL_CLOSE " Connection Lost");
        } else
          updateLabel(uiElements.wifiStatusLabel, LV_SYMBOL_WIFI " %s", WiFi.SSID().c_str());
        break;
      case RECONNECTING:
        if (millis() - wifiManagement.lastConnectAttempt >= 10000) {
          log_i("Attempting to reconnect WiFi");
          WiFi.reconnect();
          wifiManagement.connectStartTime = millis();
          wifiManagement.lastConnectAttempt = millis();
          wifiManagement.state = CONNECTING;
          updateLabel(uiElements.wifiStatusLabel, LV_SYMBOL_WIFI " Reconnecting...");
        } else
          updateLabel(uiElements.wifiStatusLabel, LV_SYMBOL_CLOSE " Connection Lost");
        break;
    }
  } else if (currentScreen == uiElements.settingScreen) {
    if (WiFi.status() == WL_CONNECTED) {
      log_i("Disconnecting WiFi (settings screen active)");
      WiFi.disconnect();
    }
    wifiManagement.state = DISCONNECTED;
    updateLabel(uiElements.wifiStatusLabel, LV_SYMBOL_CLOSE " Disconnected");
  }
}

// Show keyboard for text area input
void showKeyboard(lv_event_t *e, lv_obj_t *textArea) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_keyboard_set_textarea(uiElements.keyboard, textArea);
    lv_obj_clear_flag(uiElements.keyboard, LV_OBJ_FLAG_HIDDEN);
  }
}

// Event handlers for text areas
void ssidTextAreaEvent(lv_event_t *e) { showKeyboard(e, uiElements.ssidTextArea); }
void passwordTextAreaEvent(lv_event_t *e) { showKeyboard(e, uiElements.passwordTextArea); }
void metarIdTextAreaEvent(lv_event_t *e) { showKeyboard(e, uiElements.metarIdTextArea); }
void timeOffsetTextAreaEvent(lv_event_t *e) { showKeyboard(e, uiElements.timeOffsetTextArea); }

// Handle keyboard events
void keyboardEvent(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_READY || lv_event_get_code(e) == LV_EVENT_CANCEL) lv_obj_add_flag(uiElements.keyboard, LV_OBJ_FLAG_HIDDEN);
}

// Create a styled card panel
lv_obj_t *createCard(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, w, h);
  lv_obj_set_pos(card, x, y);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // Card styling
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x2a2a2a), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(0x444444), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(card, 8, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(card, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(card, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_x(card, 2, LV_PART_MAIN);
  lv_obj_set_style_shadow_offset_y(card, 2, LV_PART_MAIN);

  return card;
}

// Create a styled label with icon
lv_obj_t *createStyledLabel(lv_obj_t *parent, int x, int y, const char *text, const char *icon = nullptr) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_pos(label, x, y);
  char fullText[200];
  if (icon)
    snprintf(fullText, sizeof(fullText), "%s %s", icon, text);
  else
    strlcpy(fullText, text, sizeof(fullText));
  lv_label_set_text(label, fullText);
  lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
  return label;
}

// Create a gradient button with icon
lv_obj_t *createGradientButton(lv_obj_t *parent, int x, int y, int w, int h, const char *text, const char *icon, lv_event_cb_t eventCb) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_pos(btn, x, y);

  // Button styling
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x3366ff), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x1a4dff), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
  lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(btn, lv_color_hex(0x4d79ff), LV_PART_MAIN);
  lv_obj_set_style_shadow_width(btn, 4, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(btn, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(btn, LV_OPA_30, LV_PART_MAIN);

  // Pressed state
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x1a4dff), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x0d33cc), LV_PART_MAIN | LV_STATE_PRESSED);

  // Label with icon
  lv_obj_t *lbl = lv_label_create(btn);
  lv_obj_center(lbl);

  char fullText[64];
  if (icon)
    snprintf(fullText, sizeof(fullText), "%s %s", icon, text);
  else
    strlcpy(fullText, text, sizeof(fullText));

  lv_label_set_text(lbl, fullText);
  lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, LV_FONT_DEFAULT, LV_PART_MAIN);

  if (eventCb) lv_obj_add_event_cb(btn, eventCb, LV_EVENT_ALL, NULL);

  return btn;
}

// Create a text area with modern styling
lv_obj_t *createModernTextArea(lv_obj_t *parent, int x, int y, int w, int h, bool oneLine, bool passwordMode, lv_event_cb_t eventCb,
                                 const char *initialText, const char *placeholder) {
  lv_obj_t *ta = lv_textarea_create(parent);
  lv_obj_set_size(ta, w, h);
  lv_obj_set_pos(ta, x, y);
  lv_textarea_set_one_line(ta, oneLine);
  lv_textarea_set_password_mode(ta, passwordMode);
  if (placeholder) lv_textarea_set_placeholder_text(ta, placeholder);

  // Modern styling
  lv_obj_set_style_bg_color(ta, lv_color_hex(0x2a2a2a), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(ta, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(ta, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_text_color(ta, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_radius(ta, 6, LV_PART_MAIN);
  lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(ta, lv_color_hex(0x444444), LV_PART_MAIN);
  lv_obj_set_style_pad_all(ta, 8, LV_PART_MAIN);

  // Focus styling
  lv_obj_set_style_border_color(ta, lv_color_hex(0x3366ff), LV_PART_MAIN | LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(ta, 2, LV_PART_MAIN | LV_STATE_FOCUSED);

  // Cursor styling
  lv_obj_set_style_bg_color(ta, lv_color_white(), LV_PART_CURSOR);

  if (eventCb) lv_obj_add_event_cb(ta, eventCb, LV_EVENT_ALL, NULL);
  lv_textarea_set_text(ta, initialText);

  return ta;
}

// Initialize main screen with improved layout and better spacing
void mainScreenInit(void) {
  uiElements.mainScreen = lv_obj_create(NULL);
  lv_obj_remove_flag(uiElements.mainScreen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(uiElements.mainScreen, lv_color_hex(0x0d1117), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(uiElements.mainScreen, lv_color_hex(0x161b22), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(uiElements.mainScreen, LV_GRAD_DIR_VER, LV_PART_MAIN);
  // Header card
  lv_obj_t *headerCard = createCard(uiElements.mainScreen, 5, 5, 790, 80);
  uiElements.wifiStatusLabel = createStyledLabel(headerCard, 0, -5, "Disconnected", LV_SYMBOL_CLOSE);
  uiElements.timeDateLabel = createStyledLabel(headerCard, 0, 25, "Time: --", LV_SYMBOL_LIST);
  createGradientButton(headerCard, 650, 0, 110, 40, "Settings", LV_SYMBOL_SETTINGS, settingsButtonEvent);
  // Airport info card
  lv_obj_t *airportCard = createCard(uiElements.mainScreen, 5, 90, 790, 45);
  uiElements.airportNameLabel = createStyledLabel(airportCard, 0, -5, "Airport: --", LV_SYMBOL_HOME);
  uiElements.dataAgeLabel = createStyledLabel(airportCard, 400, -5, "Data Age: -- min", LV_SYMBOL_REFRESH);
  // Weather data card - left side
  lv_obj_t *weatherCard = createCard(uiElements.mainScreen, 5, 140, 390, 185);
  lv_obj_t *weatherTitle = createStyledLabel(weatherCard, 0, -5, "Weather Data", LV_SYMBOL_EYE_OPEN);
  lv_obj_set_style_text_color(weatherTitle, lv_color_hex(0x3366ff), LV_PART_MAIN);
  uiElements.temperatureLabel = createStyledLabel(weatherCard, 0, 25, "Temperature: -- °C", LV_SYMBOL_BATTERY_3);
  uiElements.humidityLabel = createStyledLabel(weatherCard, 0, 55, "Humidity: -- %", LV_SYMBOL_TINT);
  uiElements.windSpeedLabel = createStyledLabel(weatherCard, 0, 85, "Wind Speed: -- km/h", LV_SYMBOL_GPS);
  uiElements.pressureLabel = createStyledLabel(weatherCard, 0, 115, "Pressure: -- hPa", LV_SYMBOL_POWER);
  // Sun times card - right side
  lv_obj_t *sunCard = createCard(uiElements.mainScreen, 405, 140, 390, 185);
  lv_obj_t *sunTitle = createStyledLabel(sunCard, 0, -5, "Sun Times", LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(sunTitle, lv_color_hex(0xffa500), LV_PART_MAIN);
  uiElements.sunriseLabel = createStyledLabel(sunCard, 0, 25, "Sunrise: --", LV_SYMBOL_UP);
  uiElements.sunsetLabel = createStyledLabel(sunCard, 0, 55, "Sunset: --", LV_SYMBOL_DOWN);
  lv_obj_t *infoLabel1 = createStyledLabel(sunCard, 0, 85, "Daylight Info", nullptr);
  lv_obj_set_style_text_color(infoLabel1, lv_color_hex(0x888888), LV_PART_MAIN);
  // Big time date card
  lv_obj_t *bigTimeDateCard = createCard(uiElements.mainScreen, 5, 330, 790, 95);
  uiElements.bigTimeLabel = lv_label_create(bigTimeDateCard);
  lv_obj_align(uiElements.bigTimeLabel, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_text_color(uiElements.bigTimeLabel, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(uiElements.bigTimeLabel, &lv_font_montserrat_48, LV_PART_MAIN);
  lv_label_set_text(uiElements.bigTimeLabel, "--:--:--");
  uiElements.bigDateLabel = lv_label_create(bigTimeDateCard);
  lv_obj_align(uiElements.bigDateLabel, LV_ALIGN_TOP_LEFT, 250, 0);
  lv_obj_set_style_text_color(uiElements.bigDateLabel, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(uiElements.bigDateLabel, &lv_font_montserrat_48, LV_PART_MAIN);
  lv_label_set_text(uiElements.bigDateLabel, "--.--.----");
  // Status card
  lv_obj_t *statusCard = createCard(uiElements.mainScreen, 5, 430, 790, 45);
  lv_obj_t *statusLabel = createStyledLabel(statusCard, 0, -5, "ESP32 METAR Weather Station", LV_SYMBOL_EYE_OPEN);
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x3366ff), LV_PART_MAIN);
  lv_obj_t *versionLabel = createStyledLabel(statusCard, 600, -5, "v1.0", nullptr);
  lv_obj_set_style_text_color(versionLabel, lv_color_hex(0x888888), LV_PART_MAIN);
}

// Initialize settings screen with modern design and better spacing
void settingScreenInit(void) {
  uiElements.settingScreen = lv_obj_create(NULL);
  lv_obj_remove_flag(uiElements.settingScreen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(uiElements.settingScreen, lv_color_hex(0x0d1117), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(uiElements.settingScreen, lv_color_hex(0x161b22), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(uiElements.settingScreen, LV_GRAD_DIR_VER, LV_PART_MAIN);
  // Setting Header card
  lv_obj_t *headerCard = createCard(uiElements.settingScreen, 5, 5, 790, 70);
  lv_obj_t *title = createStyledLabel(headerCard, 0, 5, "Weather Station Settings", LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_color(title, lv_color_hex(0x3366ff), LV_PART_MAIN);
  createGradientButton(headerCard, 650, -5, 110, 40, "Save", LV_SYMBOL_SAVE, backButtonEvent);
  // Settings card
  lv_obj_t *formCard = createCard(uiElements.settingScreen, 5, 80, 790, 345);
  lv_obj_t *ssidLabel = createStyledLabel(formCard, 0, -5, "WiFi Network (SSID):");
  lv_obj_set_style_text_color(ssidLabel, lv_color_hex(0xcccccc), LV_PART_MAIN);
  uiElements.ssidTextArea = createModernTextArea(formCard, 0, 20, 300, 35, true, false, ssidTextAreaEvent, config.ssid, "Enter WiFi SSID");
  lv_obj_t *pwdLabel = createStyledLabel(formCard, 360, -5, "WiFi Password:");
  lv_obj_set_style_text_color(pwdLabel, lv_color_hex(0xcccccc), LV_PART_MAIN);
  uiElements.passwordTextArea = createModernTextArea(formCard, 360, 20, 300, 35, true, true, passwordTextAreaEvent, config.password, "Enter password");
  lv_obj_t *metarLabel = createStyledLabel(formCard, 0, 75, "METAR ID (4 letters):");
  lv_obj_set_style_text_color(metarLabel, lv_color_hex(0xcccccc), LV_PART_MAIN);
  uiElements.metarIdTextArea = createModernTextArea(formCard, 0, 100, 300, 35, true, false, metarIdTextAreaEvent, config.metarId, "e.g. KJFK");
  lv_obj_t *offsetLabel = createStyledLabel(formCard, 360, 75, "Time Offset (seconds):");
  lv_obj_set_style_text_color(offsetLabel, lv_color_hex(0xcccccc), LV_PART_MAIN);
  uiElements.timeOffsetTextArea = createModernTextArea(formCard, 360, 100, 300, 35, true, false, timeOffsetTextAreaEvent, String(config.timeOffset).c_str(), "UTC offset");
  // Help card
  lv_obj_t *helpCard = createCard(uiElements.settingScreen, 5, 430, 790, 45);
  lv_obj_t *helpTitle = createStyledLabel(helpCard, 0, -5, "Help:", nullptr);
  lv_obj_set_style_text_color(helpTitle, lv_color_hex(0x3366ff), LV_PART_MAIN);
  lv_obj_t *helpText = createStyledLabel(helpCard, 50, -5, "METAR ID: Find airport codes at wikipedia.org/wiki/ICAO_airport_code", nullptr);
  lv_obj_set_style_text_color(helpText, lv_color_hex(0x888888), LV_PART_MAIN);
  // Keyboard - positioned better
  uiElements.keyboard = lv_keyboard_create(uiElements.settingScreen);
  lv_keyboard_set_textarea(uiElements.keyboard, uiElements.ssidTextArea);
  lv_obj_add_flag(uiElements.keyboard, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(uiElements.keyboard, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(uiElements.keyboard, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
  lv_obj_add_event_cb(uiElements.keyboard, keyboardEvent, LV_EVENT_ALL, NULL);
  // Position keyboard at bottom when visible
  lv_obj_set_size(uiElements.keyboard, 770, 150);
  lv_obj_align(uiElements.keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// Initialize UI with modern theme
void uiInit(void) {
  lv_disp_t *display = lv_display_get_default();
  lv_theme_t *theme = lv_theme_default_init(display, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
  lv_disp_set_theme(display, theme);

  mainScreenInit();
  settingScreenInit();
  lv_disp_load_scr(uiElements.mainScreen);
}

bool fetchWeatherData() {
  HTTPClient http;
  http.setTimeout(10000);
  WiFiClientSecure client;
  client.setInsecure();
  char urlBuffer[100];
  if (WiFi.status() != WL_CONNECTED) return false;
  if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) return false;
  snprintf(urlBuffer, sizeof(urlBuffer), "https://aviationweather.gov/api/data/metar?ids=%s&format=json", config.metarId);
  log_i("Fetching METAR from: %s", urlBuffer);
  if (!http.begin(client, urlBuffer)) {
    log_i("Failed to initialize HTTPS connection");
    http.end();
    return false;
  }
  http.setReuse(false);
  int httpCode = http.GET();
  if (httpCode != 200) {
    String errorResponse = http.getString();
    log_i("HTTP request failed with code: %d, Response: %s", httpCode, errorResponse.substring(0, 200).c_str());
    http.end();
    return false;
  }
  String responseStr = http.getString();
  if (responseStr.length() == 0) {
    log_i("Empty response received");
    http.end();
    return false;
  }
  http.end();
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, responseStr);
  if (error) {
    log_i("JSON parsing failed: %s", error.c_str());
    return false;
  }
  if (!doc.is<JsonArray>() || doc.as<JsonArray>().size() == 0) {
    log_i("Invalid METAR JSON structure: not an array or empty");
    return false;
  }
  JsonObject obj = doc[0].as<JsonObject>();
  if (obj["icaoId"].isNull() || obj["temp"].isNull()) {
    log_i("Invalid METAR JSON structure: missing icaoId or temp");
    return false;
  }
  const char *newId = obj["icaoId"];
  if (strcmp(newId, config.metarId) != 0) {
    log_i("METAR ID mismatch: %s vs %s", newId, config.metarId);
    return false;
  }
  weather.temperature = obj["temp"] | 0;
  weather.dewPoint = obj["dewp"] | 0;
  weather.windSpeedKnots = obj["wspd"] | 0;
  weather.pressure = obj["altim"] | 0;
  weather.obsTime = obj["obsTime"] | 0;
  weather.lat = obj["lat"].as<float>();
  weather.lon = obj["lon"].as<float>();
  if (weather.lat == 0 && weather.lon == 0) {
    log_i("Invalid lat and lon position");
    return false;
  }
  weather.windSpeedKmh = weather.windSpeedKnots * 1.852;
  float t = weather.temperature, d = weather.dewPoint;
  weather.relativeHumidity = 100 * exp((17.625 * d) / (243.04 + d)) / exp((17.625 * t) / (243.04 + t));
  const char *name = obj["name"] | "Unknown";
  String normalized = normalizeString(name);
  strncpy(weather.airportName, normalized.c_str(), sizeof(weather.airportName) - 1);
  weather.airportName[sizeof(weather.airportName) - 1] = '\0';
  log_i("METAR updated: T=%d°C, WS=%dkmh, P=%dhPa, RH=%d%% Lat=%.3f,Lon=%.3f", weather.temperature, weather.windSpeedKmh, weather.pressure,
        weather.relativeHumidity, weather.lat, weather.lon);
  log_i("Last update was =%lus, Data age =%dmin", weather.epochTime - weather.timeOfLastUpdate, weather.dataAgeMin);
  weather.timeOfLastUpdate = weather.epochTime;
  return true;
}

// Returns true if successful, sets offset_seconds
bool getUtcOffset(float lat, float lon, long &offsetSeconds) {
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();
  char url[128];
  if (WiFi.status() != WL_CONNECTED) return false;
  if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) return false;
  if (lat == 0 && lon == 0) {
    log_i("no lat and lon values for getUtcOffset()");
    return false;
  }
  snprintf(url, sizeof(url), "https://timeapi.io/api/TimeZone/coordinate?latitude=%.3f&longitude=%.3f", lat, lon);
  log_i("Fetching time offset from: %s", url);
  if (!http.begin(client, url)) {
    log_i("Failed to connect to timeapi.io");
    http.end();
    return false;
  }
  int httpCode = http.GET();
  if (httpCode != 200) {
    log_i("timeapi.io returned HTTP %d", httpCode);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    log_i("Failed to parse JSON: %s", err.c_str());
    return false;
  }
  if (!doc["currentUtcOffset"].is<JsonObject>()) {
    log_i("JSON does not contain valid 'currentUtcOffset' object");
    return false;
  }
  if (!doc["currentUtcOffset"]["seconds"].is<long>()) {
    log_i("currentUtcOffset does not contain valid 'seconds' field");
    return false;
  }
  offsetSeconds = doc["currentUtcOffset"]["seconds"];
  log_i("UTC Offset for (%.5f, %.5f) is %ld seconds", lat, lon, offsetSeconds);
  return true;
}

#define LEAP_YEAR(Y) ((Y > 0) && !(Y % 4) && ((Y % 100) || !(Y % 400)))
String getFormattedDate(unsigned long epoch) {
  unsigned long days = epoch / 86400L, totalDays = 0;
  int year = 1970, month = 0;
  static const uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  while ((totalDays += LEAP_YEAR(year) ? 366 : 365) <= days) year++;
  days -= totalDays - (LEAP_YEAR(year) ? 366 : 365);
  for (; month < 12 && days >= (month == 1 && LEAP_YEAR(year) ? 29 : monthDays[month]); month++) days -= month == 1 && LEAP_YEAR(year) ? 29 : monthDays[month];
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%02lu-%02d-%04d", days + 1, month + 1, year);
  return String(buffer);
}

String getFormattedTime(unsigned long epoch) {
  unsigned long timeSeconds = epoch % 86400;
  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u", (uint)(timeSeconds / 3600), (uint)((timeSeconds % 3600) / 60), (uint)(timeSeconds % 60));
  return String(buffer);
}

String sunEvent(unsigned long timeStamp, float lat, float lon, bool isRise, long timeOffset) {
  log_i("Time: %s %s", getFormattedTime(timeStamp).c_str(), getFormattedDate(timeStamp).c_str());
  log_i("Sun_event input: t=%lu, lat=%.3f, lon=%.3f, rise=%d, time_offset=%ld", timeStamp, lat, lon, isRise, timeOffset);
  constexpr double ZENITH = 90.833;
  time_t rawTime = (time_t)timeStamp;
  struct tm *ptm = gmtime(&rawTime);
  int year = ptm->tm_year + 1900;
  int month = ptm->tm_mon + 1;
  int day = ptm->tm_mday;
  int n1 = floor(275 * month / 9);
  int n2 = floor((month + 9) / 12);
  int n3 = (1 + floor((year - 4 * floor(year / 4) + 2) / 3));
  int n = n1 - (n2 * n3) + day - 30;
  double lngHour = lon / 15.0;
  double approxTime = isRise ? n + ((6 - lngHour) / 24.0) : n + ((18 - lngHour) / 24.0);
  double meanAnomaly = (0.9856 * approxTime) - 3.289;
  double trueLong = meanAnomaly + (1.916 * sin(DEG_TO_RAD * meanAnomaly)) + (0.020 * sin(2 * DEG_TO_RAD * meanAnomaly)) + 282.634;
  if (trueLong >= 360.0) trueLong -= 360.0;
  if (trueLong < 0.0) trueLong += 360.0;
  double rightAsc = RAD_TO_DEG * atan(0.91764 * tan(DEG_TO_RAD * trueLong));
  if (rightAsc < 0.0) rightAsc += 360.0;
  if (rightAsc >= 360.0) rightAsc -= 360.0;
  double lQuadrant = floor(trueLong / 90.0) * 90.0;
  double raQuadrant = floor(rightAsc / 90.0) * 90.0;
  rightAsc = rightAsc + (lQuadrant - raQuadrant);
  rightAsc /= 15.0;
  double sinDec = 0.39782 * sin(DEG_TO_RAD * trueLong);
  double cosDec = cos(asin(sinDec));
  double cosH = (cos(DEG_TO_RAD * ZENITH) - (sinDec * sin(DEG_TO_RAD * lat))) / (cosDec * cos(DEG_TO_RAD * lat));
  if (cosH > 1) return String("No sunrise");
  if (cosH < -1) return String("No sunset");
  double hourAngle = isRise ? 360.0 - RAD_TO_DEG * acos(cosH) : RAD_TO_DEG * acos(cosH);
  hourAngle /= 15.0;
  double localMeanTime = hourAngle + rightAsc - (0.06571 * approxTime) - 6.622;
  double utcTime = localMeanTime - lngHour;
  while (utcTime < 0) utcTime += 24.0;
  while (utcTime >= 24) utcTime -= 24.0;
  unsigned long eventSec = (unsigned long)(utcTime * 3600);
  if (timeOffset != 0) {
    long eventLocal = (long)eventSec + (timeOffset % 86400);
    if (eventLocal < 0) eventLocal += 86400;
    if (eventLocal >= 86400) eventLocal -= 86400;
    eventSec = (unsigned long)eventLocal;
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)(eventSec / 3600), (unsigned)((eventSec % 3600) / 60), (unsigned)(eventSec % 60));
  return String(buf);
}

// Update time and date display
void updateTimeCallback(lv_timer_t *timer) {
  if (WiFi.status() != WL_CONNECTED) return;  // Exit if not connected to WiFi
  timeClient.update();
  weather.epochTime = timeClient.getEpochTime();
  updateLabel(uiElements.timeDateLabel, LV_SYMBOL_LIST " %s %s", getFormattedTime(weather.epochTime).c_str(), getFormattedDate(weather.epochTime).c_str());
  lv_label_set_text_fmt(uiElements.bigTimeLabel, "%s", getFormattedTime(weather.epochTime).c_str());
  lv_label_set_text_fmt(uiElements.bigDateLabel, "%s", getFormattedDate(weather.epochTime).c_str());
  if (weather.weatherIsValid) {
    weather.dataAgeMin = (weather.epochTime - config.timeOffset - weather.obsTime) / 60;
    updateLabel(uiElements.dataAgeLabel, LV_SYMBOL_REFRESH " %lu min ago", weather.dataAgeMin);
  }
  // log_i("weather.epochtime: %lu, config.time_offset %ld, weather.obsTime %lu, weather.data_age_min: %d", weather.epochTime, config.timeOffset,
  // weather.obsTime, weather.dataAgeMin);
}

// Update weather data display with enhanced visuals
void updateWeatherCallback(lv_timer_t *timer) {
  if (!weather.weatherIsValid || weather.dataAgeMin > 60 || weather.epochTime - weather.timeOfLastUpdate > 600) {
    weather.weatherIsValid = fetchWeatherData();
    weather.utcOffsetIsValid = getUtcOffset(weather.lat, weather.lon, weather.localTimeOffset);
    if (weather.utcOffsetIsValid) {
      weather.sunrise = sunEvent(weather.epochTime, weather.lat, weather.lon, true, weather.localTimeOffset);
      weather.sunset = sunEvent(weather.epochTime, weather.lat, weather.lon, false, weather.localTimeOffset);
      log_i("Next sunrise: %s", weather.sunrise.c_str());
      log_i("Next sunset: %s", weather.sunset.c_str());
    } else
      weather.localTimeOffset = 0;
  }
  // Update weather data with icons
  updateLabel(uiElements.temperatureLabel, LV_SYMBOL_BATTERY_3 " %d°C", weather.temperature);
  updateLabel(uiElements.humidityLabel, LV_SYMBOL_TINT " %d%%", weather.relativeHumidity);
  updateLabel(uiElements.windSpeedLabel, LV_SYMBOL_GPS " %d km/h", weather.windSpeedKmh);
  updateLabel(uiElements.pressureLabel, LV_SYMBOL_POWER " %d hPa", weather.pressure);
  updateLabel(uiElements.airportNameLabel, LV_SYMBOL_HOME " %s", weather.airportName);
  if (weather.weatherIsValid && weather.utcOffsetIsValid) {
    // Calculate and display sunrise/sunset
    updateLabel(uiElements.sunriseLabel, LV_SYMBOL_UP " %s", weather.sunrise.c_str());
    updateLabel(uiElements.sunsetLabel, LV_SYMBOL_DOWN " %s", weather.sunset.c_str());
  } else {
    updateLabel(uiElements.sunriseLabel, LV_SYMBOL_UP " --:--");
    updateLabel(uiElements.sunsetLabel, LV_SYMBOL_DOWN " --:--");
    updateLabel(uiElements.dataAgeLabel, LV_SYMBOL_REFRESH " -- min ago");
    updateLabel(uiElements.sunriseLabel, LV_SYMBOL_UP " --:--");
    updateLabel(uiElements.sunsetLabel, LV_SYMBOL_DOWN " --:--");
  }
}

// Initialize hardware and software
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  Serial.setDebugOutput(true);
  log_i("Booting...");
  uint32_t chipId1 = 0, chipId2 = 0;
  uint64_t mac = ESP.getEfuseMac();
  chipId1 = (uint32_t)(mac >> 24) & 0xFFFFFF;
  uint8_t macAddr[6];
  WiFi.macAddress(macAddr);
  chipId2 = (macAddr[3] << 16) | (macAddr[4] << 8) | macAddr[5];

  log_i("Chip ID1 (EFuse): %u", chipId1);
  log_i("Chip ID2 (Wi-Fi MAC): %u", chipId2);
  log_i("CPU: %s rev%d, CPU Freq: %d Mhz, %d core(s)", ESP.getChipModel(), ESP.getChipRevision(), getCpuFrequencyMhz(), ESP.getChipCores());
  log_i("Board: %s", BOARD_NAME);
  log_i("SDK version: %s", ESP.getSdkVersion());
  log_i("PSRAM total: %u", ESP.getPsramSize());
  log_i("PSRAM free:  %u", ESP.getFreePsram());
  log_i("Heap free:   %d bytes", ESP.getFreeHeap());
  smartdisplay_init();
  smartdisplay_lcd_set_backlight(1.0);
  loadConfigurations();
  WiFi.mode(WIFI_STA);
  uiInit();
  lv_timer_create(updateTimeCallback, 1000, NULL);
  lv_timer_create(updateWeatherCallback, 60000, NULL);
  lv_timer_create(wifiManagementCallback, 1000, NULL);
  timeClient.setTimeOffset(config.timeOffset);
  timeClient.begin();
}

auto lastLvTick = millis();
void loop() {
  auto now = millis();
  lv_tick_inc(now - lastLvTick);
  lastLvTick = now;
  lv_timer_handler();
}























