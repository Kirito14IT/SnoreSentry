import os
import sqlite3
from contextlib import contextmanager
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

import bcrypt


BASE_DIR = Path(__file__).resolve().parent
DB_PATH = Path(os.getenv("RADAR_SQLITE_PATH", BASE_DIR / "radar_monitor.db"))
SNORE_ONLINE_TIMEOUT_MS = 10_000


def utcnow_ms() -> int:
    return int(datetime.now(timezone.utc).timestamp() * 1000)


def ms_to_datetime(ms: Optional[int]) -> Optional[str]:
    if ms is None:
        return None
    return datetime.fromtimestamp(ms / 1000, tz=timezone.utc).strftime("%Y-%m-%d %H:%M:%S")


def datetime_to_ms(value: Optional[str]) -> Optional[int]:
    if not value:
        return None
    try:
        dt = datetime.strptime(value, "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc)
    except ValueError:
        dt = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=timezone.utc)
    return int(dt.timestamp() * 1000)


@contextmanager
def get_connection():
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    try:
        yield conn
        conn.commit()
    finally:
        conn.close()


@dataclass
class Database:
    def query(self, sql: str, params: Sequence[Any] = ()) -> List[Dict[str, Any]]:
        with get_connection() as conn:
            cursor = conn.execute(sql, params)
            return [dict(row) for row in cursor.fetchall()]

    def query_one(self, sql: str, params: Sequence[Any] = ()) -> Optional[Dict[str, Any]]:
        rows = self.query(sql, params)
        return rows[0] if rows else None

    def execute(self, sql: str, params: Sequence[Any] = ()) -> int:
        with get_connection() as conn:
            cursor = conn.execute(sql, params)
            return cursor.rowcount

    def insert(self, sql: str, params: Sequence[Any] = ()) -> int:
        with get_connection() as conn:
            cursor = conn.execute(sql, params)
            return int(cursor.lastrowid)

    def executemany(self, sql: str, params_list: Sequence[Sequence[Any]]) -> int:
        with get_connection() as conn:
            cursor = conn.executemany(sql, params_list)
            return cursor.rowcount


db = Database()


def ensure_user_table() -> None:
    db.execute(
        """
        CREATE TABLE IF NOT EXISTS users (
            userID INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT NOT NULL UNIQUE,
            password_hash TEXT NOT NULL,
            nickname TEXT,
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
        """
    )


def ensure_heart_data_table() -> None:
    db.execute(
        """
        CREATE TABLE IF NOT EXISTS heart_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            userID INTEGER,
            heart_rate REAL,
            breath_rate REAL,
            target_distance REAL,
            timestamp TEXT NOT NULL,
            measurement_at_ms INTEGER,
            response_at_ms INTEGER,
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
        """
    )


def ensure_snore_tables() -> None:
    db.execute(
        """
        CREATE TABLE IF NOT EXISTS snore_devices (
            device_id TEXT PRIMARY KEY,
            device_name TEXT,
            last_seen_at_ms INTEGER,
            is_detecting INTEGER NOT NULL DEFAULT 0,
            last_score REAL,
            latest_window_start_ms INTEGER,
            latest_window_end_ms INTEGER,
            last_event_at_ms INTEGER,
            controller_user_id INTEGER,
            control_acquired_at_ms INTEGER,
            updated_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
        """
    )
    db.execute(
        """
        CREATE TABLE IF NOT EXISTS user_snore_device_bindings (
            user_id INTEGER PRIMARY KEY,
            device_id TEXT NOT NULL,
            updated_at TEXT NOT NULL DEFAULT (datetime('now'))
        )
        """
    )
    db.execute(
        """
        CREATE TABLE IF NOT EXISTS snore_commands (
            command_id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT NOT NULL,
            command TEXT NOT NULL,
            requested_by INTEGER,
            status TEXT NOT NULL DEFAULT 'pending',
            message TEXT,
            created_at_ms INTEGER NOT NULL,
            acked_at_ms INTEGER
        )
        """
    )
    db.execute(
        """
        CREATE TABLE IF NOT EXISTS snore_events (
            event_id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER,
            device_id TEXT NOT NULL,
            score REAL NOT NULL,
            detected_at_ms INTEGER NOT NULL,
            window_start_ms INTEGER,
            window_end_ms INTEGER,
            created_at_ms INTEGER NOT NULL
        )
        """
    )


