import hashlib
import hmac
import json
import os
import re
import sqlite3
import struct
import uuid
from contextlib import closing
from datetime import datetime, timedelta, timezone

import paho.mqtt.client as mqtt
from fastapi import Depends, FastAPI, Header, HTTPException, Response, status
from fastapi.middleware.cors import CORSMiddleware
from typing import Literal
from pydantic import BaseModel, ConfigDict, Field, StrictBool, StrictInt

DB_PATH = "/data/commands.db"
RESULT_TOPIC = "lorahome/sensor/+/config/result"
CONFIG_TOPIC = "lorahome/sensor/+/config"
SENSOR_MAC = re.compile(r"[0-9A-F]{2}(?::[0-9A-F]{2}){5}")
GATEWAY_ID = re.compile(r"[0-9A-F]{2}(?::[0-9A-F]{2}){5}")
RELAY_RESULT_TOPIC = "lorahome/gateway/+/relay/result"
RELAY_COMMAND_TOPIC = "lorahome/gateway/+/relay"

app = FastAPI(title="LoRaHome Control API", docs_url=None, redoc_url=None)
app.add_middleware(
    CORSMiddleware,
    allow_origins=[os.environ["GRAFANA_ORIGIN"]],
    allow_credentials=False,
    allow_methods=["GET", "POST"],
    allow_headers=["Authorization", "Content-Type"],
)
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="lorahome-control-api", clean_session=False)
client.username_pw_set(os.environ["MQTT_USER"], os.environ["MQTT_PASSWORD"])

class Configuration(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)

    defaultSleepSeconds: StrictInt | None = Field(default=None, ge=10, le=255)
    defaultDevMode: StrictBool | None = None
    defaultTxPower: StrictInt | None = Field(default=None, ge=2, le=15)
    defaultCpuFrequencyMhz: Literal[80, 160, 240] | None = None
    inheritDefaults: StrictBool | None = None
    radioProfilePreset: Literal["fast", "default", "range"] | None = None
    spreadingFactor: StrictInt | None = Field(default=None, ge=7, le=12)
    bandwidthKhz: float | None = None
    codingRate: StrictInt | None = Field(default=None, ge=5, le=8)

class RelayConfiguration(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True)
    relayClosed: StrictBool | None = None
    relayMaxOnSeconds: StrictInt | None = Field(default=None, ge=1, le=7200)

class ControlConfiguration(Configuration):
    relayClosed: StrictBool | None = None
    relayMaxOnSeconds: StrictInt | None = Field(default=None, ge=1, le=7200)

def connect_db():
    connection = sqlite3.connect(DB_PATH)
    connection.execute("""CREATE TABLE IF NOT EXISTS commands (
        id TEXT PRIMARY KEY, sensor_id TEXT NOT NULL, payload TEXT NOT NULL, status TEXT NOT NULL, created_at TEXT NOT NULL
    )""")
    columns = {row[1] for row in connection.execute("PRAGMA table_info(commands)")}
    if "sensor_id" not in columns:
        connection.execute("ALTER TABLE commands ADD COLUMN sensor_id TEXT")
    connection.execute("""CREATE TABLE IF NOT EXISTS radio_transition_acknowledgements (
        command_id TEXT PRIMARY KEY, acknowledged_at TEXT NOT NULL
    )""")
    return connection

def pending_radio_transition(db, sensor_id):
    rows = db.execute("""SELECT id, payload, status, created_at FROM commands
        WHERE sensor_id = ? AND status IN ('published', 'queued', 'unconfirmed')
        AND id NOT IN (SELECT command_id FROM radio_transition_acknowledgements)
        ORDER BY created_at DESC""", (sensor_id,)).fetchall()
    for command_id, payload, command_status, created_at in rows:
        config = json.loads(payload).get("config", {})
        if "radioProfilePreset" in config or "spreadingFactor" in config:
            return {"commandId": command_id, "status": command_status, "createdAt": created_at}
    return None

def authorize(authorization: str | None = Header(default=None)):
    expected = f"Bearer {os.environ['CONTROL_API_TOKEN']}"
    if authorization is None or not hmac.compare_digest(authorization, expected):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Unauthorized")

