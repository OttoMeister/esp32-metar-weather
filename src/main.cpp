// ESP32S board - 8048S043C -  4.3-inch TFT 800x480 - Capacitive touch - 8M PSRAM 16M Flash
// pio pkg update -e esp32-8048S043C
// pio run -t clean -e esp32-8048S043C
// pio run -e esp32-8048S043C
// pio run -e esp32-8048S043C --upload-port  /dev/ttyUSB0 -t upload
// pio run -e esp32-8048S043C --monitor-port /dev/ttyUSB0 -t monitor
// https://formatter.org/cpp-formatter

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp32_smartdisplay.h>
#include <lvgl.h>
#include <math.h>
#include <cmath>
#include <strings.h>

Preferences preferences;

// UI elements for main screen
lv_obj_t *ui_scrMain;
lv_obj_t *ui_pnlMain;
lv_obj_t *lblTemperature;
lv_obj_t *lblHumidity;
lv_obj_t *lblWindSpeed;
lv_obj_t *lblPressure;
lv_obj_t *lblTimeDate;
lv_obj_t *lblWifiStatus;
lv_obj_t *lblAirportName;
lv_obj_t *lblDataAge;
lv_obj_t *lblSunrise;
lv_obj_t *lblSunset;
lv_obj_t *lblBigTime;
lv_obj_t *lblBigDate;

// UI elements for settings screen
lv_obj_t *ui_scrSetting;
lv_obj_t *ui_taSSID;
lv_obj_t *ui_taPassword;
lv_obj_t *ui_taMetarId;
lv_obj_t *ui_taTimeOffset;
lv_obj_t *ui_kb;

// Configuration variables
char ssid[64] = "";
char password[64] = "";
char metar_id[10] = ""; // search here:  "https://en.wikipedia.org/wiki/ICAO_airport_code"
long time_offset = 0;

// NTP and time client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

enum WiFiState {
  DISCONNECTED,
  CONNECTING,
  CONNECTED,
  RECONNECTING
};
WiFiState wifi_state = DISCONNECTED;
unsigned long last_connect_attempt = 0;
unsigned long connect_start_time = 0;

// Weather data variables
String metar_url;
int temperature = 0;
int dew_point = 0;
int wind_speed_knots = 0;
int pressure = 0;
int relative_humidity = 0;
int wind_speed_kmh = 0;
char airport_name[100];
unsigned long obsTime = 0;
float lat = 0, lon= 0;

// Normalize string by replacing accented characters
String normalizeString(String str) {
  static const char *replacements[][2] = {
    {"á", "a"},  {"à", "a"},  {"â", "a"},  {"ã", "a"},  {"ä", "ae"}, {"æ", "ae"}, {"Á", "A"},  {"À", "A"}, 
    {"Â", "A"},  {"Ã", "A"},  {"Ä", "Ae"}, {"Æ", "AE"}, {"é", "e"},  {"è", "e"},  {"ê", "e"},  {"ë", "e"}, 
    {"É", "E"},  {"È", "E"},  {"Ê", "E"},  {"Ë", "E"},  {"í", "i"},  {"ì", "i"},  {"î", "i"},  {"ï", "i"}, 
    {"Í", "I"},  {"Ì", "I"},  {"Î", "I"},  {"Ï", "I"},  {"ó", "o"},  {"ò", "o"},  {"ô", "o"},  {"õ", "o"}, 
    {"ö", "oe"}, {"œ", "oe"}, {"Ó", "O"},  {"Ò", "O"},  {"Ô", "O"},  {"Õ", "O"},  {"Ö", "Oe"}, {"Œ", "OE"}, 
    {"ú", "u"},  {"ù", "u"},  {"û", "u"},  {"ü", "ue"}, {"Ú", "U"},  {"Ù", "U"},  {"Û", "U"},  {"Ü", "Ue"},
    {"ñ", "n"},  {"Ñ", "N"},  {"ç", "c"},  {"Ç", "C"},  {"ÿ", "y"},  {"Ÿ", "Y"},  {"ß", "ss"}, {"ẞ", "SS"}};
  String result = str;
  for (auto &pair : replacements) 
    result.replace(pair[0], pair[1]);
  return result;
}

