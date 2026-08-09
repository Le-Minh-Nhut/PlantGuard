# PlantGuard

**PlantGuard** is an AIoT edge-computing system for real-time tomato leaf disease monitoring.

The system combines an **ESP32-CAM** for live garden monitoring, an **ESP32-S3 N16R8** for on-device YOLO inference, a **FastAPI** realtime backend, and a **React** dashboard for live camera, sensor, and AI detection data.

The goal is to keep plant-health monitoring responsive while running computer vision directly at the edge instead of relying entirely on cloud inference.

---

## Overview

PlantGuard separates the realtime camera path from the AI inference path.

```text
                       ┌─────────────────────────────┐
                       │        React Dashboard      │
                       │                             │
                       │  Live Camera               │
                       │  Sensor Data               │
                       │  Edge AI Results           │
                       └──────────────▲──────────────┘
                                      │
                            WebSocket / REST
                                      │
                       ┌──────────────┴──────────────┐
                       │       FastAPI Backend       │
                       │                             │
                       │  /ws/camera/live           │
                       │  /ws/detections            │
                       │  /ws                       │
                       │  /api/detections           │
                       └──────▲──────────────▲───────┘
                              │              │
                     Live JPEG│              │Detection JSON
                              │              │
                     ┌────────┴──────┐   ┌───┴─────────────┐
                     │   ESP32-CAM   │   │    ESP32-S3     │
                     │    OV2640     │   │     N16R8       │
                     │               │   │                 │
                     │  VGA JPEG     │   │ YOLO26n ESP-DL │
                     └───────┬───────┘   └──────▲──────────┘
                             │                  │
                             └── AI sample ─────┘
                                  POST /frame
```

The architecture intentionally uses different rates for camera streaming and AI inference:

```text
Live Camera FPS != AI FPS
```

This prevents the ~6 second edge inference latency from blocking the realtime monitoring experience.

---

## Key Features

* Realtime ESP32-CAM garden monitoring
* Persistent WebSocket JPEG streaming
* On-device tomato disease detection on ESP32-S3
* Asynchronous edge inference using FreeRTOS
* Latest-frame semantics to avoid AI queue buildup
* FastAPI realtime communication hub
* React + TypeScript monitoring dashboard
* Sensor telemetry over WebSocket
* ESP32-S3 detection results streamed to the frontend
* INT8 ESP-DL deployment for resource-constrained hardware
* PSRAM-backed frame ownership and memory management
* Separate synchronous `/infer` endpoint for debugging and benchmarking

---

## Edge AI Model

PlantGuard currently uses a custom **YOLO26n object detection model** trained for tomato leaf disease detection.

### Classes

```text
Bacterial_spot
Early_blight
Late_blight
Leaf_Mold
Septoria_leaf_spot
Spider_mites
Tomato_Yellow_Leaf_Curl_Virus
Tomato_mosaic_virus
```

### Model Configuration

```text
Model        : YOLO26n
Task         : Object Detection
Input        : 320 × 320
Classes      : 8
Deployment   : ESP-DL
Quantization : INT8
Model size   : ~2.9 MB
```

### Quantized Validation

```text
Precision   : 0.814
Recall      : 0.665
mAP@50      : 0.764
mAP@50-95   : 0.661
```

The quantized model preserves most of the original model quality while making deployment on the ESP32-S3 practical.

---

## Hardware

### ESP32-S3

```text
Module       : ESP32-S3 N16R8
Flash        : 16 MB
PSRAM        : 8 MB Octal
PSRAM Speed  : 80 MHz
Framework    : ESP-IDF 5.5.2
ESP-DL       : 3.3.9
```

The ESP32-S3 performs the complete edge inference pipeline:

```text
JPEG
 ↓
Decode
 ↓
Resize / Letterbox
 ↓
INT8 preprocessing
 ↓
YOLO26n inference
 ↓
Postprocessing
 ↓
Detection JSON
```

Typical inference latency is currently approximately:

```text
~6.2 seconds / frame
```

Inference performance optimization is planned after system-level stability and synchronization are complete.

