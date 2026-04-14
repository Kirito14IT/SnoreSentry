from collections import defaultdict, deque
from typing import Deque, Dict, Optional

from fastapi import FastAPI, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
import uvicorn

from db import (
    SNORE_ONLINE_TIMEOUT_MS,
    ack_snore_command,
    clear_snore_controller,
    cleanup_expired_snore_controls,
    create_snore_event,
    get_latest_bound_user_for_device,
    get_next_pending_snore_command,
    get_snore_device_status,
    get_user_snore_device,
    list_online_snore_devices,
    query_snore_events_by_page,
    replace_pending_snore_command,
    select_user_snore_device,
    try_acquire_snore_control,
    upsert_snore_device_status,
    utcnow_ms,
)


app = FastAPI(title="Snore Detection Service", version="1.0.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


TREND_MAX_POINTS = 30
trend_cache: Dict[str, Deque[dict]] = defaultdict(lambda: deque(maxlen=TREND_MAX_POINTS))


def ok(data=None, message: str = "success"):
    return {"code": 200, "message": message, "data": data}


def offline_response(device_id: str):
    return {"code": 410, "message": f"设备 {device_id} 当前离线", "data": None}


def is_device_online(device: Optional[dict]) -> bool:
    if not device:
        return False
    last_seen_at_ms = device.get("last_seen_at_ms")
    return last_seen_at_ms is not None and last_seen_at_ms >= utcnow_ms() - SNORE_ONLINE_TIMEOUT_MS


def append_trend_point(device_id: str, ts_ms: Optional[int], score: Optional[float]) -> None:
    if device_id and ts_ms is not None and score is not None:
        trend_cache[device_id].append({"ts_ms": ts_ms, "score": float(score)})


class ViewerDeviceSelection(BaseModel):
    userID: int
    deviceID: str


class ControlRequest(BaseModel):
    userID: int
    deviceID: str


class HeartbeatRequest(BaseModel):
    deviceID: str
    deviceName: Optional[str] = None
    detecting: bool = False
    latest_score: Optional[float] = None
    latest_window_start_ms: Optional[int] = None
    latest_window_end_ms: Optional[int] = None
    last_event_at_ms: Optional[int] = None
    timestamp: Optional[int] = None


class CommandAckRequest(BaseModel):
    deviceID: str
    commandID: int
    status: str = Field(pattern="^(success|failed|rejected)$")
    message: Optional[str] = None


class DeviceEventRequest(BaseModel):
    deviceID: str
    score: float
    detected_at_ms: int
    window_start_ms: Optional[int] = None
    window_end_ms: Optional[int] = None


@app.get("/snore/devices/online")
def get_online_devices():
    cleanup_expired_snore_controls(SNORE_ONLINE_TIMEOUT_MS)
    return ok(list_online_snore_devices())


@app.post("/snore/device-bindings/select")
def select_viewer_device(payload: ViewerDeviceSelection):
    device = get_snore_device_status(payload.deviceID)
    if not is_device_online(device):
        return offline_response(payload.deviceID)
    select_user_snore_device(payload.userID, payload.deviceID)
    return ok({"user_id": payload.userID, "device_id": payload.deviceID}, "查看设备已更新")


@app.get("/snore/device-bindings/current")
def get_current_viewer_device(userID: int = Query(...)):
    return ok(get_user_snore_device(userID))


@app.post("/snore/control/start")
def start_snore_detection(payload: ControlRequest):
    cleanup_expired_snore_controls(SNORE_ONLINE_TIMEOUT_MS)
    device = get_snore_device_status(payload.deviceID)
    if not is_device_online(device):
        return offline_response(payload.deviceID)

    acquired, controller_user_id = try_acquire_snore_control(payload.userID, payload.deviceID)
    if not acquired:
        raise HTTPException(status_code=409, detail=f"设备已被用户 {controller_user_id} 占用")

    select_user_snore_device(payload.userID, payload.deviceID)
    replace_pending_snore_command(payload.deviceID, "start", payload.userID)
    return ok({"device_id": payload.deviceID}, "开始检测命令已下发")


@app.post("/snore/control/stop")
def stop_snore_detection(payload: ControlRequest):
    cleanup_expired_snore_controls(SNORE_ONLINE_TIMEOUT_MS)
    device = get_snore_device_status(payload.deviceID)
    if not device:
        raise HTTPException(status_code=404, detail="设备不存在")
    controller_user_id = device.get("controller_user_id")
    if controller_user_id not in (None, payload.userID):
        raise HTTPException(status_code=409, detail=f"设备当前由用户 {controller_user_id} 控制")

    replace_pending_snore_command(payload.deviceID, "stop", payload.userID)
    return ok({"device_id": payload.deviceID}, "停止检测命令已下发，等待设备执行")


@app.get("/snore/live")
def get_live_state(
    userID: Optional[int] = Query(default=None),
    deviceID: Optional[str] = Query(default=None),
):
    cleanup_expired_snore_controls(SNORE_ONLINE_TIMEOUT_MS)

    selected_device_id = deviceID
    if not selected_device_id and userID is not None:
        binding = get_user_snore_device(userID)
        selected_device_id = binding.get("device_id") if binding else None

    if not selected_device_id:
        return ok(
            {
                "device_id": None,
                "device_name": None,
                "device_online": False,
                "detecting": False,
                "latest_score": None,
                "latest_window_end_ms": None,
                "last_event_at_ms": None,
                "controller_user_id": None,
                "can_control": False,
                "trend_points": [],
            }
        )

    device = get_snore_device_status(selected_device_id)
    if not device:
        return ok(
            {
                "device_id": selected_device_id,
                "device_name": None,
                "device_online": False,
                "detecting": False,
                "latest_score": None,
                "latest_window_end_ms": None,
                "last_event_at_ms": None,
                "controller_user_id": None,
                "can_control": False,
                "trend_points": list(trend_cache.get(selected_device_id, [])),
            }
        )

    controller_user_id = device.get("controller_user_id")
    can_control = controller_user_id in (None, userID)
    return ok(
        {
            "device_id": selected_device_id,
            "device_name": device.get("device_name"),
            "device_online": is_device_online(device),
            "detecting": bool(device.get("is_detecting")),
            "latest_score": device.get("last_score"),
            "latest_window_end_ms": device.get("latest_window_end_ms"),
            "last_event_at_ms": device.get("last_event_at_ms"),
            "controller_user_id": controller_user_id,
            "can_control": can_control,
            "trend_points": list(trend_cache.get(selected_device_id, [])),
        }
    )


@app.get("/snore/events/selectPage")
def get_snore_events(
    pageNum: int = Query(default=1, ge=1),
    pageSize: int = Query(default=10, ge=1, le=200),
    date: Optional[str] = Query(default=None),
    userID: Optional[int] = Query(default=None),
    deviceID: Optional[str] = Query(default=None),
):
    return ok(
        query_snore_events_by_page(
            date=date,
            user_id=userID,
            device_id=deviceID,
            page_num=pageNum,
            page_size=pageSize,
        )
    )


@app.post("/snore/device/heartbeat")
def device_heartbeat(payload: HeartbeatRequest):
    now_ms = utcnow_ms()
    heartbeat_time_ms = payload.timestamp or now_ms
    upsert_snore_device_status(
        device_id=payload.deviceID,
        device_name=payload.deviceName,
        last_seen_at_ms=heartbeat_time_ms,
        is_detecting=payload.detecting,
        latest_score=payload.latest_score,
        latest_window_start_ms=payload.latest_window_start_ms,
        latest_window_end_ms=payload.latest_window_end_ms,
        last_event_at_ms=payload.last_event_at_ms,
    )
    append_trend_point(payload.deviceID, payload.latest_window_end_ms or heartbeat_time_ms, payload.latest_score)
    return ok(
        {
            "server_time_ms": now_ms,
            "device_online": True,
            "next_poll_in_ms": 1000,
        }
    )


@app.get("/snore/device/commands/next")
def device_next_command(deviceID: str = Query(...)):
    return ok(get_next_pending_snore_command(deviceID))


@app.post("/snore/device/commands/ack")
def device_command_ack(payload: CommandAckRequest):
    ack_snore_command(payload.commandID, payload.deviceID, payload.status, payload.message)
    current = get_snore_device_status(payload.deviceID)
    if payload.status in ("failed", "rejected"):
        clear_snore_controller(payload.deviceID)
    elif payload.status == "success" and current and not bool(current.get("is_detecting")):
        clear_snore_controller(payload.deviceID)
    return ok({"command_id": payload.commandID})


@app.post("/snore/device/events")
def device_event(payload: DeviceEventRequest):
    device = get_snore_device_status(payload.deviceID)
    if not device:
        raise HTTPException(status_code=404, detail="设备不存在")

    user_id = device.get("controller_user_id") or get_latest_bound_user_for_device(payload.deviceID)
    create_snore_event(
        user_id=user_id,
        device_id=payload.deviceID,
        score=payload.score,
        detected_at_ms=payload.detected_at_ms,
        window_start_ms=payload.window_start_ms,
        window_end_ms=payload.window_end_ms,
    )
    append_trend_point(payload.deviceID, payload.window_end_ms or payload.detected_at_ms, payload.score)
    return ok({"device_id": payload.deviceID, "user_id": user_id}, "事件已记录")


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=9090)
