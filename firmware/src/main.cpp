#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>
#include <driver/rtc_io.h>
#include <Wire.h>
#include <time.h>
#include <Preferences.h>
#include "secrets.h"

// --- RTC Persistent Memory ---
int bootCount = 0;
int measureCount = 0;

// Dynamic Location Data (Persistent in NVS)
bool locationSynced = false;
float locLat = 0.0;
float locLon = 0.0;
long locOffset = 28800; 
char locCity[16] = "UNKNOWN";

Preferences preferences;
SemaphoreHandle_t displayMutex;

enum SystemState { SS_MENU, SS_CONNECTING, SS_SCANNING, SS_SYNCING, SS_LOCATING, SS_CLOCK, SS_WEATHER, SS_SLEEPING, SS_STATS, SS_RESET };
volatile SystemState currentState = SS_MENU;
String uiLine1 = "", uiLine2 = "", uiLine3 = "";
const uint8_t* uiIcon = NULL;

// --- Pin Definitions ---
#define DHTPIN 4
#define DHTTYPE DHT11
#define BUTTON_PIN 33 
#define LDR_PIN 34
#define RED_PIN 13
#define GREEN_PIN 14
#define BLUE_PIN 27
#define OLED_RST 16 

// --- OLED Config (64x48 visible window) ---
#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64
#define OLED_W 64
#define OLED_H 48
#define OLED_OFFSET_X 32    
#define OLED_OFFSET_Y 16 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const unsigned char icon_wifi[] PROGMEM = { 0x3C, 0x7E, 0xC3, 0x00, 0x3C, 0x42, 0x00, 0x18 };
const unsigned char icon_cloud[] PROGMEM = { 0x18, 0x3C, 0x7E, 0xDB, 0xFF, 0x24, 0x24, 0x24 }; // Syncing
const unsigned char icon_pin[] PROGMEM = { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x18, 0x00, 0x00 };   // Location
const unsigned char icon_scan[] PROGMEM = { 0xFF, 0x81, 0xBD, 0xA5, 0xA5, 0xBD, 0x81, 0xFF }; // Sensor Scan

// --- Menu Configuration ---
enum MenuPage { PAGE_MEASURE, PAGE_TIME, PAGE_WEATHER, PAGE_LOCATE, PAGE_STATS, PAGE_RESET, PAGE_SLEEP };
const char* menuItems[] = {" MEASURE", " CLOCK", " WEATHER", " LOCATE", " STATS", " RESET", " SLEEP"};
const int TOTAL_MENU_ITEMS = 7;
const int VISIBLE_MENU_ITEMS = 3;
int currentMenuIndex = 0;
unsigned long lastInteractionTime = 0;
#define MENU_TIMEOUT 15000 
#define LONG_PRESS_MS 1200

// LEDC Channels
#define RED_CH 0
#define GREEN_CH 1
#define BLUE_CH 2

// --- Global Objects ---
DHT dht(DHTPIN, DHTTYPE);
Adafruit_MPU6050 mpu;
bool mpuFound = false;
bool oledFound = false;

void setLED(int r, int g, int b) {
  ledcWrite(RED_CH, r);
  ledcWrite(GREEN_CH, g);
  ledcWrite(BLUE_CH, b);
}

// Helper to poll button so we can cancel out of long screens
bool waitWithButtonPoll(unsigned long ms) {
  // Wait for the user to release the button from the long press FIRST
  while(digitalRead(BUTTON_PIN) == LOW) vTaskDelay(10);
  
  // Now start polling for a new short press
  unsigned long start = millis();
  while(millis() - start < ms) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      while(digitalRead(BUTTON_PIN) == LOW) vTaskDelay(10); // debounce release
      lastInteractionTime = millis(); // Reset menu timeout
      return true; // Button was pressed
    }
    vTaskDelay(50);
  }
  return false;
}

