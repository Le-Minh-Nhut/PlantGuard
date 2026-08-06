#include <WiFi.h>
#include <HTTPClient.h>
#include "DHT.h"

#define DHTPIN 27      
#define DHTTYPE DHT11
#define SOIL_PIN 33
DHT dht(DHTPIN, DHTTYPE);

#define RELAY_PIN 26    

// Ngưỡng dưới: Bật bơm nếu độ ẩm đất < giá trị này
const int SOIL_MOISTURE_LOW_THRESHOLD = 30;

// Dải trễ (Hysteresis): Bơm sẽ chỉ tắt khi độ ẩm vượt qua (Ngưỡng dưới + Dải trễ)
// Giúp tránh bật/tắt liên tục khi độ ẩm dao động quanh ngưỡng
const int SOIL_MOISTURE_HYSTERESIS = 10; 

// Thời gian chờ (cooldown) giữa các lần bật/tắt (tính bằng mili giây)
const unsigned long COOLDOWN_PERIOD_MS = 5 * 60 * 1000; 

bool isRelayOn = false;                
unsigned long lastSwitchTime = 0;

const char* ssid = "Sai 3G di ma";
const char* password = "50thicho";
const char* serverName = "http://192.168.1.28:8000/api/data";

void setup() {
  Serial.begin(115200);

  analogReadResolution(12);
  pinMode(SOIL_PIN, INPUT);

  dht.begin();

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // LOW = Tắt (Nếu relay kích hoạt mức cao)
  isRelayOn = false;
  Serial.println("Relay is OFF");
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected!");
}

void loop() {                                                                                                                                                                                                                
  delay(2000);
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();
  int soil_analog = analogRead(SOIL_PIN);
  int soil_moisture = map(
    soil_analog,
    3500,  // giá trị khi khô, tạm thời
    1400,  // giá trị khi ướt, tạm thời
    0,
    100
  );

  soil_moisture = constrain(soil_moisture, 0, 100);

  Serial.printf(
    "Soil raw: %d | Soil moisture: %d%%\n",
    soil_analog,
    soil_moisture
  );
  if (isnan(temp) || isnan(hum)) {
    Serial.println("DHT read failed");
    return;
  }
  // if (isnan(soil_analog)){
  //   Serial.println("Soil_moisture read failed");
  //   return;
  // }

  unsigned long currentTime = millis();

  if (currentTime - lastSwitchTime >= COOLDOWN_PERIOD_MS) {
    if (!isRelayOn) {
      if (soil_moisture < SOIL_MOISTURE_LOW_THRESHOLD) {
        digitalWrite(RELAY_PIN, HIGH);
        isRelayOn = true;
        lastSwitchTime = currentTime;
        Serial.println(">>> PUMP ON (Soil is too dry)");
      }
    }
    else {
      // Bơm chỉ tắt khi độ ẩm vượt qua ngưỡng cao (ngưỡng dưới + dải trễ)
      if (soil_moisture > (SOIL_MOISTURE_LOW_THRESHOLD + SOIL_MOISTURE_HYSTERESIS)) {
        digitalWrite(RELAY_PIN, LOW); // Tắt bơm
        isRelayOn = false;
        lastSwitchTime = currentTime;
        Serial.println("<<< PUMP OFF (Soil is moist enough)");
      }
    }
  }


  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"temperature\": " + String(temp) + ", \"humidity\": " + String(hum) + ",\"soilMoisture\": "+ String(soil_moisture)+"}";
    int code = http.POST(payload);

    Serial.printf(
      "HTTP %d: %s\n",
      code,
      http.errorToString(code).c_str()
    );
    http.end();
  } else {
    Serial.println("WiFi lost");
  }
}