// Update label text with formatted string
void update_label(lv_obj_t *label, const char *format, ...) {
  char buffer[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  lv_label_set_text(label, buffer);
}

// Save configuration settings
void save_configurations() {
  preferences.begin("config", false);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  preferences.putString("metar_id", metar_id);
  preferences.putInt("time_offset", time_offset);
  preferences.end();
  log_i("Saved configurations: SSID: \"%s\", Password: \"%s\", METAR ID: \"%s\", Time Offset: \"%i\"", ssid, password, metar_id, time_offset);
}

// Load configuration settings
void load_configurations() {
  preferences.begin("config", true);
  preferences.getString("ssid", ssid, sizeof(ssid));
  preferences.getString("password", password, sizeof(password));
  preferences.getString("metar_id", metar_id, sizeof(metar_id));
  time_offset = preferences.getInt("time_offset");
  preferences.end();
  log_i("Loaded configurations: SSID: \"%s\", Password: \"%s\", METAR ID: \"%s\", Time Offset: \"%i\"", ssid, password, metar_id, time_offset);
}

// Event handler for settings button
void ui_event_btnSettings(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) lv_disp_load_scr(ui_scrSetting);
}

void ui_event_btnBack(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    String tempSsid = lv_textarea_get_text(ui_taSSID);
    tempSsid.trim();
    strlcpy(ssid, tempSsid.c_str(), sizeof(ssid));
    strlcpy(password, lv_textarea_get_text(ui_taPassword) ? lv_textarea_get_text(ui_taPassword) : "", sizeof(password));
    String rawMetar = lv_textarea_get_text(ui_taMetarId);
    rawMetar.trim();
    rawMetar.toUpperCase();
    strlcpy(metar_id, rawMetar.substring(0, 4).c_str(), sizeof(metar_id));
    time_offset = lv_textarea_get_text(ui_taTimeOffset) ? strtol(lv_textarea_get_text(ui_taTimeOffset), nullptr, 10) : 0;
    save_configurations();
    timeClient.setTimeOffset(time_offset);
    lv_disp_load_scr(ui_scrMain);
  }
}

void wifi_management_cb(lv_timer_t *timer) {         
  lv_obj_t *current_screen = lv_disp_get_scr_act(NULL);
  if (current_screen == ui_scrMain) {
    switch (wifi_state) {
      case DISCONNECTED:
        if (millis() - last_connect_attempt >= 10000) {
          log_i("Starting WiFi connection to %s", ssid);
          WiFi.begin(ssid, password);
          connect_start_time = millis();
          last_connect_attempt = millis();
          wifi_state = CONNECTING;
          update_label(lblWifiStatus, LV_SYMBOL_WIFI " Connecting...");
        } else update_label(lblWifiStatus, LV_SYMBOL_CLOSE " Disconnected");
        break;
      case CONNECTING:
        if (WiFi.status() == WL_CONNECTED) {
          wifi_state = CONNECTED;
          log_i("WiFi: Connected to %s with %d dBm", WiFi.SSID().c_str(), WiFi.RSSI()); 
        } else if (millis() - connect_start_time >= 10000) {
          log_i("WiFi connection timeout");
          WiFi.disconnect();
          wifi_state = DISCONNECTED;
          update_label(lblWifiStatus, LV_SYMBOL_CLOSE " Connection Failed");
        } else update_label(lblWifiStatus, LV_SYMBOL_WIFI " Connecting...");
        break;
      case CONNECTED:
        if (WiFi.status() != WL_CONNECTED) {
          wifi_state = RECONNECTING;
          log_i("WiFi connection lost");
          update_label(lblWifiStatus, LV_SYMBOL_CLOSE " Connection Lost");
        } else update_label(lblWifiStatus, LV_SYMBOL_WIFI " %s", WiFi.SSID().c_str()); 
        break;
      case RECONNECTING:
        if (millis() - last_connect_attempt >= 10000) {
          log_i("Attempting to reconnect WiFi");
          WiFi.reconnect();
          connect_start_time = millis();
          last_connect_attempt = millis();
          wifi_state = CONNECTING;
          update_label(lblWifiStatus, LV_SYMBOL_WIFI " Reconnecting...");
        } else update_label(lblWifiStatus, LV_SYMBOL_CLOSE " Connection Lost");
        break;
    }
  } else if (current_screen == ui_scrSetting) {
    if (WiFi.status() == WL_CONNECTED) {
      log_i("Disconnecting WiFi (settings screen active)");
      WiFi.disconnect();
    }
    wifi_state = DISCONNECTED;
    update_label(lblWifiStatus, LV_SYMBOL_CLOSE " Disconnected");
  }
}

