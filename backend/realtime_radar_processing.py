import json
import math
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
import uvicorn

from db import query_heart_data_by_page, save_vitals_with_user, utcnow_ms


BASE_DIR = Path(__file__).resolve().parent
RESULT_JSON = BASE_DIR.parent / "frontend" / "dist" / "result.json"

app = FastAPI(title="Radar Processing Service", version="1.0.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


class SaveVitalsRequest(BaseModel):
    userID: Optional[int] = None
    heart_rate: Optional[float] = None
    breath_rate: Optional[float] = None
    target_distance: Optional[float] = None
    timestamp: Optional[str] = None
    measurement_at_ms: Optional[int] = None
    response_at_ms: Optional[int] = None


class RadarState:
    def __init__(self):
        self.heart_rate = 69.0
        self.breath_rate = 16.0
        self.target_distance = 0.85
        self.presence = 1
        self.last_measurement_at_ms = utcnow_ms()

    def refresh_from_result(self):
        if RESULT_JSON.exists():
            try:
                payload = json.loads(RESULT_JSON.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError):
                payload = {}
            self.presence = 1 if payload.get("presence", 1) else 0
            if isinstance(payload.get("heartrate"), (int, float)):
                self.heart_rate = float(payload["heartrate"])
        self.last_measurement_at_ms = utcnow_ms()

    def build_target_response(self):
        self.refresh_from_result()
        response_at_ms = utcnow_ms()
        return {
            "presence": self.presence,
            "target_distance": self.target_distance if self.presence else 0.0,
            "heart_rate": self.heart_rate if self.presence else 0.0,
            "breath_rate": self.breath_rate if self.presence else 0.0,
            "measurement_at_ms": self.last_measurement_at_ms,
            "response_at_ms": response_at_ms,
            "timestamp": response_at_ms / 1000.0,
        }

    def build_detailed_response(self):
        self.refresh_from_result()
        phase_values = [round(math.sin(i / 6) * 0.9, 4) for i in range(120)]
        return {
            "presence": self.presence,
            "breath_rate": self.breath_rate if self.presence else 0.0,
            "phase_values": phase_values,
            "measurement_at_ms": self.last_measurement_at_ms,
        }


radar_state = RadarState()


@app.get("/target")
def get_target():
    return radar_state.build_target_response()


@app.get("/detailed")
def get_detailed():
    return radar_state.build_detailed_response()


@app.post("/save-vitals-with-user")
def save_vitals(payload: SaveVitalsRequest):
    save_vitals_with_user(
        user_id=payload.userID,
        heart_rate=payload.heart_rate,
        breath_rate=payload.breath_rate,
        target_distance=payload.target_distance,
        timestamp=payload.timestamp or datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S"),
        measurement_at_ms=payload.measurement_at_ms,
        response_at_ms=payload.response_at_ms,
    )
    return {"code": 200, "message": "保存成功", "data": None}


@app.get("/heartdata/selectPage")
def select_heart_data(pageNum: int = 1, pageSize: int = 10, date: Optional[str] = None, userID: Optional[int] = None):
    return {
        "code": 200,
        "message": "success",
        "data": query_heart_data_by_page(date=date, user_id=userID, page_num=pageNum, page_size=pageSize),
    }


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8081)