def ensure_all_tables() -> None:
    ensure_user_table()
    ensure_heart_data_table()
    ensure_snore_tables()


def create_user(username: str, password: str, nickname: Optional[str] = None) -> int:
    ensure_user_table()
    hashed = bcrypt.hashpw(password.encode("utf-8"), bcrypt.gensalt()).decode("utf-8")
    return db.insert(
        """
        INSERT INTO users (username, password_hash, nickname)
        VALUES (?, ?, ?)
        """,
        (username, hashed, nickname),
    )


def get_user_by_username(username: str) -> Optional[Dict[str, Any]]:
    ensure_user_table()
    return db.query_one("SELECT * FROM users WHERE username = ?", (username,))


def verify_user(username: str, password: str) -> Optional[Dict[str, Any]]:
    user = get_user_by_username(username)
    if not user:
        return None
    password_hash = user.get("password_hash", "")
    if not password_hash:
        return None
    if bcrypt.checkpw(password.encode("utf-8"), password_hash.encode("utf-8")):
        return user
    return None


def save_vitals_with_user(
    user_id: Optional[int],
    heart_rate: Optional[float],
    breath_rate: Optional[float],
    target_distance: Optional[float],
    timestamp: Optional[str] = None,
    measurement_at_ms: Optional[int] = None,
    response_at_ms: Optional[int] = None,
) -> int:
    ensure_heart_data_table()
    timestamp = timestamp or datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    return db.insert(
        """
        INSERT INTO heart_data (
            userID, heart_rate, breath_rate, target_distance, timestamp, measurement_at_ms, response_at_ms
        ) VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        (user_id, heart_rate, breath_rate, target_distance, timestamp, measurement_at_ms, response_at_ms),
    )


def query_heart_data_by_page(
    date: Optional[str] = None,
    user_id: Optional[int] = None,
    page_num: int = 1,
    page_size: int = 10,
) -> Dict[str, Any]:
    ensure_heart_data_table()
    page_num = max(1, int(page_num))
    page_size = max(1, int(page_size))
    where = []
    params: List[Any] = []

    if date:
        normalized = date.replace("/", "-")
        where.append("strftime('%Y-%m-%d', timestamp) = ?")
        params.append(normalized)
    if user_id is not None:
        where.append("userID = ?")
        params.append(user_id)

    where_sql = f"WHERE {' AND '.join(where)}" if where else ""
    count_row = db.query_one(f"SELECT COUNT(*) AS total FROM heart_data {where_sql}", params) or {"total": 0}
    offset = (page_num - 1) * page_size
    records = db.query(
        f"""
        SELECT id, userID, heart_rate, breath_rate, target_distance, timestamp, measurement_at_ms, response_at_ms
        FROM heart_data
        {where_sql}
        ORDER BY id DESC
        LIMIT ? OFFSET ?
        """,
        (*params, page_size, offset),
    )
    return {"pageNum": page_num, "pageSize": page_size, "total": count_row["total"], "records": records}


def upsert_snore_device_status(
    device_id: str,
    device_name: Optional[str],
    last_seen_at_ms: int,
    is_detecting: bool,
    latest_score: Optional[float],
    latest_window_start_ms: Optional[int] = None,
    latest_window_end_ms: Optional[int] = None,
    last_event_at_ms: Optional[int] = None,
) -> None:
    ensure_snore_tables()
    existing = get_snore_device_status(device_id)
    if existing:
        db.execute(
            """
            UPDATE snore_devices
            SET device_name = COALESCE(?, device_name),
                last_seen_at_ms = ?,
                is_detecting = ?,
                last_score = ?,
                latest_window_start_ms = COALESCE(?, latest_window_start_ms),
                latest_window_end_ms = COALESCE(?, latest_window_end_ms),
                last_event_at_ms = COALESCE(?, last_event_at_ms),
                updated_at = datetime('now')
            WHERE device_id = ?
            """,
            (
                device_name,
                last_seen_at_ms,
                1 if is_detecting else 0,
                latest_score,
                latest_window_start_ms,
                latest_window_end_ms,
                last_event_at_ms,
                device_id,
            ),
        )
    else:
        db.insert(
            """
            INSERT INTO snore_devices (
                device_id, device_name, last_seen_at_ms, is_detecting, last_score,
                latest_window_start_ms, latest_window_end_ms, last_event_at_ms
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                device_id,
                device_name,
                last_seen_at_ms,
                1 if is_detecting else 0,
                latest_score,
                latest_window_start_ms,
                latest_window_end_ms,
                last_event_at_ms,
            ),
        )


