#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "board_config.h"
#include <WebSocketsClient.h>

const char* ssid = "Sai 3G di ma";
const char* password = "50thicho";

// const char* serverUrl =
//   "http://192.168.1.28:8000/upload-image/";
const char* backendHost = "192.168.1.28";
const uint16_t backendPort = 8000;
const char* backendPath = "/ws/camera";
const char* s3Url      = "http://192.168.1.22/frame";


const unsigned long AI_INTERVAL_MS = 15000;
unsigned long lastAiSent = 0;

WebSocketsClient cameraWs;
bool wsConnected = false;
const unsigned long LIVE_INTERVAL_MS = 250;
unsigned long lastLiveSent = 0;

int postJpeg(const char* url, uint8_t* data, size_t len) {
    WiFiClient client;
    HTTPClient http;

    client.setTimeout(5000);

    http.setConnectTimeout(3000);
    http.setTimeout(5000);
    http.setReuse(false);

    if (!http.begin(client, url)) {
        Serial.printf("HTTP begin FAILED: %s\n", url);
        return -100;
    }

    http.addHeader("Content-Type", "image/jpeg");
    int code = http.POST(data, len);
    if (code > 0) {
        Serial.printf("POST %s -> HTTP %d\n", url, code);
    } else {
        Serial.printf(
            "POST %s -> ERROR %d: %s\n",
            url,
            code,
            HTTPClient::errorToString(code).c_str()
        );
    }
    
    http.end();
    client.stop();
    return code;
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            wsConnected = true;
            Serial.println("LIVE WS -> CONNECTED");
            break;

        case WStype_DISCONNECTED:
            wsConnected = false;
            Serial.println("LIVE WS -> DISCONNECTED");
            break;

        case WStype_ERROR:
            wsConnected = false;
            Serial.println("LIVE WS -> ERROR");
            break;

        case WStype_PING:
            Serial.println("LIVE WS <- PING");
            break;

        case WStype_PONG:
            break;

        default:
            break;
    }
}

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
  config.frame_size = FRAMESIZE_VGA;
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
  cameraWs.begin(backendHost, backendPort, backendPath);
  cameraWs.onEvent(webSocketEvent);
  cameraWs.setReconnectInterval(2000);
  cameraWs.enableHeartbeat(15000, 3000, 2);

  Serial.println("Camera WebSocket initialized");
  Serial.println("Testing ESP32-S3 TCP connection...");

  WiFiClient testClient;
  if (testClient.connect("192.168.1.28", 8000)) {
      Serial.println("CAM -> LAPTOP TCP: SUCCESS");
      testClient.stop();
  } else {
      Serial.println("CAM -> LAPTOP TCP: FAILED");
  }

  if (testClient.connect("192.168.1.22", 80)) {
      Serial.println("TCP -> ESP32-S3: SUCCESS");
      testClient.stop();
  } else {
      Serial.println("TCP -> ESP32-S3: FAILED");
  }
  Serial.print("Camera IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("Subnet: ");
  Serial.println(WiFi.subnetMask());

  Serial.print("Gateway: ");
  Serial.println(WiFi.gatewayIP());

  Serial.print("BSSID: ");
  Serial.println(WiFi.BSSIDstr());

  Serial.print("Channel: ");
  Serial.println(WiFi.channel());
}

void loop() {
    cameraWs.loop();

    unsigned long now = millis();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected");
        delay(10);
        return;
    }

    bool liveDue = wsConnected && (lastLiveSent == 0 || now - lastLiveSent >= LIVE_INTERVAL_MS);
    bool aiDue = lastAiSent == 0 || now - lastAiSent >= AI_INTERVAL_MS;
    if (!liveDue && !aiDue) {
        delay(1);
        return;
    }

    camera_fb_t* fb = esp_camera_fb_get();

    if (!fb) {
        Serial.println("Camera capture failed");
        delay(10);
        return;
    }

    if (liveDue) {
        lastLiveSent = now;
        bool sent = cameraWs.sendBIN(fb->buf,fb->len);

        Serial.printf(
            "LIVE | %u bytes | WS=%s | heap=%u\n",
            fb->len,
            sent ? "OK" : "FAIL",
            ESP.getFreeHeap()
        );
    }

    if (aiDue) {
        lastAiSent = now;
        int aiCode = postJpeg(s3Url,fb->buf, fb->len);
        Serial.printf("AI FRAME -> S3 | HTTP=%d\n", aiCode);
    }

    esp_camera_fb_return(fb);

    delay(1);
}