void drawPulsingPower(int frame) {
  int cx = OLED_OFFSET_X + 32;
  int cy = OLED_OFFSET_Y + 28;
  float pulse = (sin(frame * 0.2) + 1.0) / 2.0; // 0.0 to 1.0
  int r = 8 + (int)(pulse * 4);
  
  display.drawCircle(cx, cy, r, SSD1306_WHITE);
  display.fillRect(cx - 3, cy - r - 2, 7, 5, SSD1306_BLACK); // Gap
  display.drawLine(cx, cy - r + 1, cx, cy - 2, SSD1306_WHITE); // Stem
}

void uiTask(void *pvParameters) {
  int frame = 0;
  const char* loader = "/|-\\";
  while(1) {
    if (currentState == SS_CONNECTING || currentState == SS_SYNCING || currentState == SS_LOCATING || currentState == SS_SCANNING) {
      if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(50))) {
        display.clearDisplay();
        // Header
        display.fillRect(OLED_OFFSET_X, OLED_OFFSET_Y, OLED_W, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
        display.setCursor(OLED_OFFSET_X + 4, OLED_OFFSET_Y + 1);
        
        String head = "WAITING";
        if (currentState == SS_CONNECTING) head = "WIFI...";
        else if (currentState == SS_SYNCING) head = "CLOUD";
        else if (currentState == SS_LOCATING) head = "GEO-IP";
        else if (currentState == SS_SCANNING) head = "SCANNING";
        display.print(head);

        // Classic Mechanical Spinner
        display.setTextColor(SSD1306_WHITE);
        display.setTextSize(2);
        display.setCursor(OLED_OFFSET_X + 26, OLED_OFFSET_Y + 18);
        display.print(loader[frame % 4]);
        display.setTextSize(1);
        
        display.setCursor(OLED_OFFSET_X + 4, OLED_OFFSET_Y + 40);
        display.print(uiLine1);
        
        display.display();
        xSemaphoreGive(displayMutex);
        frame++;
      }
    }
    vTaskDelay(150 / portTICK_PERIOD_MS);
  }
}

void sendLog(String msg, String level = "INFO") {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/device_logs";
  if (http.begin(client, url)) {
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
    http.addHeader("Content-Type", "application/json");
    JsonDocument doc;
    doc["message"] = msg; doc["level"] = level;
    String body; serializeJson(doc, body);
    http.POST(body); http.end();
  }
}

