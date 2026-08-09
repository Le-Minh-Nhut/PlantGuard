#include <cstring>

// ->Hệ điều hành nhỏ chạy bên trong ESP32
#include "freertos/FreeRTOS.h" 
// Tạo cờ/event để các task báo hiệu cho nhau 
#include "freertos/event_groups.h"
// -> Nhận event như Wi-Fi connected/disconnected
#include "esp_event.h"
// -> Cấp phát RAM/PSRAM
#include "esp_heap_caps.h"
// -> Tạo HTTP server /infe
#include "esp_http_server.h"
// -> In log ESP_LOGI, ESP_LOGE,...
#include "esp_log.h"
// -> Quản lý network interface/IP
#include "esp_netif.h"
// -> Điều khiển Wi-Fi
#include "esp_wifi.h"
// -> Bộ nhớ persistent trong flash
#include "nvs_flash.h"
#include "secrets.h"
#include "dl_model_base.hpp"
#include "yolo26.hpp"
#include "plantguard_classes.hpp"
#include "esp_timer.h"
#include <cstdio>
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "PlantGuard";

extern const uint8_t model_binary_start[] asm(MODEL_SYMBOL_STR);
static dl::Model *g_model = nullptr;
static YOLO26 *g_yolo = nullptr;

struct FramePacket {
    uint8_t *data;
    size_t len;
    uint64_t frame_id;
    int64_t timestamp_us;
};

static SemaphoreHandle_t g_frame_mutex = nullptr;
static TaskHandle_t g_inference_task_handle = nullptr;

static FramePacket *g_latest_frame = nullptr;
static uint64_t g_frame_counter = 0;

static SemaphoreHandle_t g_ai_mutex = nullptr;

// static const char *WIFI_SSID_VALUE = WIFI_SSID;
// static const char *WIFI_PASSWORD_VALUE = WIFI_PASSWORD;


static EventGroupHandle_t wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0


static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected. Reconnecting...");

        esp_wifi_connect();
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto *event =
            static_cast<ip_event_got_ip_t *>(event_data);

        ESP_LOGI(
            TAG,
            "ESP32-S3 IP: " IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}



static void init_wifi()
{
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifi_init_config =WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, nullptr));
    wifi_config_t wifi_config = {};
    std::strncpy(
        reinterpret_cast<char *>(wifi_config.sta.ssid),
        WIFI_SSID,
        sizeof(wifi_config.sta.ssid) - 1
    );

    std::strncpy(
        reinterpret_cast<char *>(wifi_config.sta.password),
        WIFI_PASSWORD,
        sizeof(wifi_config.sta.password) - 1
    );

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to Wi-Fi...");

    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );

    ESP_LOGI(TAG, "Wi-Fi connected");
}

static void free_frame(FramePacket *frame)
{
    if (frame == nullptr) {
        return;
    }

    if (frame->data != nullptr) {
        heap_caps_free(frame->data);
    }

    delete frame;
}

static void inference_task(void *arg)
{
    while (true) {

        // Ngủ cho tới khi /frame báo có ảnh mới.
        ulTaskNotifyTake(
            pdTRUE,
            portMAX_DELAY
        );

        FramePacket *frame = nullptr;

        // Lấy frame mới nhất ra.
        xSemaphoreTake(
            g_frame_mutex,
            portMAX_DELAY
        );

        frame = g_latest_frame;
        g_latest_frame = nullptr;

        xSemaphoreGive(g_frame_mutex);

        if (frame == nullptr) {
            continue;
        }

        ESP_LOGI(
            TAG,
            "AI processing frame #%llu (%u bytes)",
            frame->frame_id,
            static_cast<unsigned>(frame->len)
        );
        xSemaphoreTake(g_ai_mutex, portMAX_DELAY);
        // JPEG -> RGB
        auto img =
            g_yolo->decode_jpeg(
                frame->data,
                frame->len
            );

        // JPEG compressed không cần nữa.
        heap_caps_free(frame->data);
        frame->data = nullptr;

        if (img.data == nullptr) {
            ESP_LOGE(TAG, "JPEG decode failed");
            xSemaphoreGive(g_ai_mutex);
            free_frame(frame);
            continue;
        }

        // AI
        int64_t start = esp_timer_get_time();

        g_yolo->preprocess(img);
        heap_caps_free(img.data);
        img.data = nullptr;
        g_model->run();
        auto detections =
            g_yolo->postprocess(
                g_model->get_outputs()
            );
        xSemaphoreGive(g_ai_mutex);

        float inference_ms =
            (esp_timer_get_time() - start) / 1000.0f;

        ESP_LOGI(
            TAG,
            "Frame #%llu | AI %.2f ms | detections=%u",
            frame->frame_id,
            inference_ms,
            static_cast<unsigned>(detections.size())
        );

        for (const auto &res : detections) {
            if (
                res.category >= 0 &&
                res.category < PLANTGUARD_NUM_CLASSES &&
                res.box.size() >= 4
            ) {
                ESP_LOGI(
                    TAG,
                    "[%s] score=%.3f bbox=[%d,%d,%d,%d]",
                    plantguard_classes[res.category],
                    res.score,
                    res.box[0],
                    res.box[1],
                    res.box[2],
                    res.box[3]
                );
            }
        }

        free_frame(frame);
    }
}