def on_connect(client, userdata, flags, reason_code, properties):
    client.subscribe(RESULT_TOPIC, qos=1)
    # Remove commands retained by earlier deployments. New commands are never retained.
    client.subscribe(CONFIG_TOPIC, qos=1)

    client.subscribe(RELAY_RESULT_TOPIC, qos=1)
    client.subscribe(RELAY_COMMAND_TOPIC, qos=1)

def on_message(client, userdata, message):
    try:
        parts = message.topic.split("/")
        if len(parts) == 4 and parts[:2] == ["lorahome", "gateway"] and GATEWAY_ID.fullmatch(parts[2]) and parts[3] == "relay":
            if message.retain:
                client.publish(message.topic, b"", qos=1, retain=True)
            return
        sensor_result = len(parts) == 5 and parts[:2] == ["lorahome", "sensor"] and parts[3:] == ["config", "result"] and SENSOR_MAC.fullmatch(parts[2])
        relay_result = len(parts) == 5 and parts[:2] == ["lorahome", "gateway"] and parts[3:] == ["relay", "result"] and GATEWAY_ID.fullmatch(parts[2])
        if (
            len(parts) == 4
            and parts[:2] == ["lorahome", "sensor"]
            and parts[3] == "config"
            and SENSOR_MAC.fullmatch(parts[2])
        ):
            if message.retain:
                client.publish(message.topic, b"", qos=1, retain=True)
            return
        result = json.loads(message.payload.decode("utf-8"))
        command_id = result["commandId"]
        command_status = result["status"]
        if (
            set(result) != {"commandId", "status"}
            or not isinstance(command_id, str)
            or not re.fullmatch(r"[0-9a-fA-F]{8}-(?:[0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12}", command_id)
            or not (sensor_result or relay_result)
            or command_status not in ({"queued", "applied", "failed", "rejected", "expired", "superseded", "unconfirmed"} if sensor_result else {"queued", "applied", "failed", "rejected", "superseded"})
        ):
            return
        with closing(connect_db()) as db, db:
            allowed_current = ("published",) if command_status == "queued" else ("published", "queued", "unconfirmed")
            db.execute(
                "UPDATE commands SET status = ? WHERE id = ? AND sensor_id = ? AND status IN ({})".format(
                    ",".join("?" for _ in allowed_current)
                ),
                (command_status, command_id, parts[2], *allowed_current),
            )
    except (UnicodeDecodeError, json.JSONDecodeError, KeyError, TypeError):
        return

client.on_connect = on_connect
client.on_message = on_message

@app.on_event("startup")
def startup():
    connect_db().close()
    client.connect(os.environ["MQTT_HOST"], int(os.environ["MQTT_PORT"]))
    client.loop_start()

@app.on_event("shutdown")
def shutdown():
    client.loop_stop()
    client.disconnect()

@app.get("/healthz")
def healthz():
    return {"status": "ok"}

@app.post("/api/v1/sensors/{sensor_id}/config", status_code=status.HTTP_202_ACCEPTED)
def update_config(sensor_id: str, config: Configuration, _=Depends(authorize)):
    if not SENSOR_MAC.fullmatch(sensor_id):
        raise HTTPException(status_code=status.HTTP_422_UNPROCESSABLE_ENTITY, detail="sensor_id must be an uppercase colon-separated MAC address")
    values = config.model_dump(exclude_none=True)
    if not values:
        raise HTTPException(status_code=status.HTTP_422_UNPROCESSABLE_ENTITY, detail="No configuration values supplied")
    if "inheritDefaults" in values and (values["inheritDefaults"] is not True or len(values) != 1):
        raise HTTPException(status_code=status.HTTP_422_UNPROCESSABLE_ENTITY, detail="inheritDefaults must be true and supplied alone")
    custom = {"spreadingFactor", "bandwidthKhz", "codingRate"}
    if "defaultDevMode" in values and ({"defaultCpuFrequencyMhz", "radioProfilePreset"} | custom).intersection(values):
        raise HTTPException(status_code=422, detail="Submit mode changes separately from CPU or radio profile changes")
    if custom.intersection(values) and (not custom.issubset(values) or "radioProfilePreset" in values):
        raise HTTPException(status_code=422, detail="Supply either a preset or all custom radio fields")
    if "bandwidthKhz" in values:
        supported = (7.8, 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125, 250, 500)
        match = next((value for value in supported if abs(value - values["bandwidthKhz"]) < 0.01), None)
        if match is None:
            raise HTTPException(status_code=422, detail="Unsupported radio bandwidth")
        values["bandwidthKhz"] = match
    return publish_command(sensor_id, values, f"lorahome/sensor/{sensor_id}/config")