// Show keyboard for text area input
void show_keyboard(lv_event_t *e, lv_obj_t *textarea) {
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
    lv_keyboard_set_textarea(ui_kb, textarea);
    lv_obj_clear_flag(ui_kb, LV_OBJ_FLAG_HIDDEN);
  }
}

// Event handlers for text areas
void ui_event_taSSID(lv_event_t *e) { show_keyboard(e, ui_taSSID); }
void ui_event_taPassword(lv_event_t *e) { show_keyboard(e, ui_taPassword); }
void ui_event_taMetarId(lv_event_t *e) { show_keyboard(e, ui_taMetarId); }
void ui_event_taTimeOffset(lv_event_t *e) { show_keyboard(e, ui_taTimeOffset); }

// Handle keyboard events
void ui_event_kb(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_READY || lv_event_get_code(e) == LV_EVENT_CANCEL) 
    lv_obj_add_flag(ui_kb, LV_OBJ_FLAG_HIDDEN);
}

// Create a styled card panel
lv_obj_t *create_card(lv_obj_t *parent, int x, int y, int w, int h) {
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
lv_obj_t *create_styled_label(lv_obj_t *parent, int x, int y, const char *text, const char *icon = nullptr) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_pos(label, x, y);
  char full_text[200];
  if (icon) snprintf(full_text, sizeof(full_text), "%s %s", icon, text);
  else strlcpy(full_text, text, sizeof(full_text)); 
  lv_label_set_text(label, full_text);
  lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
  return label;
}

// Create a gradient button with icon
lv_obj_t *create_gradient_button(lv_obj_t *parent, int x, int y, int w, int h, const char *text, const char *icon, lv_event_cb_t event_cb) {
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
  
  char full_text[64];
  if (icon) snprintf(full_text, sizeof(full_text), "%s %s", icon, text);
  else strlcpy(full_text, text, sizeof(full_text));
  
  lv_label_set_text(lbl, full_text);
  lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(lbl, LV_FONT_DEFAULT, LV_PART_MAIN);
  
  if (event_cb) lv_obj_add_event_cb(btn, event_cb, LV_EVENT_ALL, NULL);
  
  return btn;
}

// Create a text area with modern styling
lv_obj_t *create_modern_textarea(lv_obj_t *parent, int x, int y, int w, int h, bool one_line, bool password_mode, lv_event_cb_t event_cb, const char *initial_text, const char *placeholder) {
  lv_obj_t *ta = lv_textarea_create(parent);
  lv_obj_set_size(ta, w, h);
  lv_obj_set_pos(ta, x, y);
  lv_textarea_set_one_line(ta, one_line);
  lv_textarea_set_password_mode(ta, password_mode);
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
  
  if (event_cb) lv_obj_add_event_cb(ta, event_cb, LV_EVENT_ALL, NULL);
  lv_textarea_set_text(ta, initial_text);
  
  return ta;
}