static esp_err_t infer_handler(httpd_req_t *request)
{
    size_t image_size = request->content_len;
    ESP_LOGI(TAG, "Incoming image: %u bytes", static_cast<unsigned>(image_size));

    if (image_size == 0 ||image_size > 512 * 1024) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid image size");
        return ESP_FAIL;
    }


    // Allocate JPEG buffer trong PSRAM.
    uint8_t *jpeg_buffer = static_cast<uint8_t *>(
            heap_caps_malloc(
                image_size,
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
            )
        );

    if (jpeg_buffer == nullptr) {
        ESP_LOGE(TAG, "Cannot allocate PSRAM");

        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot allocate memory");

        return ESP_FAIL;
    }


    // Nhận toàn bộ HTTP body.
    size_t received = 0;

    while (received < image_size) {
        int result = httpd_req_recv(request, reinterpret_cast<char *>(jpeg_buffer + received), image_size - received);

        if (result <= 0) {
            ESP_LOGE(TAG, "Failed receiving JPEG");
            heap_caps_free(jpeg_buffer);
            return ESP_FAIL;
        }

        received += result;
    }


    // JPEG thường:
    // start = FF D8
    // end   = FF D9
    bool valid_jpeg =
        image_size >= 4 &&
        jpeg_buffer[0] == 0xFF &&
        jpeg_buffer[1] == 0xD8 &&
        jpeg_buffer[image_size - 2] == 0xFF &&
        jpeg_buffer[image_size - 1] == 0xD9;


    ESP_LOGI(
        TAG,
        "Received: %u bytes | JPEG: %s",
        static_cast<unsigned>(received),
        valid_jpeg ? "YES" : "NO"
    );

    ESP_LOGI(
        TAG,
        "Free PSRAM: %u bytes",
        static_cast<unsigned>(
            heap_caps_get_free_size(
                MALLOC_CAP_SPIRAM
            )
        )
    );


    // httpd_resp_set_type(request, "application/json");

    // if (valid_jpeg) {
    //     httpd_resp_sendstr(
    //         request,
    //         "{\"status\":\"ok\",\"jpeg\":true}"
    //     );
    // } else {
    //     httpd_resp_sendstr(
    //         request,
    //         "{\"status\":\"ok\",\"jpeg\":false}"
    //     );
    // }

    // return ESP_OK;
    if (!valid_jpeg) {
        heap_caps_free(jpeg_buffer);
        httpd_resp_set_type(request, "application/json");
        httpd_resp_sendstr(
            request,
            "{\"status\":\"error\",\"message\":\"invalid jpeg\"}"
        );

        return ESP_OK;
    }

    xSemaphoreTake(g_ai_mutex, portMAX_DELAY);
    // 1. JPEG -> RGB888

    ESP_LOGI(TAG, "Decoding JPEG...");
    int64_t decode_start = esp_timer_get_time();
    dl::image::img_t img = g_yolo->decode_jpeg(jpeg_buffer, image_size);
    int64_t decode_end = esp_timer_get_time();


    heap_caps_free(jpeg_buffer);
    jpeg_buffer = nullptr;


    if (img.data == nullptr) {
        ESP_LOGE(TAG, "JPEG decode failed");
        xSemaphoreGive(g_ai_mutex);

        httpd_resp_set_type(request, "application/json");
        httpd_resp_sendstr(
            request,
            "{\"status\":\"error\",\"message\":\"jpeg decode failed\"}"
        );
        return ESP_FAIL;
    }

    // 2. RGB888 -> letterbox -> INT8 model input
    ESP_LOGI(TAG, "Preprocessing image...");
    int64_t preprocess_start = esp_timer_get_time();
    g_yolo->preprocess(img);
    int64_t preprocess_end = esp_timer_get_time();


    // Theo README YOLO26:
    // preprocess đã copy/quantize dữ liệu vào model input RAM.
    // RGB decode buffer bây giờ có thể free.
    heap_caps_free(img.data);
    img.data = nullptr;


    // 3. YOLO26 inference
    ESP_LOGI(TAG, "Running YOLO26 inference...");
    int64_t inference_start = esp_timer_get_time();
    g_model->run();
    int64_t inference_end = esp_timer_get_time();


    // 4. Postprocess
    ESP_LOGI(TAG, "Postprocessing...");
    int64_t postprocess_start = esp_timer_get_time();

    auto detections =
        g_yolo->postprocess(
            g_model->get_outputs()
        );

    int64_t postprocess_end = esp_timer_get_time();

    xSemaphoreGive(g_ai_mutex);


    // 5. Latency
    float decode_ms =(decode_end - decode_start) / 1000.0f;
    float preprocess_ms = (preprocess_end - preprocess_start) / 1000.0f;
    float inference_ms = (inference_end - inference_start) / 1000.0f;
    float postprocess_ms = (postprocess_end - postprocess_start) / 1000.0f;


    ESP_LOGI(
        TAG,
        "Latency | decode=%.2f ms | preprocess=%.2f ms | inference=%.2f ms | postprocess=%.2f ms",
        decode_ms,
        preprocess_ms,
        inference_ms,
        postprocess_ms
    );


    // 6. Print detections
    ESP_LOGI(TAG, "Detections: %u", static_cast<unsigned>(detections.size()));

    for (const auto &res : detections) {
        if (res.category >=0 && res.category < PLANTGUARD_NUM_CLASSES && res.box.size() >= 4){
            ESP_LOGI(
                TAG,
                "[%s] score=%.3f bbox=[%d, %d, %d, %d]",
                plantguard_classes[res.category],
                res.score,
                res.box[0],
                res.box[1],
                res.box[2],
                res.box[3]
            );
        } else {
            ESP_LOGW(
                TAG,
                "Invalid detection: category=%d box_size=%u",
                res.category,
                static_cast<unsigned>(res.box.size())
            );
        }
        
    }


    // char response[256];

    // snprintf(
    //     response,
    //     sizeof(response),
    //     "{\"status\":\"ok\",\"detections\":%u,"
    //     "\"decode_ms\":%.2f,"
    //     "\"preprocess_ms\":%.2f,"
    //     "\"inference_ms\":%.2f,"
    //     "\"postprocess_ms\":%.2f}",
    //     static_cast<unsigned>(detections.size()),
    //     decode_ms,
    //     preprocess_ms,
    //     inference_ms,
    //     postprocess_ms
    // );

    // httpd_resp_set_type(request, "application/json");
    // httpd_resp_sendstr(request, response);
    httpd_resp_set_type(request, "application/json");

    // Đếm số detection hợp lệ thực sự sẽ trả về.
    size_t valid_detection_count = 0;

    for (const auto &res : detections) {
        if (
            res.category >= 0 &&
            res.category < PLANTGUARD_NUM_CLASSES &&
            res.box.size() >= 4
        ) {
            valid_detection_count++;
        }
    }


    // JSON header
    char header[128];

    snprintf(
        header,
        sizeof(header),
        "{\"status\":\"ok\","
        "\"detection_count\":%u,"
        "\"detections\":[",
        static_cast<unsigned>(valid_detection_count)
    );

    httpd_resp_send_chunk(request, header, HTTPD_RESP_USE_STRLEN);


    // ---------------------------------------------------------
    // Detection objects
    // ---------------------------------------------------------

    bool first_detection = true;
    for (const auto &res : detections) {

        if (res.category < 0 || res.category >= PLANTGUARD_NUM_CLASSES || res.box.size() < 4) {
            continue;
        }


        char detection_json[256];
        snprintf(
            detection_json,
            sizeof(detection_json),
            "%s{"
                "\"class_id\":%d,"
                "\"class\":\"%s\","
                "\"confidence\":%.4f,"
                "\"bbox\":[%d,%d,%d,%d]"
            "}",
            first_detection ? "" : ",",
            res.category,
            plantguard_classes[res.category],
            res.score,
            res.box[0],
            res.box[1],
            res.box[2],
            res.box[3]
        );

        httpd_resp_send_chunk(request, detection_json, HTTPD_RESP_USE_STRLEN);
        first_detection = false;
    }


    char footer[256];
    snprintf(
        footer,
        sizeof(footer),
        "],"
        "\"latency\":{"
            "\"decode_ms\":%.2f,"
            "\"preprocess_ms\":%.2f,"
            "\"inference_ms\":%.2f,"
            "\"postprocess_ms\":%.2f,"
            "\"total_ms\":%.2f"
        "}"
        "}",
        decode_ms,
        preprocess_ms,
        inference_ms,
        postprocess_ms,
        decode_ms +
            preprocess_ms +
            inference_ms +
            postprocess_ms
    );

    httpd_resp_send_chunk(request, footer, HTTPD_RESP_USE_STRLEN);


    // Kết thúc chunked HTTP response.
    httpd_resp_send_chunk(request, nullptr, 0);
    return ESP_OK;
}

