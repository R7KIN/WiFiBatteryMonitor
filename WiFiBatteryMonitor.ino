#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

// Настройки Wi-Fi
const char* WIFI_SSID     = "SSID";
const char* WIFI_PASSWORD = "PASSWORD";

// Настройки ThingSpeak
const char* THINGSPEAK_SERVER = "http://api.thingspeak.com/update";
const String WRITE_API_KEY    = "API_KEY";

// Интервал отправки данных (30 секунд; минимум ThingSpeak — 15000 мс)
const unsigned long SEND_INTERVAL_MS = 30000;

// Делитель: верхнее плечо 1300k (доп.) + 220k (встроенное) = 1520k, нижнее = 100k -> 16.2
const float DIVIDER_RATIO       = 16.2f;
float CALIBRATION_FACTOR        = 1.0241f; // Подстройте по мультиметру
const float LOW_VOLTAGE_ALARM_V = 11.50f;

// Диапазон оценки заряда свинцового АКБ 12В (10.5 В -> 0%, 12.75 В -> 100%)
const float V_MIN_SOC = 10.50f;
const float V_MAX_SOC = 12.75f;

unsigned long lastSendTime = 0;

int readAveragedAdc(int samples = 32) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(A0);
    delay(3);
  }
  return (int)(sum / samples);
}

float readBatteryVoltage() {
  int raw = readAveragedAdc(32);
  float pinVoltage = (raw / 1023.0f) * 1.0f;
  return pinVoltage * DIVIDER_RATIO * CALIBRATION_FACTOR;
}

int estimateBatteryPercent(float voltage) {
  if (voltage <= V_MIN_SOC) return 0;
  if (voltage >= V_MAX_SOC) return 100;
  float pct = ((voltage - V_MIN_SOC) / (V_MAX_SOC - V_MIN_SOC)) * 100.0f;
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return (int)roundf(pct);
}

void printStatus(float voltage, int percent, int rawAdc) {
  Serial.printf("[%lu мс] ADC: %d | АКБ: %.2f В | Заряд (оценка): %d%%\n",
                millis(), rawAdc, voltage, percent);
  if (voltage < LOW_VOLTAGE_ALARM_V) {
    Serial.printf("ВНИМАНИЕ: Напряжение АКБ ниже %.2f В (текущее: %.2f В)!\n",
                  LOW_VOLTAGE_ALARM_V, voltage);
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.print(F("Подключение к Wi-Fi: "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_MODEM_SLEEP);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 20000) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Wi-Fi подключен, IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("Не удалось подключиться к Wi-Fi"));
  }
}

void sendToThingSpeak(float voltage, int percent) {
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Отправка отменена: нет подключения к Wi-Fi"));
    return;
  }

  WiFiClient client;
  HTTPClient http;

  String url = String(THINGSPEAK_SERVER) +
               "?api_key=" + WRITE_API_KEY +
               "&field1=" + String(voltage, 2) +
               "&field2=" + String(percent);

  Serial.print(F("Запрос ThingSpeak: "));
  Serial.println(url);

  http.setTimeout(8000);
  if (http.begin(client, url)) {
    int httpCode = http.GET();
    if (httpCode > 0) {
      Serial.print(F("HTTP код: "));
      Serial.print(httpCode);
      Serial.print(F(" | Ответ ThingSpeak: "));
      Serial.println(http.getString());
    } else {
      Serial.print(F("Ошибка HTTP: "));
      Serial.println(http.errorToString(httpCode));
    }
    http.end();
  } else {
    Serial.println(F("Ошибка инициализации HTTPClient"));
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("=== Мониторинг АКБ 12В на ESP8266 NodeMCU v0.0001a by R7KIN 2026 ==="));

  WiFi.setSleepMode(WIFI_MODEM_SLEEP);
  connectWiFi();

  int rawAdc = readAveragedAdc(32);
  float vBat = readBatteryVoltage();
  int soc    = estimateBatteryPercent(vBat);

  printStatus(vBat, soc, rawAdc);
  sendToThingSpeak(vBat, soc);
  lastSendTime = millis();
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleepMode(WIFI_MODEM_SLEEP);
  }

  if (millis() - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = millis();

    int rawAdc = readAveragedAdc(32);
    float vBat = readBatteryVoltage();
    int soc    = estimateBatteryPercent(vBat);

    printStatus(vBat, soc, rawAdc);
    sendToThingSpeak(vBat, soc);
  }

  delay(50);
}
