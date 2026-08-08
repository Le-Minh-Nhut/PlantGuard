#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "board_config.h"

const char* ssid = "Sai 3G di ma";
const char* password = "50thicho";

const char* serverUrl =
  "http://192.168.1.28:8000/upload-image/";

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Giảm khả năng Wi-Fi ngủ giữa lúc gửi ảnh
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");

  Serial.print("Camera IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());

  Serial.print("WiFi RSSI: ");
  Serial.println(WiFi.RSSI());

  // Phải khởi tạo bằng {} để các trường chưa dùng bằng 0
  camera_config_t config = {};

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Test ảnh nhỏ trước để tránh lỗi send payload
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t cameraResult = esp_camera_init(&config);

  if (cameraResult != ESP_OK) {
    Serial.printf(
      "Camera init failed: 0x%x\n",
      cameraResult
    );

    while (true) {
      delay(1000);
    }
  }

  Serial.println("Camera initialized!");
}

void loop() {
  Serial.println("Capturing image...");

  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Camera capture failed");
    delay(2000);
    return;
  }

  Serial.printf(
    "Captured %u bytes | Free heap: %u\n",
    fb->len,
    ESP.getFreeHeap()
  );

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;

    client.setTimeout(20);

    http.setConnectTimeout(5000);
    http.setTimeout(20000);
    http.setReuse(false);

    Serial.print("Sending to: ");
    Serial.println(serverUrl);

    if (!http.begin(client, serverUrl)) {
      Serial.println("HTTP begin failed");
    } else {
      http.addHeader("Content-Type", "image/jpeg");

      int code = http.POST(fb->buf, fb->len);

      if (code > 0) {
        Serial.printf("HTTP POST code: %d\n", code);
        Serial.println(http.getString());
      } else {
        Serial.printf(
          "HTTP failed %d: %s\n",
          code,
          http.errorToString(code).c_str()
        );
      }

      http.end();
    }
  } else {
    Serial.println("WiFi disconnected");
  }

  esp_camera_fb_return(fb);

  delay(10000);
}