static esp_err_t frame_handler(httpd_req_t *request)
{
    size_t image_size = request->content_len;

    if (image_size == 0 || image_size > 512 * 1024) {
        httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid image size");
        return ESP_FAIL;
    }

    auto *packet = new FramePacket{};

    packet->data =
        static_cast<uint8_t *>(
            heap_caps_malloc(
                image_size,
                MALLOC_CAP_SPIRAM |
                MALLOC_CAP_8BIT
            )
        );

    if (packet->data == nullptr) {
        delete packet;

        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot allocate frame");

        return ESP_FAIL;
    }

    packet->len = image_size;
    packet->timestamp_us = esp_timer_get_time();

    size_t received = 0;

    while (received < image_size) {

        int result = httpd_req_recv(
            request,
            reinterpret_cast<char *>(packet->data + received),
            image_size - received
        );

        if (result <= 0) {
            free_frame(packet);
            return ESP_FAIL;
        }

        received += result;
    }

    bool valid =
        image_size >= 4 &&
        packet->data[0] == 0xFF &&
        packet->data[1] == 0xD8 &&
        packet->data[image_size - 2] == 0xFF &&
        packet->data[image_size - 1] == 0xD9;

    if (!valid) {
        free_frame(packet);

        httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "Invalid JPEG"
        );

        return ESP_FAIL;
    }

    xSemaphoreTake(g_frame_mutex, portMAX_DELAY);
    packet->frame_id = ++g_frame_counter;
    const uint64_t accepted_frame_id = packet->frame_id;
    FramePacket *old_frame = g_latest_frame;
    g_latest_frame = packet;
    xSemaphoreGive(g_frame_mutex);

    if (old_frame != nullptr) {
        ESP_LOGI(
            TAG,
            "Dropping stale frame #%llu",
            old_frame->frame_id
        );

        free_frame(old_frame);
    }
    xTaskNotifyGive(g_inference_task_handle);
    ESP_LOGI(TAG, "Accepted frame #%llu", accepted_frame_id);
    httpd_resp_set_status(request, "202 Accepted");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_sendstr(request, "{\"status\":\"accepted\"}");

    return ESP_OK;
}