void updateOLED(String header, String line1, String line2, String line3 = "", const uint8_t* icon = NULL) {
  if (!oledFound) return;
  uiLine1 = line1; uiLine2 = line2; uiLine3 = line3; uiIcon = icon;
  
  if (xSemaphoreTake(displayMutex, portMAX_DELAY)) {
    display.clearDisplay();
    display.fillRect(OLED_OFFSET_X, OLED_OFFSET_Y, OLED_W, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(OLED_OFFSET_X + 4, OLED_OFFSET_Y + 1);
    display.print(header);
    
    if (icon != NULL) {
      display.drawBitmap(OLED_OFFSET_X + OLED_W - 10, OLED_OFFSET_Y + 1, icon, 8, 8, SSD1306_BLACK);
    }

    display.setTextColor(SSD1306_WHITE);
    display.setCursor(OLED_OFFSET_X + 4, OLED_OFFSET_Y + 14); display.print(line1);
    display.setCursor(OLED_OFFSET_X + 4, OLED_OFFSET_Y + 24); display.print(line2);
    if (line3 != "") {
      display.setCursor(OLED_OFFSET_X + 4, OLED_OFFSET_Y + 34); display.print(line3);
    }
    display.display();
    xSemaphoreGive(displayMutex);
  }
}

void drawMenu() {
  if (xSemaphoreTake(displayMutex, portMAX_DELAY)) {
    display.clearDisplay();
    display.fillRect(OLED_OFFSET_X, OLED_OFFSET_Y, OLED_W, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    String headStr = "";
    if (locationSynced) headStr += String(locCity).substring(0, 3);
    else headStr += "LOC";
    headStr.toUpperCase();
    display.setCursor(OLED_OFFSET_X + 4, OLED_OFFSET_Y + 1);
    display.print(headStr);
    
    if (WiFi.status() == WL_CONNECTED) {
      display.drawBitmap(OLED_OFFSET_X + OLED_W - 12, OLED_OFFSET_Y + 1, icon_wifi, 8, 8, SSD1306_BLACK);
    } else {
      display.setCursor(OLED_OFFSET_X + OLED_W - 10, OLED_OFFSET_Y + 1);
      display.print("x");
    }
    
    int startIdx = currentMenuIndex - (VISIBLE_MENU_ITEMS / 2);
    if (startIdx < 0) startIdx = 0;
    if (startIdx > TOTAL_MENU_ITEMS - VISIBLE_MENU_ITEMS) startIdx = TOTAL_MENU_ITEMS - VISIBLE_MENU_ITEMS;

    for (int i = 0; i < VISIBLE_MENU_ITEMS; i++) {
      int itemIdx = startIdx + i;
      int y = OLED_OFFSET_Y + 14 + (i * 10);
      
      if (itemIdx == currentMenuIndex) {
        display.fillRect(OLED_OFFSET_X + 2, y - 1, OLED_W - 4, 9, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      display.setCursor(OLED_OFFSET_X + 4, y);
      display.print(menuItems[itemIdx]);
    }
    
    unsigned long elapsed = millis() - lastInteractionTime;
    int barWidth = map(elapsed, 0, MENU_TIMEOUT, OLED_W - 8, 0);
    display.fillRect(OLED_OFFSET_X + 4, OLED_OFFSET_Y + 45, barWidth, 2, SSD1306_WHITE);
    display.display();
    xSemaphoreGive(displayMutex);
  }
}

bool ensureWiFi(bool showConnected = true) {
  if (WiFi.status() == WL_CONNECTED) return true;
  currentState = SS_CONNECTING;
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
  
  currentState = SS_MENU;
  if (WiFi.status() == WL_CONNECTED) {
    if (showConnected) {
      updateOLED("WIFI", "CONNECTED", WiFi.localIP().toString());
      vTaskDelay(800 / portTICK_PERIOD_MS); 
    }
    return true;
  }
  
  updateOLED("WIFI", "FAILED", "CHECK", "ROUTER");
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  return false;
}

void runLocatePage() {
  if (!ensureWiFi()) return;
  currentState = SS_LOCATING;
  uiLine1 = "FETCHING IP...";
  
  WiFiClient client; HTTPClient http;
  String url = "http://ip-api.com/json/?fields=status,city,lat,lon,offset";
  if (http.begin(client, url)) {
    int code = http.GET();
    currentState = SS_MENU;
    if (code == 200) {
      JsonDocument doc; deserializeJson(doc, http.getString());
      if (doc["status"] == "success") {
        locLat = doc["lat"].as<float>();
        locLon = doc["lon"].as<float>();
        locOffset = doc["offset"].as<long>();
        String city = doc["city"].as<String>();
        strncpy(locCity, city.c_str(), 15);
        locCity[15] = '\0'; 
        locationSynced = true;

        // Save to Permanent NVS
        preferences.begin("aether", false);
        preferences.putBool("locSync", true);
        preferences.putFloat("lat", locLat);
        preferences.putFloat("lon", locLon);
        preferences.putLong("offset", locOffset);
        preferences.putString("city", String(locCity));
        preferences.end();
        
        String cleanCity = String(locCity);
        cleanCity.toUpperCase();
        updateOLED("LOCATE", "FOUND:", cleanCity, "SAVED", icon_pin);
        waitWithButtonPoll(3000);
      } else {
        updateOLED("LOCATE", "API ERR", "FAILED");
        waitWithButtonPoll(3000);
      }
    } else {
      updateOLED("LOCATE", "HTTP ERR", "CODE:" + String(code));
      waitWithButtonPoll(3000);
    }
    http.end();
  }
  currentState = SS_MENU;
}

void showTimePage() {
  if (!locationSynced) {
    updateOLED("CLOCK", "LOCATION", "REQ FIRST", "RUN LOCATE");
    waitWithButtonPoll(3000);
    return;
  }

  if (!ensureWiFi()) return;
  currentState = SS_CONNECTING;
  uiLine1 = "NTP SYNC...";
  
  long tzHours = locOffset / 3600;
  String tzString = "UTC" + String(tzHours > 0 ? "-" : "+") + String(abs(tzHours));
  configTime(locOffset, 0, "pool.ntp.org");
  setenv("TZ", tzString.c_str(), 1); tzset();
  
  struct tm tinfo;
  int retries = 0;
  while (!getLocalTime(&tinfo) && retries < 10) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    retries++;
  }
  currentState = SS_MENU;
  
  unsigned long start = millis();
  String cleanCity = String(locCity);
  cleanCity.toUpperCase();
  
  while (millis() - start < 8000) {
    if (getLocalTime(&tinfo)) {
      char tStr[10], dStr[12], dayStr[16];
      strftime(tStr, 10, "%H:%M:%S", &tinfo);
      strftime(dStr, 12, "%d/%m/%Y", &tinfo);
      strftime(dayStr, 16, "%A", &tinfo);
      String day = String(dayStr); day.toUpperCase();
      updateOLED("CLOCK", tStr, dStr, day);
    }
    if (waitWithButtonPoll(500)) break; // Cancel on button press
  }
}

void showWeatherPage() {
  if (!locationSynced) {
    updateOLED("WEATHER", "LOCATION", "REQ FIRST", "RUN LOCATE");
    waitWithButtonPoll(3000);
    return;
  }

  if (!ensureWiFi()) return;
  currentState = SS_CONNECTING;
  uiLine1 = "WEATHER API...";
  
  #ifdef WEATHER_API_KEY
    WiFiClient client; HTTPClient http;
    String url = "http://api.openweathermap.org/data/2.5/weather?lat=" + String(locLat, 4) + "&lon=" + String(locLon, 4) + "&units=metric&appid=" + String(WEATHER_API_KEY);
    if (http.begin(client, url)) {
      int code = http.GET();
      currentState = SS_MENU;
      if (code == 200) {
        JsonDocument doc; deserializeJson(doc, http.getString());
        float temp = doc["main"]["temp"].as<float>();
        int hum = doc["main"]["humidity"].as<int>();
        String desc = doc["weather"][0]["main"].as<String>();
        
        updateOLED("WEATHER", String(temp, 1) + " C", desc, "HUM: " + String(hum) + "%");
      } else { 
        updateOLED("WEATHER", "API ERR", "CODE:" + String(code)); 
      }
      http.end();
    }
  #else
    updateOLED("WEATHER", "30.5 C", "SUNNY", "KEY REQ");
  #endif
  waitWithButtonPoll(8000); // Wait 8s, cancel on button press
}

void runMeasurementFlow(String trigger) {
  if (trigger == "manual") {
    updateOLED("AETHER", "INITIATING", "SCAN...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  float totalT = 0, totalH = 0, totalA = 0; int totalL = 0, validCount = 0;
  String sampleLog = "Scans: ";
  
  for (int i = 0; i < 5; i++) {
    vTaskDelay(2500 / portTICK_PERIOD_MS); 
    
    setLED(i * 40, 200, 0);
    float t = dht.readTemperature(); float h = dht.readHumidity(); int l = analogRead(LDR_PIN);
    float acc = 0;
    if (mpuFound) {
      sensors_event_t ae, ge, te;
      if (mpu.getEvent(&ae, &ge, &te)) acc = sqrt(sq(ae.acceleration.x) + sq(ae.acceleration.y) + sq(ae.acceleration.z));
    }
    
    String headerStr = "SCAN " + String(i + 1) + "/5";
    if (!isnan(t) && !isnan(h) && h <= 100.0 && t < 60.0) {
      totalT += t; totalH += h; totalL += l; totalA += acc; validCount++;
      sampleLog += "[T:" + String(t, 1) + " H:" + String(h, 0) + "] ";
      updateOLED(headerStr, "T:" + String(t, 1) + "C", "H:" + String(h, 0) + "%", "L:" + String(l));
    } else {
      sampleLog += "[ERR] ";
      updateOLED(headerStr, "SENSOR", "GLITCH", "SKIPPED");
    }
  }

  if (validCount > 0) {
    uiLine1 = "LINKING...";
    if (!ensureWiFi(true)) return; 
    
    currentState = SS_SYNCING;
    uiLine1 = "SYNCING...";
    
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.setReuse(true); 

    // Task 1: Debug Log
    if (http.begin(client, String(SUPABASE_URL) + "/rest/v1/device_logs")) {
      http.addHeader("apikey", SUPABASE_KEY);
      http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
      http.addHeader("Content-Type", "application/json");
      JsonDocument logDoc; logDoc["message"] = sampleLog; logDoc["level"] = "DEBUG";
      String logBody; serializeJson(logDoc, logBody);
      http.POST(logBody);
    }

    // Task 2: Data Upload
    if (http.begin(client, String(SUPABASE_URL) + "/rest/v1/room_readings")) {
      http.addHeader("apikey", SUPABASE_KEY); 
      http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY)); 
      http.addHeader("Content-Type", "application/json");
      JsonDocument doc; 
      doc["temperature"] = totalT / validCount; doc["humidity"] = totalH / validCount;
      doc["ldr_value"] = totalL / validCount; doc["accel_total"] = totalA / validCount;
      doc["trigger_source"] = trigger; doc["battery_v"] = 3.3;
      String body; serializeJson(doc, body);
      int code = http.POST(body);
      if (code >= 200 && code < 300) { 
        measureCount++; 
        preferences.begin("stats", false);
        preferences.putInt("measures", measureCount);
        preferences.end();
        
        currentState = SS_MENU;
        updateOLED("CLOUD", "SYNCED", "SUCCESS", "SAVED", icon_cloud);
        vTaskDelay(1500 / portTICK_PERIOD_MS);
      }
      http.end();
    }
    currentState = SS_MENU;
  } else {
    updateOLED("ERROR", "SENSOR", "FAILED", "NO DATA");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

void runResetStats() {
  updateOLED("RESET", "CLEARING", "HISTORY...");
  bootCount = 0;
  measureCount = 0;
  preferences.begin("stats", false);
  preferences.putInt("boots", 0);
  preferences.putInt("measures", 0);
  preferences.end();
  vTaskDelay(1500 / portTICK_PERIOD_MS);
  updateOLED("RESET", "STATS", "CLEARED");
  vTaskDelay(1500 / portTICK_PERIOD_MS);
}

void showStatsPage() {
  updateOLED("STATS", "MES:" + String(measureCount), "BOT:" + String(bootCount), "UP:" + String(millis()/60000) + "m");
  waitWithButtonPoll(5000); // Cancel on button press
}

void enterDeepSleep() {
  currentState = SS_SLEEPING;
  if (oledFound) {
    if (xSemaphoreTake(displayMutex, portMAX_DELAY)) {
      for (int i = 0; i <= 10; i++) {
        display.clearDisplay();
        
        // Header
        display.fillRect(OLED_OFFSET_X, OLED_OFFSET_Y, OLED_W, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
        display.setCursor(OLED_OFFSET_X + 4, OLED_OFFSET_Y + 1);
        display.print("SLEEPING...");
        
        // Power Icon
        int cx = OLED_OFFSET_X + 32;
        int cy = OLED_OFFSET_Y + 28;
        display.drawCircle(cx, cy, 10, SSD1306_WHITE);
        display.fillRect(cx - 3, cy - 13, 7, 6, SSD1306_BLACK); 
        display.drawLine(cx, cy - 11, cx, cy - 2, SSD1306_WHITE); 
        
        // Progress Bar
        int barW = map(i, 0, 10, 0, 40);
        display.drawRect(OLED_OFFSET_X + 12, OLED_OFFSET_Y + 42, 40, 3, SSD1306_WHITE);
        display.fillRect(OLED_OFFSET_X + 12, OLED_OFFSET_Y + 42, barW, 3, SSD1306_WHITE);
        
        display.display();
        vTaskDelay(100 / portTICK_PERIOD_MS);
      }
      display.clearDisplay();
      display.display();
      xSemaphoreGive(displayMutex);
    }
  }
  
  ledcDetachPin(RED_PIN); ledcDetachPin(GREEN_PIN); ledcDetachPin(BLUE_PIN);
  pinMode(RED_PIN, INPUT); pinMode(GREEN_PIN, INPUT); pinMode(BLUE_PIN, INPUT);
  esp_sleep_enable_timer_wakeup(300 * 1000000ULL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BUTTON_PIN, 0);
  esp_deep_sleep_start();
}

void monitorTask(void *pvParameters) {
  // Initialize NVS and Load Persistent Stats & Location
  preferences.begin("stats", false);
  bootCount = preferences.getInt("boots", 0) + 1;
  measureCount = preferences.getInt("measures", 0);
  preferences.putInt("boots", bootCount);
  preferences.end();
  
  preferences.begin("aether", true);
  locationSynced = preferences.getBool("locSync", false);
  if (locationSynced) {
    locLat = preferences.getFloat("lat", 0.0);
    locLon = preferences.getFloat("lon", 0.0);
    locOffset = preferences.getLong("offset", 28800);
    String city = preferences.getString("city", "UNKNOWN");
    strncpy(locCity, city.c_str(), 15);
    locCity[15] = '\0';
  }
  preferences.end();

  pinMode(OLED_RST, OUTPUT); digitalWrite(OLED_RST, LOW); vTaskDelay(50); digitalWrite(OLED_RST, HIGH);
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) oledFound = true;
  else if (display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) oledFound = true;
  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
  if (reason == ESP_SLEEP_WAKEUP_EXT0) {
    lastInteractionTime = millis();
    while (millis() - lastInteractionTime < MENU_TIMEOUT) {
      setLED(0, 10, 20); drawMenu();
      if (digitalRead(BUTTON_PIN) == LOW) {
        unsigned long pressStart = millis(); bool longPressed = false;
        while (digitalRead(BUTTON_PIN) == LOW) {
          if (millis() - pressStart > LONG_PRESS_MS && !longPressed) {
            longPressed = true; setLED(0, 255, 0); vTaskDelay(100 / portTICK_PERIOD_MS);
            if (currentMenuIndex == PAGE_MEASURE) runMeasurementFlow("manual");
            else if (currentMenuIndex == PAGE_TIME) showTimePage();
            else if (currentMenuIndex == PAGE_WEATHER) showWeatherPage();
            else if (currentMenuIndex == PAGE_LOCATE) runLocatePage();
            else if (currentMenuIndex == PAGE_STATS) showStatsPage();
            else if (currentMenuIndex == PAGE_RESET) runResetStats();
            else if (currentMenuIndex == PAGE_SLEEP) enterDeepSleep();
            lastInteractionTime = millis();
          }
          vTaskDelay(10);
        }
        if (!longPressed && (millis() - pressStart > 50)) { currentMenuIndex = (currentMenuIndex + 1) % TOTAL_MENU_ITEMS; lastInteractionTime = millis(); }
      }
      vTaskDelay(50 / portTICK_PERIOD_MS);
    }
  } else { runMeasurementFlow("auto"); }
  enterDeepSleep();
}

void setup() {
  Serial.begin(115200); Wire.begin(21, 22); pinMode(BUTTON_PIN, INPUT_PULLUP);
  displayMutex = xSemaphoreCreateMutex();
  
  dht.begin();
  if (mpu.begin()) mpuFound = true;

  ledcSetup(RED_CH, 5000, 8); ledcAttachPin(RED_PIN, RED_CH);
  ledcSetup(GREEN_CH, 5000, 8); ledcAttachPin(GREEN_PIN, GREEN_CH);
  ledcSetup(BLUE_CH, 5000, 8); ledcAttachPin(BLUE_PIN, BLUE_CH);
  
  xTaskCreatePinnedToCore(uiTask, "UI", 4096, NULL, 1, NULL, 0); // Core 0 for Painter
  xTaskCreatePinnedToCore(monitorTask, "Monitor", 16384, NULL, 1, NULL, 1); // Core 1 for Logic
}
void loop() {}