def get_snore_device_status(device_id: str) -> Optional[Dict[str, Any]]:
    ensure_snore_tables()
    return db.query_one("SELECT * FROM snore_devices WHERE device_id = ?", (device_id,))


def list_online_snore_devices(now_ms: Optional[int] = None, timeout_ms: int = SNORE_ONLINE_TIMEOUT_MS) -> List[Dict[str, Any]]:
    ensure_snore_tables()
    now_ms = now_ms or utcnow_ms()
    threshold = now_ms - timeout_ms
    rows = db.query(
        """
        SELECT device_id, device_name, last_seen_at_ms, is_detecting, last_score, controller_user_id
        FROM snore_devices
        WHERE last_seen_at_ms IS NOT NULL AND last_seen_at_ms >= ?
        ORDER BY last_seen_at_ms DESC
        """,
        (threshold,),
    )
    for row in rows:
        row["device_online"] = True
    return rows


def select_user_snore_device(user_id: int, device_id: str) -> None:
    ensure_snore_tables()
    existing = db.query_one("SELECT user_id FROM user_snore_device_bindings WHERE user_id = ?", (user_id,))
    if existing:
        db.execute(
            """
            UPDATE user_snore_device_bindings
            SET device_id = ?, updated_at = datetime('now')
            WHERE user_id = ?
            """,
            (device_id, user_id),
        )
    else:
        db.insert(
            """
            INSERT INTO user_snore_device_bindings (user_id, device_id)
            VALUES (?, ?)
            """,
            (user_id, device_id),
        )


def get_user_snore_device(user_id: int) -> Optional[Dict[str, Any]]:
    ensure_snore_tables()
    return db.query_one(
        """
        SELECT b.user_id, b.device_id, d.device_name, d.last_seen_at_ms
        FROM user_snore_device_bindings b
        LEFT JOIN snore_devices d ON d.device_id = b.device_id
        WHERE b.user_id = ?
        """,
        (user_id,),
    )


def get_latest_bound_user_for_device(device_id: str) -> Optional[int]:
    ensure_snore_tables()
    row = db.query_one(
        """
        SELECT user_id
        FROM user_snore_device_bindings
        WHERE device_id = ?
        ORDER BY updated_at DESC
        LIMIT 1
        """,
        (device_id,),
    )
    return int(row["user_id"]) if row and row.get("user_id") is not None else None


def cleanup_expired_snore_controls(timeout_ms: int = SNORE_ONLINE_TIMEOUT_MS) -> None:
    ensure_snore_tables()
    threshold = utcnow_ms() - timeout_ms
    db.execute(
        """
        UPDATE snore_devices
        SET controller_user_id = NULL,
            control_acquired_at_ms = NULL,
            is_detecting = CASE WHEN last_seen_at_ms < ? THEN 0 ELSE is_detecting END
        WHERE controller_user_id IS NOT NULL
          AND (last_seen_at_ms IS NULL OR last_seen_at_ms < ?)
        """,
        (threshold, threshold),
    )


def try_acquire_snore_control(
    user_id: int,
    device_id: str,
    now_ms: Optional[int] = None,
    timeout_ms: int = SNORE_ONLINE_TIMEOUT_MS,
) -> Tuple[bool, Optional[int]]:
    ensure_snore_tables()
    now_ms = now_ms or utcnow_ms()
    cleanup_expired_snore_controls(timeout_ms)
    device = get_snore_device_status(device_id)
    if not device:
        return False, None
    controller_user_id = device.get("controller_user_id")
    if controller_user_id in (None, user_id):
        db.execute(
            """
            UPDATE snore_devices
            SET controller_user_id = ?, control_acquired_at_ms = ?, updated_at = datetime('now')
            WHERE device_id = ?
            """,
            (user_id, now_ms, device_id),
        )
        return True, user_id
    return False, controller_user_id