### ESP32-CAM

```text
Camera       : OV2640
Format       : JPEG
Resolution   : VGA 640 × 480
JPEG Quality : 12
```

The camera handles two independent tasks:

```text
Realtime JPEG stream → FastAPI

Periodic AI sample → ESP32-S3 /frame
```

---

## Asynchronous Edge Inference

The ESP32-S3 exposes:

```text
POST /frame
```

Instead of keeping the HTTP request open for the entire inference, `/frame` copies the JPEG into PSRAM, stores the newest pending frame, notifies the inference task, and immediately returns:

```http
202 Accepted
```

Inference then runs asynchronously inside a FreeRTOS task.

```text
ESP32-CAM
    │
    │ POST JPEG
    ▼
ESP32-S3 /frame
    │
    ├── Copy JPEG to PSRAM
    ├── Replace stale pending frame
    ├── Notify InferenceTask
    └── 202 Accepted
              │
              ▼
        InferenceTask
              │
              ├── Decode
              ├── Preprocess
              ├── YOLO26n
              ├── Postprocess
              └── POST detection JSON to FastAPI
```

This avoids blocking the camera for the entire AI inference time.

---

## Latest-Frame Semantics

The AI pipeline intentionally does not create an unlimited frame queue.

If inference is processing frame `#10` while newer frames arrive:

```text
#11 arrives
#12 arrives → replaces #11
#13 arrives → replaces #12
```

When inference finishes, the newest available frame is processed.

This design prevents stale AI backlogs and keeps detections relevant to the current scene.

---

## Realtime Camera Pipeline

The live camera path uses a persistent WebSocket connection.

```text
ESP32-CAM
    │
    │ JPEG binary WebSocket
    ▼
FastAPI /ws/camera
    │
    │ latest frame
    ▼
FastAPI /ws/camera/live
    │
    ▼
React Dashboard
```

The backend keeps only the latest camera frame rather than maintaining a historical video-frame queue.

Slow viewers can therefore skip stale frames instead of slowing down the producer.

---

## Detection Pipeline

After inference finishes, the ESP32-S3 sends structured metadata to:

```text
POST /api/detections
```

Example:

```json
{
  "device_id": "esp32-s3",
  "frame_id": 12,
  "timestamp_us": 58392013,
  "detection_count": 1,
  "detections": [
    {
      "class_id": 2,
      "class": "Late_blight",
      "confidence": 0.9785,
      "bbox": [69, 45, 281, 273]
    }
  ],
  "latency": {
    "decode_ms": 22.1,
    "preprocess_ms": 25.4,
    "inference_ms": 6190.3,
    "postprocess_ms": 2.8,
    "total_ms": 6240.6
  }
}
```

FastAPI stores the latest result and broadcasts it through:

```text
/ws/detections
```

The React dashboard currently displays:

```text
AI frame ID
Disease class
Confidence
Inference latency
Edge AI connection status
```

Bounding-box overlay is intentionally deferred until frame synchronization and bounding-box coordinate mapping are fully verified.

---

## Sensor Pipeline

Sensor data is received through:

```text
POST /api/data
```

and broadcast to frontend clients through:

```text
/ws
```

Current frontend telemetry:

```text
Temperature
Air Humidity
Soil Moisture
```

---

## Backend API

```text
POST /api/data
    Receive sensor telemetry

WS   /ws
    Stream sensor telemetry to frontend

WS   /ws/camera
    Receive binary JPEG frames from ESP32-CAM

WS   /ws/camera/live
    Stream latest camera frame to React

POST /api/detections
    Receive ESP32-S3 inference results

WS   /ws/detections
    Stream latest detection result to React

POST /upload-image/
    Legacy/manual JPEG debug endpoint
```

The ESP32-S3 also exposes:

```text
POST /frame
    Asynchronous production-style AI frame ingest

POST /infer
    Synchronous inference endpoint for debugging and benchmarking
```

---

## Project Structure

