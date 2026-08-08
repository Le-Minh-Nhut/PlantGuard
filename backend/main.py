from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Body
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from typing import List
from pathlib import Path
from uuid import uuid4

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


class SensorData(BaseModel):
    temperature: float
    humidity: float
    soilMoisture:float

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

@app.post("/upload-image/")
async def upload_image(jpeg: bytes = Body(..., media_type="image/jpeg")):
    global index
    filename = IMAGE_DIR / f"{uuid4().hex}.jpg"
    filename.write_bytes(jpeg)
    return {"status": "ok", "size": len(jpeg), "filename": filename.name}


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)