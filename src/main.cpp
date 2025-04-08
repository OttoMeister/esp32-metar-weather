//platformio run -e esp32-8048S043C -t upload
//platformio run -e esp32-8048S043C -t monitor
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
  char buffer[64];
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
  if (lv_event_get_code(e) == LV_EVENT_CLICKED) 
    lv_disp_load_scr(ui_scrSetting);
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
          update_label(lblWifiStatus, "WiFi: Connecting...");
        } else update_label(lblWifiStatus, "WiFi: Disconnected");
        break;
      case CONNECTING:
        if (WiFi.status() == WL_CONNECTED) {
          wifi_state = CONNECTED;
          log_i("WiFi: Connected to %s with %d dBm", WiFi.SSID().c_str(), WiFi.RSSI()); 
        } else if (millis() - connect_start_time >= 10000) {
          log_i("WiFi connection timeout");
          WiFi.disconnect();
          wifi_state = DISCONNECTED;
          update_label(lblWifiStatus, "WiFi: Connection Failed");
        } else update_label(lblWifiStatus, "WiFi: Connecting...");
        break;
      case CONNECTED:
        if (WiFi.status() != WL_CONNECTED) {
          wifi_state = RECONNECTING;
          log_i("WiFi connection lost");
          update_label(lblWifiStatus, "WiFi: Connection Lost");
        } else update_label(lblWifiStatus, "WiFi: Connected to %s", WiFi.SSID().c_str()); 
        break;
      case RECONNECTING:
        if (millis() - last_connect_attempt >= 10000) {
          log_i("Attempting to reconnect WiFi");
          WiFi.reconnect();
          connect_start_time = millis();
          last_connect_attempt = millis();
          wifi_state = CONNECTING;
          update_label(lblWifiStatus, "WiFi: Reconnecting...");
        } else update_label(lblWifiStatus, "WiFi: Connection Lost");
        break;
    }
  } else if (current_screen == ui_scrSetting) {
    if (WiFi.status() == WL_CONNECTED) {
      log_i("Disconnecting WiFi (settings screen active)");
      WiFi.disconnect();
    }
    wifi_state = DISCONNECTED;
    update_label(lblWifiStatus, "WiFi: Disconnected");
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

// Create a screen with black background
lv_obj_t *create_screen() {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
  return scr;
}

// Create a full-size panel
lv_obj_t *create_panel(lv_obj_t *parent) {
  lv_obj_t *pnl = lv_obj_create(parent);
  lv_obj_set_size(pnl, lv_pct(100), lv_pct(100));
  lv_obj_center(pnl);
  lv_obj_remove_flag(pnl, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(pnl, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_radius(pnl, 0, LV_PART_MAIN);
  lv_obj_set_style_border_width(pnl, 0, LV_PART_MAIN);
  return pnl;
}

// Create a label with white text
lv_obj_t *create_label(lv_obj_t *parent, int x, int y, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_pos(label, x, y);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
  return label;
}

// Create a button with centered label
lv_obj_t *create_button(lv_obj_t *parent, int w, int h, const char *label_text, lv_event_cb_t event_cb) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_size(btn, w, h);
  lv_obj_t *lbl = lv_label_create(btn);
  lv_obj_center(lbl);
  lv_label_set_text(lbl, label_text);
  lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
  lv_obj_add_event_cb(btn, event_cb, LV_EVENT_ALL, NULL);
  return btn;
}

// Create a text area with customization
lv_obj_t *create_textarea(lv_obj_t *parent, int x, int y, int w, int h, bool one_line, bool password_mode, lv_event_cb_t event_cb, const char *initial_text) {
  lv_obj_t *ta = lv_textarea_create(parent);
  lv_obj_set_size(ta, w, h);
  lv_obj_set_pos(ta, x, y);
  lv_textarea_set_one_line(ta, one_line);
  lv_textarea_set_password_mode(ta, password_mode);
  if (event_cb) lv_obj_add_event_cb(ta, event_cb, LV_EVENT_ALL, NULL);
  lv_textarea_set_text(ta, initial_text);
  lv_obj_set_style_bg_color(ta, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_color(ta, lv_color_white(), LV_PART_MAIN);
  return ta;
}

// Initialize main screen
void ui_scrMain_screen_init(void) {
  ui_scrMain = create_screen();
  ui_pnlMain = create_panel(ui_scrMain);
  lv_obj_t *ui_btnSettings = create_button(ui_pnlMain, 100, 50, "Settings", ui_event_btnSettings);
  lv_obj_align(ui_btnSettings, LV_ALIGN_TOP_LEFT, 0, 0);
  lblWifiStatus = create_label(ui_pnlMain, 10, 60, "WiFi: Disconnected");
  lblAirportName = create_label(ui_pnlMain, 10, 90, "Airport: --");
  lblDataAge = create_label(ui_pnlMain, 10, 120, "Data Age: -- min");
  lblTemperature = create_label(ui_pnlMain, 10, 150, "Temperature: -- °C");
  lblHumidity = create_label(ui_pnlMain, 10, 180, "Humidity: -- %");
  lblWindSpeed = create_label(ui_pnlMain, 10, 210, "Wind Speed: -- km/h");
  lblPressure = create_label(ui_pnlMain, 10, 240, "Pressure: -- hPa");
  lblTimeDate = create_label(ui_pnlMain, 10, 270, "Time/Date: --");
}

// Initialize settings screen
void ui_scrSetting_screen_init(void) {
  ui_scrSetting = create_screen();
  lv_obj_t *ui_btnBack = create_button(ui_scrSetting, 100, 50, "Back", ui_event_btnBack);
  lv_obj_align(ui_btnBack, LV_ALIGN_TOP_LEFT, 10, 10);
  create_label(ui_scrSetting, 150, 20, "SSID:");
  ui_taSSID = create_textarea(ui_scrSetting, 150, 50, 150, LV_SIZE_CONTENT, true, false, ui_event_taSSID, ssid);
  create_label(ui_scrSetting, 350, 20, "Password:");
  ui_taPassword = create_textarea(ui_scrSetting, 350, 50, 150, 40, true, true, ui_event_taPassword, password);
  create_label(ui_scrSetting, 150, 105, "METAR ID:");
  ui_taMetarId = create_textarea(ui_scrSetting, 150, 135, 150, LV_SIZE_CONTENT, true, false, ui_event_taMetarId, metar_id);
  create_label(ui_scrSetting, 350, 105, "Time Offset (s):");
  ui_taTimeOffset = create_textarea(ui_scrSetting, 350, 135, 150, LV_SIZE_CONTENT, true, false, ui_event_taTimeOffset, String(time_offset).c_str());
  ui_kb = lv_keyboard_create(ui_scrSetting);
  lv_keyboard_set_textarea(ui_kb, ui_taPassword);
  lv_obj_add_flag(ui_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(ui_kb, lv_color_white(), LV_PART_MAIN);
  lv_obj_add_event_cb(ui_kb, ui_event_kb, LV_EVENT_ALL, NULL);
}

// Initialize UI with theme and screens
void ui_init(void) {
  lv_disp_t *display = lv_display_get_default();
  lv_theme_t *theme = lv_theme_default_init(display, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
  lv_disp_set_theme(display, theme);
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_color(lv_scr_act(), lv_color_white(), LV_PART_MAIN);
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

// convert epoch time to string with format "dd-mm-yyyy".
#define LEAP_YEAR(Y) ((Y > 0) && !(Y % 4) && ((Y % 100) || !(Y % 400)))
String getFormattedDate(unsigned long epochtime) {
    unsigned long days = epochtime / 86400L, d = 0;
    int y = 1970, m = 0;
    static const uint8_t md[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    while ((d += LEAP_YEAR(y) ? 366 : 365) <= days) y++;
    days -= d - (LEAP_YEAR(y) ? 366 : 365);
    for (; m < 12 && days >= (m == 1 && LEAP_YEAR(y) ? 29 : md[m]); m++)
        days -= (m == 1 && LEAP_YEAR(y) ? 29 : md[m]); 
    char date_buf[20]; 
    snprintf(date_buf, sizeof(date_buf), "%02lu-%02d-%04d", days + 1, m + 1, y);
    return String(date_buf);
}

String getFormattedTime(unsigned long epochtime) {
    unsigned long t = epochtime % 86400;
    char buf[9];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned int)(t / 3600), 
             (unsigned int)((t % 3600) / 60), (unsigned int)(t % 60));
    return String(buf);
}

// rise = true = sunrise // rise = false = sunset
String sun_event(unsigned long t, float lat, float lon, bool rise, long time_offset) {
  log_i("Time: %s %s", getFormattedTime(t).c_str(), getFormattedDate(t).c_str());
  log_i("Sun_event input: t=%lu, lat=%.3f, lon=%.3f, rise=%d, time_offset=%ld", t, lat, lon, rise, time_offset);
  static const uint8_t md[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  unsigned long days = t / 86400L, sec = t % 86400L, dc = 0;
  int y = 1970, m = 0, d, doy;
  char buf[64];
  while (days >= dc + (LEAP_YEAR(y) ? 366 : 365)) dc += (LEAP_YEAR(y) ? 366 : 365), y++;
  int remaining_days = days - dc;
  while (remaining_days >= (m == 1 && LEAP_YEAR(y) ? 29 : md[m])) remaining_days -= (m == 1 && LEAP_YEAR(y) ? 29 : md[m]), m++;
  doy = d = remaining_days + 1;
  for (int i = 0; i < m; i++) doy += (i == 1 && LEAP_YEAR(y) ? 29 : md[i]);
  double gamma = 2 * PI * (doy - 1) / 365.25;
  double EoT = -7.655 * sin(gamma) + 9.873 * sin(2 * gamma + 3.588);
  double decl = 23.45 * sin(2 * PI * (doy - 80) / 365.25);
  double ha = acos((sin(-0.833 * DEG_TO_RAD) - sin(lat * DEG_TO_RAD) * sin(decl * DEG_TO_RAD)) / (cos(lat * DEG_TO_RAD) * cos(decl * DEG_TO_RAD))) * RAD_TO_DEG;
  double utc_time = fmod(12 + (rise ? -1 : 1) * (ha / 15.0) - (lon / 15.0) - (EoT / 60.0) + 24.0, 24.0);
  t = dc * 86400L + (unsigned long)(utc_time * 3600.0);
  if (!rise && sec > (t % 86400L)) t += 86400L, ++d > (m == 1 && LEAP_YEAR(y) ? 29 : md[m]) && (d = 1, ++m == 12 && (m = 0, y++));
  t += time_offset;
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d %02d-%02d-%04d", (int)(t % 86400L) / 3600, (int)(t % 3600L) / 60, (int)t % 60, d, m + 1, y);
  return String(buf);
}



// Update time and date display
void update_time_cb(lv_timer_t *timer) {
  if (WiFi.status() != WL_CONNECTED) return; // Exit if not connected to WiFi
  timeClient.update();
  if (timeClient.isTimeSet()) {
    unsigned long epochtime = timeClient.getEpochTime();
    update_label(lblTimeDate, "Time: %s %s", getFormattedTime(epochtime).c_str(), getFormattedDate(epochtime).c_str());
  }
}

// Update weather data display
void update_weather_cb(lv_timer_t *timer) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (WiFi.localIP() == IPAddress(0, 0, 0, 0))  return;
  weatherData();
  update_label(lblTemperature, "Temperature: %d °C", temperature);
  update_label(lblHumidity, "Humidity: %d %%", relative_humidity);
  update_label(lblWindSpeed, "Wind Speed: %d km/h", wind_speed_kmh);
  update_label(lblPressure, "Pressure: %d hPa", pressure);
  update_label(lblAirportName, "Airport: %s", normalizeString(airport_name).c_str());
  if (timeClient.isTimeSet() && obsTime > 0) {
    unsigned long epochtime = timeClient.getEpochTime();
    unsigned long data_age_min = (epochtime - time_offset - obsTime) / 60;
    update_label(lblDataAge, "Data Age: %lu min", data_age_min);
    log_i("Temperature: %d", (int)temperature);
    log_i("Relative humidity: %d%%", (int)relative_humidity);
    log_i("Wind speed: %d km/h", (int)wind_speed_kmh);
    log_i("Pressure: %d hPa", (int)pressure);
    log_i("Data age: %u min", data_age_min);
    log_i("Epochtime: %u sec", epochtime);
    String sunrise = sun_event(epochtime, lat, lon, true, time_offset);
    String sunset = sun_event(epochtime, lat, lon, false, time_offset);
    log_i("Next sunrise: %s", sunrise.c_str());
    log_i("Next sunset: %s", sunset.c_str());    
  } else update_label(lblDataAge, "Data Age: -- min");
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
  log_i("Free heap: %d bytes", ESP.getFreeHeap());
  log_i("Free PSRAM: %d bytes", ESP.getPsramSize());
  log_i("SDK version: %s", ESP.getSdkVersion());
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