// Initialize main screen with improved layout and better spacing
void ui_scrMain_screen_init(void) {
  ui_scrMain = lv_obj_create(NULL);
  lv_obj_remove_flag(ui_scrMain, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ui_scrMain, lv_color_hex(0x0d1117), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(ui_scrMain, lv_color_hex(0x161b22), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(ui_scrMain, LV_GRAD_DIR_VER, LV_PART_MAIN);
  // Header card 
  lv_obj_t *header_card = create_card(ui_scrMain, 5, 5, 790, 80);
  lblWifiStatus = create_styled_label(header_card, 0, -5, "Disconnected", LV_SYMBOL_CLOSE);
  lblTimeDate = create_styled_label(header_card, 0, 25, "Time: --", LV_SYMBOL_LIST);
  create_gradient_button(header_card,  650, 0, 110, 40, "Settings", LV_SYMBOL_SETTINGS, ui_event_btnSettings);
  // Airport info card
  lv_obj_t *airport_card = create_card(ui_scrMain, 5, 90, 790, 45);
  lblAirportName = create_styled_label(airport_card, 0, -5, "Airport: --", LV_SYMBOL_HOME);
  lblDataAge = create_styled_label(airport_card, 400, -5, "Data Age: -- min", LV_SYMBOL_REFRESH);  
  // Weather data card - left side
  lv_obj_t *weather_card = create_card(ui_scrMain, 5, 140, 390, 185);
  lv_obj_t *weather_title = create_styled_label(weather_card, 0, -5, "Weather Data", LV_SYMBOL_EYE_OPEN);
  lv_obj_set_style_text_color(weather_title, lv_color_hex(0x3366ff), LV_PART_MAIN);
  lblTemperature = create_styled_label(weather_card, 0, 25, "Temperature: -- °C", LV_SYMBOL_BATTERY_3);
  lblHumidity = create_styled_label(weather_card, 0, 55, "Humidity: -- %", LV_SYMBOL_TINT);
  lblWindSpeed = create_styled_label(weather_card, 0, 85, "Wind Speed: -- km/h", LV_SYMBOL_GPS);
  lblPressure = create_styled_label(weather_card, 0, 115, "Pressure: -- hPa", LV_SYMBOL_POWER);
  // Sun times card - right side
  lv_obj_t *sun_card = create_card(ui_scrMain, 405, 140, 390, 185);
  lv_obj_t *sun_title = create_styled_label(sun_card, 0, -5, "Sun Times", LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(sun_title, lv_color_hex(0xffa500), LV_PART_MAIN);
  lblSunrise = create_styled_label(sun_card, 0, 25, "Sunrise: --", LV_SYMBOL_UP);
  lblSunset = create_styled_label(sun_card, 0, 55, "Sunset: --", LV_SYMBOL_DOWN);
  lv_obj_t *info_label1 = create_styled_label(sun_card, 0, 85, "Daylight Info", nullptr);
  lv_obj_set_style_text_color(info_label1, lv_color_hex(0x888888), LV_PART_MAIN);
   // Big time date card 
  lv_obj_t *big_time_date_card = create_card(ui_scrMain, 5, 330, 790, 95);
  lblBigTime = lv_label_create(big_time_date_card);
  lv_obj_align(lblBigTime, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_text_color(lblBigTime, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(lblBigTime, &lv_font_montserrat_48, LV_PART_MAIN);
  lv_label_set_text(lblBigTime, "--:--:--"); 
  lblBigDate = lv_label_create(big_time_date_card);
  lv_obj_align(lblBigDate,  LV_ALIGN_TOP_LEFT, 250, 0);
  lv_obj_set_style_text_color(lblBigDate, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_font(lblBigDate, &lv_font_montserrat_48, LV_PART_MAIN);
  lv_label_set_text(lblBigDate, "--.--.----"); 
   // Status card 
  lv_obj_t *status_card = create_card(ui_scrMain, 5, 430, 790, 45);
  lv_obj_t *status_label = create_styled_label(status_card, 0, -5, "ESP32 METAR Weather Station", LV_SYMBOL_EYE_OPEN);
  lv_obj_set_style_text_color(status_label, lv_color_hex(0x3366ff), LV_PART_MAIN);
  lv_obj_t *version_label = create_styled_label(status_card, 600, -5, "v1.0", nullptr);
  lv_obj_set_style_text_color(version_label, lv_color_hex(0x888888), LV_PART_MAIN);
}

// Initialize settings screen with modern design and better spacing
void ui_scrSetting_screen_init(void) {
  ui_scrSetting = lv_obj_create(NULL);
  lv_obj_remove_flag(ui_scrSetting, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(ui_scrSetting, lv_color_hex(0x0d1117), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(ui_scrSetting, lv_color_hex(0x161b22), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(ui_scrSetting, LV_GRAD_DIR_VER, LV_PART_MAIN);
  // Setting Header card 
  lv_obj_t *header_card = create_card(ui_scrSetting, 5, 5, 790, 70);
  lv_obj_t *title = create_styled_label(header_card, 0, 5, "Weather Station Settings", LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_color(title, lv_color_hex(0x3366ff), LV_PART_MAIN);
  create_gradient_button(header_card, 650, -5, 110, 40, "Save", LV_SYMBOL_SAVE, ui_event_btnBack);
  // Settings card 
  lv_obj_t *form_card = create_card(ui_scrSetting, 5, 80, 790, 345);
  lv_obj_t *ssid_label = create_styled_label(form_card, 0, -5, "WiFi Network (SSID):");
  lv_obj_set_style_text_color(ssid_label, lv_color_hex(0xcccccc), LV_PART_MAIN);
  ui_taSSID = create_modern_textarea(form_card, 0, 20, 300, 35, true, false, ui_event_taSSID, ssid, "Enter WiFi SSID");
  lv_obj_t *pwd_label = create_styled_label(form_card, 360, -5, "WiFi Password:");
  lv_obj_set_style_text_color(pwd_label, lv_color_hex(0xcccccc), LV_PART_MAIN);
  ui_taPassword = create_modern_textarea(form_card, 360, 20, 300, 35, true, true, ui_event_taPassword, password, "Enter password");
  lv_obj_t *metar_label = create_styled_label(form_card, 0, 75, "METAR ID (4 letters):");
  lv_obj_set_style_text_color(metar_label, lv_color_hex(0xcccccc), LV_PART_MAIN);
  ui_taMetarId = create_modern_textarea(form_card, 0, 100, 300, 35, true, false, ui_event_taMetarId, metar_id, "e.g. KJFK");
  lv_obj_t *offset_label = create_styled_label(form_card, 360, 75, "Time Offset (seconds):");
  lv_obj_set_style_text_color(offset_label, lv_color_hex(0xcccccc), LV_PART_MAIN);
  ui_taTimeOffset = create_modern_textarea(form_card, 360, 100, 300, 35, true, false, ui_event_taTimeOffset, String(time_offset).c_str(), "UTC offset");
  // Help card 
  lv_obj_t *help_card = create_card(ui_scrSetting, 5, 430, 790, 45);
  lv_obj_t *help_title = create_styled_label(help_card, 0, -5, "Help:", nullptr);
  lv_obj_set_style_text_color(help_title, lv_color_hex(0x3366ff), LV_PART_MAIN);
  lv_obj_t *help_text = create_styled_label(help_card, 50, -5, "METAR ID: Find airport codes at wikipedia.org/wiki/ICAO_airport_code", nullptr);
  lv_obj_set_style_text_color(help_text, lv_color_hex(0x888888), LV_PART_MAIN); 
  // Keyboard - positioned better
  ui_kb = lv_keyboard_create(ui_scrSetting);
  lv_keyboard_set_textarea(ui_kb, ui_taSSID);
  lv_obj_add_flag(ui_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(ui_kb, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_kb, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
  lv_obj_add_event_cb(ui_kb, ui_event_kb, LV_EVENT_ALL, NULL);
  // Position keyboard at bottom when visible
  lv_obj_set_size(ui_kb, 770, 150);
  lv_obj_align(ui_kb, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// Initialize UI with modern theme
void ui_init(void) {
  lv_disp_t *display = lv_display_get_default();
  lv_theme_t *theme = lv_theme_default_init(display, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
  lv_disp_set_theme(display, theme);
  
  ui_scrMain_screen_init();
  ui_scrSetting_screen_init();
  lv_disp_load_scr(ui_scrMain);
}

void weatherData() {
  HTTPClient http;
  http.setTimeout(10000);  
  WiFiClientSecure client;
  client.setInsecure();    
  char urlBuffer[100];
  snprintf(urlBuffer, sizeof(urlBuffer), "https://aviationweather.gov/api/data/metar?ids=%s&format=json", metar_id);
  log_i("Fetching METAR from: %s via HTTPS", urlBuffer);
  if (!http.begin(client, urlBuffer)) {
    log_i("Failed to initialize HTTPS connection");
    http.end();
    return;
  }
  http.setReuse(false); 
  log_i("Sending GET request...");
  int httpCode = http.GET();
  log_i("HTTP status code: %d", httpCode);
  if (httpCode != 200) {
    String errorResponse = http.getString();
    log_i("HTTP request failed with code: %d, Response: %s", httpCode, errorResponse.substring(0, 200).c_str());
    http.end();
    return;
  }
  String responseStr = http.getString();
  if (responseStr.length() == 0) {
    log_i("Empty response received");
    http.end();
    return;
  }
  http.end();
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, responseStr);
  if (error) {
    log_i("JSON parsing failed: %s", error.c_str());
    return;
  }
  if (!doc.is<JsonArray>() || doc.as<JsonArray>().size() == 0) {
    log_i("Invalid METAR JSON structure: not an array or empty");
    return;
  }
  JsonObject obj = doc[0].as<JsonObject>();
  if (obj["icaoId"].isNull() || obj["temp"].isNull()) {
    log_i("Invalid METAR JSON structure: missing icaoId or temp");
    return;
  }
  const char* newId = obj["icaoId"];
  if (strcmp(newId, metar_id) != 0) {
    log_i("METAR ID mismatch: %s vs %s", newId, metar_id);
    return;
  }
  temperature = obj["temp"] | 0;
  dew_point = obj["dewp"] | 0;
  wind_speed_knots = obj["wspd"] | 0;
  pressure = obj["altim"] | 0;
  obsTime = obj["obsTime"] | 0;
  lat = obj["lat"].as<float>();
  lon = obj["lon"].as<float>();
  wind_speed_kmh = wind_speed_knots * 1.852;
  float t = temperature, d = dew_point;
  relative_humidity = 100 * exp((17.625 * d) / (243.04 + d)) / exp((17.625 * t) / (243.04 + t));
  const char* name = obj["name"] | "Unknown";
  strncpy(airport_name, name, sizeof(airport_name) - 1);
  airport_name[sizeof(airport_name) - 1] = '\0';
  metar_url = urlBuffer;
  log_i("METAR updated: T=%d°C, DP=%d°C, WS=%dkn, P=%dhPa, RH=%d%% Lat=%.3f, Lon=%.3f",
        temperature, dew_point, wind_speed_knots, pressure, relative_humidity, lat, lon);
}

#define LEAP_YEAR(Y) ((Y>0)&&!(Y%4)&&((Y%100)||!(Y%400)))
String getFormattedDate(unsigned long e){
 unsigned long d=e/86400L,t=0;int y=1970,m=0;
 static const uint8_t md[]={31,28,31,30,31,30,31,31,30,31,30,31};
 while((t+=LEAP_YEAR(y)?366:365)<=d)y++;
 d-=t-(LEAP_YEAR(y)?366:365);
 for(;m<12&&d>=(m==1&&LEAP_YEAR(y)?29:md[m]);m++)d-=m==1&&LEAP_YEAR(y)?29:md[m];
 char b[20];snprintf(b,sizeof(b),"%02lu-%02d-%04d",d+1,m+1,y);return String(b);
}

String getFormattedTime(unsigned long e){
 unsigned long t=e%86400;char b[9];
 snprintf(b,sizeof(b),"%02u:%02u:%02u",(uint)(t/3600),(uint)((t%3600)/60),(uint)(t%60));
 return String(b);
}

// rise = true = sunrise // rise = false = sunset
String sun_event(unsigned long t, float lat, float lon, bool rise, long time_offset) {
  log_i("Time: %s %s", getFormattedTime(t).c_str(), getFormattedDate(t).c_str());
  log_i("Sun_event input: t=%lu, lat=%.3f, lon=%.3f, rise=%d, time_offset=%ld", t, lat, lon, rise, time_offset);
  double n = t / 86400.0 - 10957.5, L = fmod(280.46 + 0.9856474 * n, 360) * 0.017453293;
  double g = fmod(357.528 + 0.9856003 * n, 360) * 0.017453293, lam = L + 0.033423 * sin(g) + 0.000349 * sin(2*g);
  double decl = asin(0.39782 * sin(lam)), EoT = 4 * (L * 57.295779 - atan2(0.91746 * sin(lam), cos(lam)) * 57.295779);
  if (EoT > 720) EoT -= 1440; 
  if (EoT < -720) EoT += 1440;
  double cosh = (sin(-0.014544) - sin(lat * 0.017453293) * sin(decl)) / (cos(lat * 0.017453293) * cos(decl));
  if (cosh < -1 || cosh > 1) return String(cosh < -1 ? "No sunset" : "No sunrise");
  double h = acos(cosh) * 57.295779, stime = fmod(12 - lon/15 - EoT/60 + (rise ? -h : h)/15 + 24, 24);
  t = (t/86400)*86400 + (unsigned long)(stime * 3600) + time_offset;
  unsigned long d = t/86400 + 719163, y = 1970 + d/365, doy = d % 365;
  if (doy > 58 && ((y%4==0 && y%100!=0) || y%400==0)) doy++;
  int m = doy < 31 ? 1 : doy < 59 ? 2 : doy < 90 ? 3 : doy < 120 ? 4 : doy < 151 ? 5 : 
          doy < 181 ? 6 : doy < 212 ? 7 : doy < 243 ? 8 : doy < 273 ? 9 : doy < 304 ? 10 : doy < 334 ? 11 : 12;
  int day = doy - (m==1?0:m==2?31:m==3?59:m==4?90:m==5?120:m==6?151:m==7?181:m==8?212:m==9?243:m==10?273:m==11?304:334) + 1;
  char buf[32]; 
  snprintf(buf, 32, "%02d:%02d:%02d %02d-%02d-%04d", (int)(t%86400)/3600, (int)(t%3600)/60, (int)t%60, day, m, (int)y);
  return String(buf);
}

// Update time and date display
void update_time_cb(lv_timer_t *timer) {
  if (WiFi.status() != WL_CONNECTED) return; // Exit if not connected to WiFi
  timeClient.update();
  unsigned long epochtime = timeClient.getEpochTime();
    update_label(lblTimeDate, LV_SYMBOL_LIST " %s %s", getFormattedTime(epochtime).c_str(), getFormattedDate(epochtime).c_str());
    if (lblBigTime && lblBigDate) {
      lv_label_set_text_fmt(lblBigTime, "%s", getFormattedTime(epochtime).c_str());
      lv_label_set_text_fmt(lblBigDate, "%s", getFormattedDate(epochtime).c_str());
    }
}

// Get weather icon based on conditions
const char* getWeatherIcon(int temp, int humidity, int wind_speed) {
  if (temp < 0) return LV_SYMBOL_MINUS; // Cold/Snow
  else if (humidity > 80) return LV_SYMBOL_TINT; // Rainy/Humid
  else if (wind_speed > 20) return LV_SYMBOL_GPS; // Windy
  else if (temp > 25) return LV_SYMBOL_WIFI; // Hot/Sunny
  else return LV_SYMBOL_EYE_OPEN; // Normal
}

// Update weather data display with enhanced visuals
void update_weather_cb(lv_timer_t *timer) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (WiFi.localIP() == IPAddress(0, 0, 0, 0))  return;
  weatherData();
  
  
  // Update weather data with icons
  update_label(lblTemperature, LV_SYMBOL_BATTERY_3 " %d°C", temperature);
  update_label(lblHumidity, LV_SYMBOL_TINT " %d%%", relative_humidity);
  update_label(lblWindSpeed, LV_SYMBOL_GPS " %d km/h", wind_speed_kmh);
  update_label(lblPressure, LV_SYMBOL_POWER " %d hPa", pressure);
  update_label(lblAirportName, LV_SYMBOL_HOME " %s", normalizeString(airport_name).c_str());
  
  if (timeClient.isTimeSet() && obsTime > 0) {
    unsigned long epochtime = timeClient.getEpochTime();
    unsigned long data_age_min = (epochtime - time_offset - obsTime) / 60;
    update_label(lblDataAge, LV_SYMBOL_REFRESH " %lu min ago", data_age_min);
    
    log_i("Temperature: %d", (int)temperature);
    log_i("Relative humidity: %d%%", (int)relative_humidity);
    log_i("Wind speed: %d km/h", (int)wind_speed_kmh);
    log_i("Pressure: %d hPa", (int)pressure);
    log_i("Data age: %u min", data_age_min);
    log_i("Epochtime: %u sec", epochtime);
    
    // Calculate and display sunrise/sunset
    if (lat != 0 && lon != 0) {
      String sunrise = sun_event(epochtime, lat, lon, true, time_offset);
      String sunset = sun_event(epochtime, lat, lon, false, time_offset);
      
      // Extract just the time portion for display
      int space_pos = sunrise.indexOf(' ');
      String sunrise_time = space_pos > 0 ? sunrise.substring(0, space_pos) : sunrise;
      space_pos = sunset.indexOf(' ');
      String sunset_time = space_pos > 0 ? sunset.substring(0, space_pos) : sunset;
      
      update_label(lblSunrise, LV_SYMBOL_UP " %s", sunrise_time.c_str());
      update_label(lblSunset, LV_SYMBOL_DOWN " %s", sunset_time.c_str());
      
      log_i("Next sunrise: %s", sunrise.c_str());
      log_i("Next sunset: %s", sunset.c_str());
    } else {
      update_label(lblSunrise, LV_SYMBOL_UP " --:--");
      update_label(lblSunset, LV_SYMBOL_DOWN " --:--");
    }
  } else {
    update_label(lblDataAge, LV_SYMBOL_REFRESH " -- min ago");
    update_label(lblSunrise, LV_SYMBOL_UP " --:--");
    update_label(lblSunset, LV_SYMBOL_DOWN " --:--");
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
  load_configurations();
  WiFi.mode(WIFI_STA);
  ui_init();
  lv_timer_create(update_time_cb, 1000, NULL);
  lv_timer_create(update_weather_cb, 60000, NULL);
  lv_timer_create(wifi_management_cb, 1000, NULL);
  timeClient.setTimeOffset(time_offset);
  timeClient.begin();
}

auto lv_last_tick = millis();
void loop() {
  auto now = millis();
  lv_tick_inc(now - lv_last_tick);
  lv_last_tick = now;
  lv_timer_handler();
}