COMMAND_CONFIG_ORDER = (
    "bandwidthKhz", "codingRate", "defaultCpuFrequencyMhz", "defaultDevMode",
    "defaultSleepSeconds", "defaultTxPower", "inheritDefaults", "radioProfilePreset", "spreadingFactor",
    "relayClosed", "relayMaxOnSeconds",
)

def canonical_command(command):
    config = {key: command["config"][key] for key in COMMAND_CONFIG_ORDER if key in command["config"]}
    bandwidth = config.get("bandwidthKhz")
    ordered = {
        "commandId": command["commandId"],
        "config": config,
        "expiresAt": command["expiresAt"],
        "schemaVersion": command["schemaVersion"],
    }
    serialized = json.dumps(ordered, separators=(",", ":"))
    if bandwidth is not None:
        # The Gateway assigns its float32 bandwidth table to ArduinoJson's
        # double-backed document. In the supported 7.8..500 range its formatter
        # emits ten significant digits, trimming trailing zeroes (125, not
        # 125.0). Preserve those exact bytes without changing the HTTP values.
        radio_bandwidth = struct.unpack("<f", struct.pack("<f", bandwidth))[0]
        serialized = serialized.replace(
            '"bandwidthKhz":' + json.dumps(bandwidth),
            '"bandwidthKhz":' + format(radio_bandwidth, ".10g"), 1,
        )
    return serialized.encode()

def publish_command(target_id, values, topic):
    created_at = datetime.now(timezone.utc)
    command = {
        "commandId": str(uuid.uuid4()),
        "schemaVersion": 1,
        "expiresAt": (created_at + timedelta(minutes=5)).strftime("%Y-%m-%dT%H:%M:%S+00:00"),
        "config": values,
    }
    canonical = canonical_command(command)
    command["signature"] = hmac.new(os.environ["CONTROL_COMMAND_SECRET"].encode(), canonical, hashlib.sha256).hexdigest()
    encoded = json.dumps(command, separators=(",", ":"))
    if len(encoded.encode("utf-8")) > 384:
        raise HTTPException(status_code=status.HTTP_422_UNPROCESSABLE_ENTITY, detail="Command payload exceeds the 384-byte Gateway limit")
    with closing(connect_db()) as db, db:
        if topic.startswith("lorahome/sensor/") and ("radioProfilePreset" in values or "spreadingFactor" in values):
            db.execute("BEGIN IMMEDIATE")
            pending = pending_radio_transition(db, target_id)
            if pending:
                raise HTTPException(status_code=409, detail=f"Radio transition {pending['commandId']} is still unresolved")
        db.execute(
            "INSERT INTO commands (id, sensor_id, payload, status, created_at) VALUES (?, ?, ?, ?, ?)",
            (command["commandId"], target_id, encoded, "published", created_at.isoformat()),
        )
    if client.publish(topic, encoded, qos=1, retain=False).rc != mqtt.MQTT_ERR_SUCCESS:
        with closing(connect_db()) as db, db:
            db.execute("DELETE FROM commands WHERE id = ?", (command["commandId"],))
        raise HTTPException(status_code=status.HTTP_503_SERVICE_UNAVAILABLE, detail="MQTT broker unavailable")
    return {"commandId": command["commandId"], "status": "published", "createdAt": created_at.isoformat()}

@app.post("/api/v1/gateways/{gateway_id}/relay", status_code=status.HTTP_202_ACCEPTED)
def update_relay(gateway_id: str, config: RelayConfiguration, _=Depends(authorize)):
    if not GATEWAY_ID.fullmatch(gateway_id):
        raise HTTPException(status_code=422, detail="Invalid gateway ID")
    values = config.model_dump(exclude_none=True)
    if not values:
        raise HTTPException(status_code=422, detail="No relay values supplied")
    return publish_command(gateway_id, values, f"lorahome/gateway/{gateway_id}/relay")

