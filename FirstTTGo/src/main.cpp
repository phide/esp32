#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* ssid = "XXXXXX";
const char* password =  "XXXXXX";

unsigned long previousMillis = 0; 
const long interval = 600000; // 10 Minuten in Millisekunden
//const long interval = 10000; // 10 Sekunden in Millisekunden

void makeApiCall() {
  if ((WiFi.status() == WL_CONNECTED)) {
    HTTPClient http;

    http.begin("https://api.airvisual.com/v2/city?city=Hamburg%20City&state=Hamburg&country=Germany&key=XXXXXX");
    int httpCode = http.GET();

    if (httpCode > 0) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);

      // Zugriff auf die aktuellen Verschmutzungswerte
      JsonObject current = doc["data"]["current"]["pollution"];
      int aqius = current["aqius"]; // AQI-Wert basierend auf US EPA-Standard
      int aqicn = current["aqicn"]; // AQI-Wert basierend auf China MEP-Standard
      JsonObject p1 = current["p1"];
      int conc = p1["conc"]; // Konzentration des Schadstoffs
      int aqius_p1 = p1["aqius"]; // AQI des Schadstoffs basierend auf US EPA-Standard
      int aqicn_p1 = p1["aqicn"]; // AQI des Schadstoffs basierend auf China MEP-Standard

      Serial.println("US AQI: " + String(aqius));
      Serial.println("China AQI: " + String(aqicn));
      Serial.println("Schadstoffkonzentration: " + String(conc));
      Serial.println("US AQI des Schadstoffs: " + String(aqius_p1));
      Serial.println("China AQI des Schadstoffs: " + String(aqicn_p1));
      tft.setCursor(0, 20);
      tft.setTextSize(3);
      tft.println("AQI Wert: " + String(aqius));
    }

    http.end();
  }
}

void setup() {
  Serial.begin(9600);
  delay(4000);
  WiFi.begin(ssid, password);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(0, 0);
  tft.println("Connecting to WiFi " + String(ssid) + "...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  tft.fillScreen(TFT_BLACK);
  Serial.println("Connected to WiFi");

  tft.setCursor(0, 0);
  tft.println("Connected to WiFi");

  makeApiCall();
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    makeApiCall(); // API-Aufruf alle 10 Minuten
  }

  // Berechnen Sie die verbleibende Zeit bis zum nächsten API-Aufruf
  unsigned long remainingTime = interval - (currentMillis - previousMillis);
  tft.setCursor(0, 60);
  tft.setTextSize(1);
  tft.println("Refresh: " + String(remainingTime / 1000) + " Sekunden");
}