```text
PlantGuard/
├── backend/
│   └── main.py
│
├── data/
│   ├── raw/
│   └── processed/
│
├── edge/
│   ├── CameraCapture/
│   │   └── CameraCapture.ino
│   │
│   └── ESP32S3Inference/
│       ├── main/
│       │   ├── app_main.cpp
│       │   ├── plantguard_classes.hpp
│       │   └── models/
│       ├── CMakeLists.txt
│       └── partitions.csv
│
├── ml/
│   └── ...
│
└── web/
    └── src/
        └── App.tsx
```

---

## Running the Backend

```bash
cd backend

uvicorn main:app \
  --host 0.0.0.0 \
  --port 8000 \
  --reload
```

`0.0.0.0` is required so ESP32 devices on the local network can reach the server.

---

## Running the Frontend

```bash
cd web

npm install
npm run dev
```

To expose Vite on the LAN:

```bash
npm run dev -- --host 0.0.0.0
```

---

## Building ESP32-S3 Firmware

Activate ESP-IDF:

```bash
source ~/esp/esp-idf/export.sh
```

Build:

```bash
cd edge/ESP32S3Inference

idf.py build
```

Flash:

```bash
idf.py -p /dev/ttyACM1 flash
```

Monitor:

```bash
idf.py -p /dev/ttyACM1 monitor
```

Or:

```bash
idf.py -p /dev/ttyACM1 flash monitor
```

Serial device names can vary between systems.

---

## Dataset

The processed PlantGuard training dataset currently contains approximately:

```text
Training images   : 6,849
Validation images : 856
Validation objects: 1,462
```

The dataset combines tomato disease object-detection data prepared for the eight PlantGuard disease classes.

Large datasets and generated runtime files are intentionally excluded from Git.

---

## Current Development Status

```text
Dataset preparation                  ✅
YOLO26 training                      ✅
INT8 quantization                    ✅
ESP-DL export                        ✅
ESP32-S3 model loading               ✅
Real JPEG inference on ESP32-S3      ✅
Async /frame ingest                  ✅
FreeRTOS InferenceTask               ✅
Latest-frame semantics               ✅
ESP32-CAM realtime WebSocket         ✅
FastAPI camera realtime hub          ✅
React realtime camera dashboard      ✅
S3 → FastAPI detection metadata      ✅ Integration in progress
FastAPI → React detection stream     ✅ Integration in progress
BBox / live-frame synchronization    ⏳
Inference latency optimization       ⏳
Production device management         ⏳
```

---

## Engineering Principles

PlantGuard is built around several system-level constraints:

**Realtime monitoring must not wait for AI.**

The camera can update independently while inference runs at a much slower rate.

**Do not build stale inference queues.**

Only the latest pending AI frame matters for realtime monitoring.

**Edge devices own their workloads.**

The ESP32-S3 performs inference locally and publishes results rather than requiring the backend to synchronously orchestrate every inference.

**Prefer persistent realtime connections.**

WebSocket is used for camera and frontend realtime transport instead of creating one HTTP connection per frame.

---

## Roadmap

Future work includes:

```text
Frame ID synchronization between camera and AI results
Verified bounding-box coordinate mapping
Realtime bounding-box overlay
ESP32-CAM networking task isolation
Inference latency optimization
Multiple PlantGuard edge nodes
Device identity and health monitoring
Historical disease event storage
Alerts and plant-health analytics
MQTT-based telemetry evaluation
```

---

## Security

Wi-Fi credentials are not intended to be committed to the repository.

Use a local file such as:

```cpp
#include "secrets.h"
```

and keep it excluded through `.gitignore`.

Example:

```cpp
#pragma once

#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
```

---

## Motivation

PlantGuard is designed as a practical Edge AI system rather than a model-only demo.

The project combines:

```text
Computer Vision
Embedded AI
IoT Networking
Realtime Systems
Backend Engineering
Frontend Monitoring
```

into one end-to-end deployment running on constrained hardware.

The long-term goal is a scalable garden-monitoring platform where edge devices continuously observe plants, perform local intelligence, and publish actionable plant-health information in realtime.