@app.post("/api/v1/gateways/{gateway_id}/sensors/{sensor_id}/control", status_code=status.HTTP_202_ACCEPTED)
def update_control(gateway_id: str, sensor_id: str, config: ControlConfiguration, response: Response, _=Depends(authorize)):
    if not GATEWAY_ID.fullmatch(gateway_id):
        raise HTTPException(status_code=422, detail="Invalid gateway ID")
    if not SENSOR_MAC.fullmatch(sensor_id):
        raise HTTPException(status_code=422, detail="sensor_id must be an uppercase colon-separated MAC address")
    values = config.model_dump(exclude_none=True)
    if not values:
        raise HTTPException(status_code=422, detail="No control values supplied")
    relay_values = {key: values.pop(key) for key in ("relayClosed", "relayMaxOnSeconds") if key in values}
    result = {}
    for name, action in (
        ("sensor", lambda: update_config(sensor_id, Configuration.model_validate(values))),
        ("relay", lambda: update_relay(gateway_id, RelayConfiguration.model_validate(relay_values))),
    ):
        if not (values if name == "sensor" else relay_values):
            continue
        try:
            result[name] = action()
        except HTTPException as error:
            response.status_code = 207
            result[name] = {"status": "error", "httpStatus": error.status_code, "detail": error.detail}
    return result

@app.get("/api/v1/gateways/{gateway_id}/relay/commands/{command_id}")
def relay_command_status(gateway_id: str, command_id: str, _=Depends(authorize)):
    if not GATEWAY_ID.fullmatch(gateway_id):
        raise HTTPException(status_code=422, detail="Invalid gateway ID")
    with closing(connect_db()) as db, db:
        row = db.execute("SELECT id, status, created_at FROM commands WHERE id = ? AND sensor_id = ?", (command_id, gateway_id)).fetchone()
    if row is None:
        raise HTTPException(status_code=404, detail="Unknown command")
    return {"commandId": row[0], "status": row[1], "createdAt": row[2]}

@app.get("/api/v1/sensors/{sensor_id}/commands/{command_id}")
def command_status(sensor_id: str, command_id: str, _=Depends(authorize)):
    if not SENSOR_MAC.fullmatch(sensor_id):
        raise HTTPException(status_code=status.HTTP_422_UNPROCESSABLE_ENTITY, detail="sensor_id must be an uppercase colon-separated MAC address")
    with closing(connect_db()) as db, db:
        row = db.execute("SELECT id, status, created_at FROM commands WHERE id = ? AND sensor_id = ?", (command_id, sensor_id)).fetchone()
    if row is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Unknown command")
    return {"commandId": row[0], "status": row[1], "createdAt": row[2]}

@app.get("/api/v1/sensors/{sensor_id}/radio-transition")
def radio_transition_status(sensor_id: str, _=Depends(authorize)):
    if not SENSOR_MAC.fullmatch(sensor_id):
        raise HTTPException(status_code=422, detail="Invalid sensor ID")
    with closing(connect_db()) as db, db:
        return {"pending": pending_radio_transition(db, sensor_id)}

@app.post("/api/v1/sensors/{sensor_id}/radio-transition/{command_id}/acknowledge")
def acknowledge_radio_transition(sensor_id: str, command_id: str, _=Depends(authorize)):
    if not SENSOR_MAC.fullmatch(sensor_id):
        raise HTTPException(status_code=422, detail="Invalid sensor ID")
    with closing(connect_db()) as db, db:
        db.execute("BEGIN IMMEDIATE")
        pending = pending_radio_transition(db, sensor_id)
        if not pending or pending["commandId"] != command_id:
            raise HTTPException(status_code=404, detail="Unresolved transition not found")
        db.execute("INSERT INTO radio_transition_acknowledgements (command_id, acknowledged_at) VALUES (?, ?)",
                   (command_id, datetime.now(timezone.utc).isoformat()))
    return {"commandId": command_id, "acknowledged": True}


