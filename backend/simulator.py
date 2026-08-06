import random
import requests
import time

while True:
    payload = {
        "temperature": round(random.uniform(25, 35), 2),
        "humidity": round(random.uniform(40, 60), 2),
        "wind": round(random.uniform(0, 20), 2),
        "soilMoisture": round(random.uniform(0, 100), 2),
        "pH": round(random.uniform(6, 8), 2)
    }
    try:
        # requests.post("http://192.168.1.11:8000/api/data", json=payload)
        requests.post("http://backend:8000/api/data", json=payload)
        print("Gửi:", payload)
    except Exception as e:
        print("Lỗi gửi:", e)
    time.sleep(2)
