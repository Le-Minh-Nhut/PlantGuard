from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Body
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel,  Field, ConfigDict
from typing import List, Optional
from pathlib import Path
from uuid import uuid4
import asyncio


app = FastAPI()

BASE_DIR = Path(__file__).resolve().parent
IMAGE_DIR = BASE_DIR / "runtime" / "received_images"
IMAGE_DIR.mkdir(parents=True, exist_ok=True)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # React frontend
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)

clients: List[WebSocket] = []

latest_camera_frame: bytes | None = None
camera_frame_id = 0
camera_condition = asyncio.Condition()

detection_clients: List[WebSocket] = []
latest_detection_result: dict | None = None


class SensorData(BaseModel):
    temperature: float
    humidity: float
    soilMoisture:float

class EdgeDetection(BaseModel):
    class_id: int
    class_name: str = Field(alias="class")
    confidence: float
    bbox: list[int]

    model_config = ConfigDict(populate_by_name=True)


class EdgeLatency(BaseModel):
    decode_ms: float
    preprocess_ms: float
    inference_ms: float
    postprocess_ms: float
    total_ms: float


class EdgeInferenceResult(BaseModel):
    device_id: str
    frame_id: int
    timestamp_us: Optional[int] = None
    detection_count: int
    detections: list[EdgeDetection]
    latency: EdgeLatency

@app.post("/api/data")
async def receive_data(data: SensorData):
    payload = data.model_dump()
    dead_clients = []

    for ws in clients.copy():
        try:
            await ws.send_json(payload)
        except Exception as e:
            print(f"WebSocket send failed: {e}")
            dead_clients.append(ws)

    for ws in dead_clients:
        if ws in clients:
            clients.remove(ws)

    return {"status": "sent"}

@app.websocket("/ws/camera")
async def camera_websocket(websocket: WebSocket):
    global latest_camera_frame, camera_frame_id

    await websocket.accept()
    print("ESP32-CAM connected")

    try:
        while True:
            frame = await websocket.receive_bytes()

            if len(frame) < 4 or len(frame) > 512 * 1024:
                continue

            if not (
                frame[0] == 0xFF
                and frame[1] == 0xD8
                and frame[-2] == 0xFF
                and frame[-1] == 0xD9
            ):
                continue

            async with camera_condition:
                latest_camera_frame = frame
                camera_frame_id += 1
                camera_condition.notify_all()

    except WebSocketDisconnect:
        print("ESP32-CAM disconnected")


@app.websocket("/ws/camera/live")
async def camera_live_websocket(websocket: WebSocket):
    await websocket.accept()

    last_frame_id = -1

    try:
        while True:
            async with camera_condition:
                await camera_condition.wait_for(
                    lambda: (
                        latest_camera_frame is not None
                        and camera_frame_id != last_frame_id
                    )
                )

                frame = latest_camera_frame
                last_frame_id = camera_frame_id

            await websocket.send_bytes(frame)

    except WebSocketDisconnect:
        print("Camera viewer disconnected")

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    clients.append(websocket)

    try:
        while True:
            await websocket.receive_text()

    except WebSocketDisconnect:
        print("WebSocket disconnected")

    finally:
        if websocket in clients:
            clients.remove(websocket)

@app.post("/api/detections")
async def receive_edge_detections(result: EdgeInferenceResult):
    global latest_detection_result

    payload = result.model_dump(by_alias=True)
    latest_detection_result = payload

    print(
        f"EDGE AI | "
        f"frame={result.frame_id} | "
        f"detections={result.detection_count} | "
        f"inference={result.latency.inference_ms:.2f} ms"
    )

    dead_clients = []

    for ws in detection_clients.copy():
        try:
            await ws.send_json(payload)
        except Exception as e:
            print(f"Detection WebSocket send failed: {e}")
            dead_clients.append(ws)

    for ws in dead_clients:
        if ws in detection_clients:
            detection_clients.remove(ws)

    return {
        "status": "accepted",
        "frame_id": result.frame_id,
        "viewers": len(detection_clients),
    }

@app.websocket("/ws/detections")
async def detection_websocket(websocket: WebSocket):
    await websocket.accept()
    detection_clients.append(websocket)

    print("Detection viewer connected")

    try:
        if latest_detection_result is not None:
            await websocket.send_json(latest_detection_result)

        while True:
            await websocket.receive_text()

    except WebSocketDisconnect:
        print("Detection viewer disconnected")

    finally:
        if websocket in detection_clients:
            detection_clients.remove(websocket)

@app.post("/upload-image/")
async def upload_image(jpeg: bytes = Body(..., media_type="image/jpeg")):
    filename = IMAGE_DIR / f"{uuid4().hex}.jpg"
    filename.write_bytes(jpeg)
    return {"status": "ok", "size": len(jpeg), "filename": filename.name}


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)