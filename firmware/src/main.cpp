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
#include "secrets.h"

// --- Pin Definitions ---
#define DHTPIN 4
#define DHTTYPE DHT11
#define BUTTON_PIN GPIO_NUM_33 
#define LDR_PIN 34
#define RED_PIN 13
#define GREEN_PIN 14
#define BLUE_PIN 27
#define OLED_RST 16 

// --- OLED Config ---
#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64
#define OLED_OFFSET 32    
// Using -1 for reset to prevent hardware hang
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// LEDC Channels
#define RED_CH 0
#define GREEN_CH 1
#define BLUE_CH 2

// --- Configuration ---
#define SLEEP_TIME_SEC 300 
#define READINGS_COUNT 5

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

void sendLog(String msg, String level = "INFO") {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String(SUPABASE_URL) + "/rest/v1/device_logs";
  if (http.begin(client, url)) {
    http.addHeader("apikey", SUPABASE_KEY);
    http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
    http.addHeader("Content-Type", "application/json");
    JsonDocument doc;
    doc["message"] = msg;
    doc["level"] = level;
    String body; serializeJson(doc, body);
    http.POST(body);
    http.end();
  }
}

void updateOLED(String line1, String line2, String line3) {
  if (!oledFound) return;
  display.clearDisplay();
  display.drawRect(OLED_OFFSET, 0, 64, 48, SSD1306_WHITE);
  display.setCursor(OLED_OFFSET + 4, 4); display.print(line1);
  display.setCursor(OLED_OFFSET + 4, 18); display.print(line2);
  display.setCursor(OLED_OFFSET + 4, 32); display.print(line3);
  display.display();
}

void monitorTask(void *pvParameters) {
  Serial.println("[SYSTEM] monitorTask Starting...");
  
  // Hardware OLED Reset
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW); vTaskDelay(100 / portTICK_PERIOD_MS);
  digitalWrite(OLED_RST, HIGH);

  // Try 0x3C and then 0x3D for OLED
  if(display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    oledFound = true;
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    updateOLED("AETHER", "LINKED", "0x3C");
    Serial.println("[OLED] Found at 0x3C");
  } else if(display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
    oledFound = true;
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    updateOLED("AETHER", "LINKED", "0x3D");
    Serial.println("[OLED] Found at 0x3D");
  } else {
    Serial.println("[OLED] Display NOT FOUND on I2C bus.");
  }

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool isButtonWake = (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0);
  String trigger = isButtonWake ? "manual" : "auto";

  dht.begin();
  mpuFound = mpu.begin();

  // WiFi Connection
  updateOLED("WiFi", "Connecting", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    setLED(0, 50, 50); vTaskDelay(250 / portTICK_PERIOD_MS);
    setLED(0, 0, 0); vTaskDelay(250 / portTICK_PERIOD_MS);
  }

  if (WiFi.status() == WL_CONNECTED) {
    sendLog("Device Wakeup [" + trigger + "]", "INFO");
    sendLog(oledFound ? "OLED Screen: DETECTED" : "OLED Screen: NOT FOUND", oledFound ? "INFO" : "WARN");
    sendLog(mpuFound ? "Motion Sensor: DETECTED" : "Motion Sensor: NOT FOUND", mpuFound ? "INFO" : "WARN");
    updateOLED("WiFi", "Connected", WiFi.localIP().toString());
  }

  float totalT = 0, totalH = 0, totalA = 0;
  int totalL = 0, validCount = 0;

  for (int i = 0; i < READINGS_COUNT; i++) {
    setLED(i*40, 200, 0); 
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int l = analogRead(LDR_PIN);
    float acc = 0;
    
    if (mpuFound) {
      sensors_event_t ae, ge, te;
      if (mpu.getEvent(&ae, &ge, &te)) {
        acc = sqrt(sq(ae.acceleration.x) + sq(ae.acceleration.y) + sq(ae.acceleration.z));
      }
    }

    if (!isnan(t) && !isnan(h)) {
      totalT += t; totalH += h; totalL += l; totalA += acc;
      validCount++;
      String dataStr = "T:" + String(t,1) + "C H:" + String(h,0) + "% L:" + String(l);
      sendLog("Sample [" + String(i+1) + "/5]: " + dataStr, "INFO");
      updateOLED("SAMP " + String(i+1) + "/5", String(t, 1) + " C", String(h, 0) + " %");
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }

  if (validCount > 0 && WiFi.status() == WL_CONNECTED) {
    float avgT = totalT / validCount;
    float avgH = totalH / validCount;
    int avgL = totalL / validCount;
    float avgA = totalA / validCount;

    updateOLED("CLOUD", "UPLOADING", "...");
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String(SUPABASE_URL) + "/rest/v1/room_readings";
    if (http.begin(client, url)) {
      http.addHeader("apikey", SUPABASE_KEY);
      http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
      http.addHeader("Content-Type", "application/json");
      JsonDocument doc;
      doc["temperature"] = avgT; doc["humidity"] = avgH; doc["ldr_value"] = avgL;
      doc["accel_total"] = avgA; doc["trigger_source"] = trigger; doc["battery_v"] = 3.3;
      String body; serializeJson(doc, body);
      int code = http.POST(body);
      sendLog("Cloud Data Sync Response: " + String(code), (code >= 200 && code < 300) ? "SUCCESS" : "ERROR");
      http.end();
    }
  }

  sendLog("Sequence Complete. Deep Sleep starting.", "INFO");
  updateOLED("DONE!", "Sleeping", "Zzz...");
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  
  if (oledFound) { display.clearDisplay(); display.display(); }
  
  ledcDetachPin(RED_PIN); ledcDetachPin(GREEN_PIN); ledcDetachPin(BLUE_PIN);
  pinMode(RED_PIN, INPUT); pinMode(GREEN_PIN, INPUT); pinMode(BLUE_PIN, INPUT);

  rtc_gpio_init(BUTTON_PIN);
  rtc_gpio_set_direction(BUTTON_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(BUTTON_PIN);

  esp_sleep_enable_timer_wakeup(SLEEP_TIME_SEC * 1000000ULL);
  esp_sleep_enable_ext0_wakeup(BUTTON_PIN, 0);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); 
  
  ledcSetup(RED_CH, 5000, 8); ledcAttachPin(RED_PIN, RED_CH);
  ledcSetup(GREEN_CH, 5000, 8); ledcAttachPin(GREEN_PIN, GREEN_CH);
  ledcSetup(BLUE_CH, 5000, 8); ledcAttachPin(BLUE_PIN, BLUE_CH);

  xTaskCreatePinnedToCore(monitorTask, "MonitorTask", 16384, NULL, 1, NULL, 1);
}

void loop() {}