static void start_http_server()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 20;
    config.stack_size = 8192;   
    httpd_handle_t server = nullptr;
    ESP_ERROR_CHECK(httpd_start(&server, &config));


    httpd_uri_t infer_uri = {};
    infer_uri.uri = "/infer";
    infer_uri.method = HTTP_POST;
    infer_uri.handler = infer_handler;


    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &infer_uri));

    httpd_uri_t frame_uri = {};
    frame_uri.uri = "/frame";
    frame_uri.method = HTTP_POST;
    frame_uri.handler = frame_handler;

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &frame_uri));


    ESP_LOGI(TAG, "HTTP server started");
    ESP_LOGI(TAG, "POST JPEG to /infer");
}



extern "C" void app_main()
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_LOGI(TAG, "Loading PlantGuard model...");

    g_ai_mutex = xSemaphoreCreateMutex();
    if (g_ai_mutex == nullptr) {
        ESP_LOGE(TAG, "Cannot create AI mutex");
        return;
    }

    g_model = new dl::Model(
        reinterpret_cast<const char *>(model_binary_start),
        fbs::MODEL_LOCATION_IN_FLASH_RODATA,
        0,
        dl::MEMORY_MANAGER_GREEDY,
        nullptr,
        true
    );

    ESP_LOGI(TAG, "PlantGuard model loaded");
    ESP_LOGI(TAG, "Initializing YOLO26 processor...");
    g_yolo = new YOLO26(g_model, YOLO_TARGET_K, YOLO_CONF_THRESH, plantguard_classes);
    ESP_LOGI(TAG, "YOLO26 processor initialized");

    g_frame_mutex = xSemaphoreCreateMutex();

    if (g_frame_mutex == nullptr) {
        ESP_LOGE(TAG, "Cannot create frame mutex");
        return;
    }

    BaseType_t task_result = xTaskCreate(inference_task, "InferenceTask", 12 * 1024, nullptr, 5, &g_inference_task_handle);

    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Cannot create InferenceTask");
        return;
    }

    init_wifi();
    start_http_server();
    ESP_LOGI(TAG,"PlantGuard ESP32-S3 ready");
}