def clear_snore_controller(device_id: str) -> None:
    ensure_snore_tables()
    db.execute(
        """
        UPDATE snore_devices
        SET controller_user_id = NULL, control_acquired_at_ms = NULL, updated_at = datetime('now')
        WHERE device_id = ?
        """,
        (device_id,),
    )


def replace_pending_snore_command(device_id: str, command: str, requested_by: Optional[int]) -> int:
    ensure_snore_tables()
    db.execute(
        """
        UPDATE snore_commands
        SET status = 'replaced', message = 'superseded'
        WHERE device_id = ? AND status = 'pending'
        """,
        (device_id,),
    )
    return db.insert(
        """
        INSERT INTO snore_commands (device_id, command, requested_by, status, created_at_ms)
        VALUES (?, ?, ?, 'pending', ?)
        """,
        (device_id, command, requested_by, utcnow_ms()),
    )


def get_next_pending_snore_command(device_id: str) -> Optional[Dict[str, Any]]:
    ensure_snore_tables()
    return db.query_one(
        """
        SELECT *
        FROM snore_commands
        WHERE device_id = ? AND status = 'pending'
        ORDER BY command_id ASC
        LIMIT 1
        """,
        (device_id,),
    )


def ack_snore_command(command_id: int, device_id: str, status: str, message: Optional[str]) -> None:
    ensure_snore_tables()
    db.execute(
        """
        UPDATE snore_commands
        SET status = ?, message = ?, acked_at_ms = ?
        WHERE command_id = ? AND device_id = ?
        """,
        (status, message, utcnow_ms(), command_id, device_id),
    )


def create_snore_event(
    user_id: Optional[int],
    device_id: str,
    score: float,
    detected_at_ms: int,
    window_start_ms: Optional[int] = None,
    window_end_ms: Optional[int] = None,
) -> int:
    ensure_snore_tables()
    event_id = db.insert(
        """
        INSERT INTO snore_events (
            user_id, device_id, score, detected_at_ms, window_start_ms, window_end_ms, created_at_ms
        ) VALUES (?, ?, ?, ?, ?, ?, ?)
        """,
        (user_id, device_id, score, detected_at_ms, window_start_ms, window_end_ms, utcnow_ms()),
    )
    db.execute(
        """
        UPDATE snore_devices
        SET last_event_at_ms = ?, last_score = ?, latest_window_start_ms = COALESCE(?, latest_window_start_ms),
            latest_window_end_ms = COALESCE(?, latest_window_end_ms), updated_at = datetime('now')
        WHERE device_id = ?
        """,
        (detected_at_ms, score, window_start_ms, window_end_ms, device_id),
    )
    return event_id


def query_snore_events_by_page(
    date: Optional[str] = None,
    user_id: Optional[int] = None,
    device_id: Optional[str] = None,
    page_num: int = 1,
    page_size: int = 10,
) -> Dict[str, Any]:
    ensure_snore_tables()
    page_num = max(1, int(page_num))
    page_size = max(1, int(page_size))
    where = []
    params: List[Any] = []
    if date:
        normalized = date.replace("/", "-")
        where.append("strftime('%Y-%m-%d', datetime(detected_at_ms / 1000, 'unixepoch')) = ?")
        params.append(normalized)
    if user_id is not None:
        where.append("user_id = ?")
        params.append(user_id)
    if device_id:
        where.append("device_id = ?")
        params.append(device_id)

    where_sql = f"WHERE {' AND '.join(where)}" if where else ""
    count_row = db.query_one(f"SELECT COUNT(*) AS total FROM snore_events {where_sql}", params) or {"total": 0}
    offset = (page_num - 1) * page_size
    records = db.query(
        f"""
        SELECT event_id, user_id, device_id, score, detected_at_ms, window_start_ms, window_end_ms, created_at_ms
        FROM snore_events
        {where_sql}
        ORDER BY detected_at_ms DESC, event_id DESC
        LIMIT ? OFFSET ?
        """,
        (*params, page_size, offset),
    )
    return {"pageNum": page_num, "pageSize": page_size, "total": count_row["total"], "records": records}


ensure_all_tables()
