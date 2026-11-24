import json
import logging
import os
import re
from datetime import datetime, timezone

import paho.mqtt.client as mqtt
from influxdb_client import InfluxDBClient, Point, WriteOptions

logging.basicConfig(level=os.getenv("LOG_LEVEL", "INFO"), format="%(asctime)s %(levelname)s %(message)s")
INFLUX = InfluxDBClient(url=os.environ["INFLUX_URL"], token=os.environ["INFLUX_TOKEN"], org=os.environ["INFLUX_ORG"])
WRITER = INFLUX.write_api(write_options=WriteOptions(batch_size=100, flush_interval=1_000))
BUCKET = os.environ["INFLUX_BUCKET"]
EVENT_BUCKET = os.environ["INFLUX_EVENT_BUCKET"]

def integer(value, field):
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{field} must be an integer")
    return value

def boolean(value, field):
    if not isinstance(value, bool):
        raise ValueError(f"{field} must be a boolean")
    return value

def number(value, field):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{field} must be a number")
    return float(value)

def string(value, field):
    if not isinstance(value, str):
        raise ValueError(f"{field} must be a string")
    return value

def nested_fields(point, payload, object_name, fields):
    nested = payload.get(object_name)
    if nested is None:
        return point
    if not isinstance(nested, dict):
        raise ValueError(f"{object_name} must be an object")
    for source, target, validator in fields:
        value = nested.get(source)
        if value is not None:
            point = point.field(target, validator(value, f"{object_name}.{source}"))
    return point

def snapshot_fields(point, payload, object_name, prefix, fields, nested=None):
    snapshot = payload.get(object_name)
    if snapshot is None:
        return point
    if not isinstance(snapshot, dict):
        raise ValueError(f"{object_name} must be an object")
    point = point.field(f"{prefix}_present", True)
    null_fields = []
    for source, target, validator in fields:
        if source not in snapshot:
            continue
        value = snapshot[source]
        if value is None:
            null_fields.append(source)
        else:
            point = point.field(f"{prefix}_{target}", validator(value, f"{object_name}.{source}"))
    if null_fields:
        point = point.field(f"{prefix}_null_fields", ",".join(null_fields))
    if nested:
        for child_name, child_fields in nested:
            point = snapshot_fields(point, snapshot, child_name, f"{prefix}_{child_name}", child_fields)
    return point

def device_timestamp(payload):
    value = payload["timestamp"]
    if not isinstance(value, str):
        raise ValueError("timestamp must be a string")
    try:
        datetime.fromisoformat(value)
    except ValueError as error:
        raise ValueError("timestamp is not ISO 8601") from error
    # MQTT does not indicate whether the Gateway clock is synchronized.
    return value, False

def write(topic, payload, retained=False):
    received = datetime.now(timezone.utc)
    parts = topic.split("/")
    sensor_topic = len(parts) == 4 and parts[:2] == ["lorahome", "sensor"] and parts[3] in {"data", "telemetry", "status"}
    gateway_topic = len(parts) == 4 and parts[:2] == ["lorahome", "gateway"] and parts[3] == "status"
    if sensor_topic:
        if not re.fullmatch(r"[0-9A-F]{2}(?::[0-9A-F]{2}){5}", parts[2]):
            raise ValueError("topic sensor ID is invalid")
    elif gateway_topic:
        if not re.fullmatch(r"[0-9A-F]{2}(?::[0-9A-F]{2}){5}", parts[2]):
            raise ValueError("topic gateway ID is invalid")
    else:
        raise ValueError("unknown topic")
    if retained and sensor_topic and parts[3] != "status":
        raise ValueError("Sensor measurements must be non-retained")
    reported_at, device_time_valid = device_timestamp(payload)
    if sensor_topic and parts[3] == "data":
        sensor_id = payload["mac"]
        if not isinstance(sensor_id, str) or not re.fullmatch(r"[0-9A-F]{2}(?::[0-9A-F]{2}){5}", sensor_id):
            raise ValueError("mac is invalid")
        if sensor_id != parts[2]:
            raise ValueError("topic sensor ID does not match payload mac")
        temperature = integer(payload["temperature"], "temperature")
        voltage = integer(payload["battVoltage"], "battVoltage")
        point = Point("sensor_reading").tag("sensor_id", sensor_id)
        point = point.field("temperature_raw", temperature)
        point = point.field("temperature_c", temperature / 100.0)
        point = point.field("battery_voltage_raw", voltage)
        point = point.field("battery_voltage_v", voltage / 1000.0)
        for key in ("bootNonce", "msgCount", "battPercent", "isDevMode", "needsTimeSync", "sleepInterval", "txPower"):
            point = point.field(key, integer(payload[key], key))
        point = snapshot_fields(point, payload, "sensorRadio", "sensor_radio", (
            ("available", "available", boolean),
            ("frequencyMhz", "frequency_mhz", number),
            ("bandwidthKhz", "bandwidth_khz", number),
            ("spreadingFactor", "spreading_factor", integer),
            ("codingRate", "coding_rate", integer),
            ("txPowerDbm", "tx_power_dbm", integer),
            ("uplinkRssiDbm", "uplink_rssi_dbm", number),
            ("uplinkSnrDb", "uplink_snr_db", number),
            ("lastDownlinkRssiDbm", "last_downlink_rssi_dbm", number),
            ("lastDownlinkSnrDb", "last_downlink_snr_db", number),
        ))
        point = snapshot_fields(point, payload, "gatewayRadio", "gateway_radio", (
            ("available", "available", boolean),
            ("activity", "activity", string),
            ("frequencyMhz", "frequency_mhz", number),
            ("bandwidthKhz", "bandwidth_khz", number),
            ("spreadingFactor", "spreading_factor", integer),
            ("codingRate", "coding_rate", integer),
            ("txPowerDbm", "tx_power_dbm", integer),
            ("rssiDbm", "rssi_dbm", number),
            ("snrDb", "snr_db", number),
        ), nested=(("duty", (
            ("countedBurstUs", "counted_burst_us", integer),
            ("cooldownMs", "cooldown_ms", integer),
            ("totalBurstCount", "total_burst_count", integer),
            ("nonCompliantBurstCount", "noncompliant_burst_count", integer),
            ("rulePercent", "rule_percent", number),
            ("nonCompliantBurstRatePercent", "noncompliant_burst_rate_percent", number),
            ("driftFromOnePercent", "drift_from_one_percent", number),
            ("minimumSleepOperationSeconds", "minimum_sleep_operation_s", integer),
            ("minimumSleepDevelopmentSeconds", "minimum_sleep_development_s", integer),
        )),))
    elif sensor_topic and parts[3] == "telemetry":
        sensor_id = payload["mac"]
        if not isinstance(sensor_id, str) or sensor_id != parts[2]:
            raise ValueError("telemetry mac does not match topic sensor ID")
        point = Point("sensor_diagnostics").tag("sensor_id", parts[2])
        for key in ("battCurrent", "battPower", "freeRam", "cpuTemp", "txPower", "cpuFrequencyMhz", "lastSNR", "lastRSSI"):
            point = point.field(key, integer(payload[key], key))
        point = snapshot_fields(point, payload, "sensorRadio", "sensor_radio", (
            ("available", "available", boolean),
            ("frequencyMhz", "frequency_mhz", number),
            ("bandwidthKhz", "bandwidth_khz", number),
            ("spreadingFactor", "spreading_factor", integer),
            ("codingRate", "coding_rate", integer),
            ("txPowerDbm", "tx_power_dbm", integer),
            ("uplinkRssiDbm", "uplink_rssi_dbm", number),
            ("uplinkSnrDb", "uplink_snr_db", number),
            ("lastDownlinkRssiDbm", "last_downlink_rssi_dbm", number),
            ("lastDownlinkSnrDb", "last_downlink_snr_db", number),
        ))
        point = snapshot_fields(point, payload, "gatewayRadio", "gateway_radio", (
            ("available", "available", boolean),
            ("activity", "activity", string),
            ("frequencyMhz", "frequency_mhz", number),
            ("bandwidthKhz", "bandwidth_khz", number),
            ("spreadingFactor", "spreading_factor", integer),
            ("codingRate", "coding_rate", integer),
            ("txPowerDbm", "tx_power_dbm", integer),
            ("rssiDbm", "rssi_dbm", number),
            ("snrDb", "snr_db", number),
        ), nested=(("duty", (
            ("countedBurstUs", "counted_burst_us", integer),
            ("cooldownMs", "cooldown_ms", integer),
            ("totalBurstCount", "total_burst_count", integer),
            ("nonCompliantBurstCount", "noncompliant_burst_count", integer),
            ("rulePercent", "rule_percent", number),
            ("nonCompliantBurstRatePercent", "noncompliant_burst_rate_percent", number),
            ("driftFromOnePercent", "drift_from_one_percent", number),
            ("minimumSleepOperationSeconds", "minimum_sleep_operation_s", integer),
            ("minimumSleepDevelopmentSeconds", "minimum_sleep_development_s", integer),
        )),))
    elif sensor_topic:
        if not re.fullmatch(r"[0-9A-F]{2}(?::[0-9A-F]{2}){5}", parts[2]):
            raise ValueError("topic sensor ID is invalid")
        # Replayed retained status is an observation, not a new heartbeat.
        point = Point("sensor_status_retained" if retained else "sensor_status").tag("sensor_id", parts[2])
        point = point.field("online", boolean(payload["online"], "online"))
        point = point.field("sensor_connection_uptime_s", integer(payload["uptime"], "uptime"))
        point = nested_fields(point, payload, "radio", (
            ("available", "radio_available", boolean),
            ("bandwidthKhz", "radio_bandwidth_khz", number),
            ("spreadingFactor", "radio_spreading_factor", integer),
            ("codingRate", "radio_coding_rate", integer),
        ))
        point = nested_fields(point, payload, "link", (
            ("lastSeenSeconds", "link_last_seen_seconds", integer),
            ("rssiDbm", "link_rssi_dbm", number),
            ("snrDb", "link_snr_db", number),
        ))
        point = snapshot_fields(point, payload, "sensorRadio", "sensor_radio", (
            ("available", "available", boolean),
            ("frequencyMhz", "frequency_mhz", number),
            ("bandwidthKhz", "bandwidth_khz", number),
            ("spreadingFactor", "spreading_factor", integer),
            ("codingRate", "coding_rate", integer),
            ("txPowerDbm", "tx_power_dbm", integer),
            ("uplinkRssiDbm", "uplink_rssi_dbm", number),
            ("uplinkSnrDb", "uplink_snr_db", number),
            ("lastDownlinkRssiDbm", "last_downlink_rssi_dbm", number),
            ("lastDownlinkSnrDb", "last_downlink_snr_db", number),
        ))
    elif gateway_topic:
        point = Point("gateway_status_retained" if retained else "gateway_status").tag("gateway_id", parts[2]).tag("wifi_status", string(payload["wifiStatus"], "wifiStatus"))
        point = point.field("gateway_rssi", integer(payload["gatewayRssi"], "gatewayRssi"))
        point = point.field("gateway_snr", number(payload["gatewaySnr"], "gatewaySnr"))
        point = point.field("free_heap_bytes", integer(payload["freeRam"], "freeRam"))
        point = point.field("ip", string(payload["ip"], "ip"))
        point = point.field("uptime", integer(payload["uptime"], "uptime"))
        point = nested_fields(point, payload, "radio", (
            ("available", "radio_available", boolean),
            ("activity", "radio_activity", string),
            ("frequencyMhz", "radio_frequency_mhz", number),
            ("bandwidthKhz", "radio_bandwidth_khz", number),
            ("spreadingFactor", "radio_spreading_factor", integer),
            ("codingRate", "radio_coding_rate", integer),
        ))
        point = nested_fields(point, payload, "duty", (
            ("countedBurstUs", "duty_counted_burst_us", integer),
            ("cooldownMs", "duty_cooldown_ms", integer),
            ("totalBurstCount", "duty_total_burst_count", integer),
            ("nonCompliantBurstCount", "duty_noncompliant_burst_count", integer),
            ("minimumSleepOperationSeconds", "minimum_sleep_operation_s", integer),
            ("minimumSleepDevelopmentSeconds", "minimum_sleep_development_s", integer),
        ))
        point = nested_fields(point, payload, "health", (
            ("wifiRssiDbm", "wifi_rssi_dbm", integer),
            ("cpuFrequencyMhz", "cpu_frequency_mhz", integer),
            ("cpuTemperatureC", "cpu_temperature_c", number),
            ("ramTotalBytes", "ram_total_bytes", integer),
            ("ramUsedBytes", "ram_used_bytes", integer),
            ("sdReady", "sd_ready", boolean),
            ("mqttConnected", "mqtt_connected", boolean),
        ))
        sd_reported = payload.get("SD")
        if sd_reported is not None and sd_reported not in ("ok", "failed"):
            raise ValueError("SD must be 'ok' or 'failed'")
        health = payload.get("health") or {}
        sd_ready = health.get("sdReady")
        if sd_reported is not None and sd_ready is not None and (sd_reported == "ok") != sd_ready:
            sd_status = "conflict"
        elif sd_reported is not None:
            sd_status = sd_reported
        elif sd_ready is not None:
            sd_status = "ok" if sd_ready else "failed"
        else:
            sd_status = "unknown"
        point = point.field("sd_status", sd_status)
        point = nested_fields(point, payload, "network", (
            ("wifiSsid", "wifi_ssid", string),
            ("mqttHost", "mqtt_host", string),
            ("mqttPort", "mqtt_port", integer),
        ))
        point = snapshot_fields(point, payload, "gatewayRadio", "gateway_radio", (
            ("available", "available", boolean),
            ("activity", "activity", string),
            ("frequencyMhz", "frequency_mhz", number),
            ("bandwidthKhz", "bandwidth_khz", number),
            ("spreadingFactor", "spreading_factor", integer),
            ("codingRate", "coding_rate", integer),
            ("txPowerDbm", "tx_power_dbm", number),
            ("rssiDbm", "rssi_dbm", number),
            ("snrDb", "snr_db", number),
        ), nested=(("duty", (
            ("countedBurstUs", "counted_burst_us", integer),
            ("cooldownMs", "cooldown_ms", integer),
            ("totalBurstCount", "total_burst_count", integer),
            ("nonCompliantBurstCount", "noncompliant_burst_count", integer),
            ("rulePercent", "rule_percent", number),
            ("nonCompliantBurstRatePercent", "noncompliant_burst_rate_percent", number),
            ("driftFromOnePercent", "drift_from_one_percent", number),
            ("minimumSleepOperationSeconds", "minimum_sleep_operation_s", integer),
            ("minimumSleepDevelopmentSeconds", "minimum_sleep_development_s", integer),
        )),))
        relay = Point("gateway_relay_status_retained" if retained else "gateway_relay_status").tag("gateway_id", parts[2])
        for key in ("requestedClosed", "feedbackClosed", "failed", "autoOff", "durable"):
            relay = relay.field(key, boolean(payload[key], key))
        max_on = integer(payload["maxOnSeconds"], "maxOnSeconds")
        if not 1 <= max_on <= 7200:
            raise ValueError("maxOnSeconds out of range")
        relay = relay.field("maxOnSeconds", max_on)
        relay = relay.field("gateway_reported_at", reported_at).field("device_time_valid", device_time_valid).field("mqtt_retained", retained)
    point = point.field("gateway_reported_at", reported_at).field("device_time_valid", device_time_valid).field("mqtt_retained", retained)
    WRITER.write(bucket=BUCKET, record=point.time(received))
    if gateway_topic:
        WRITER.write(bucket=BUCKET, record=relay.time(received))

def on_message(client, userdata, message):
    try:
        if len(message.payload) > 2048:
            raise ValueError("payload exceeds limit")
        write(message.topic, json.loads(message.payload.decode("utf-8")), retained=getattr(message, "retain", False))
    except (UnicodeDecodeError, json.JSONDecodeError, KeyError, TypeError, ValueError) as error:
        logging.warning("Rejected MQTT message on %s: %s", message.topic, error)
        try:
            event = Point("ingestion_event").tag("reason", type(error).__name__).field("count", 1)
            WRITER.write(bucket=EVENT_BUCKET, record=event.time(datetime.now(timezone.utc)))
        except Exception:
            logging.exception("Could not record rejected MQTT message")

client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="lorahome-normalizer", clean_session=False)
client.username_pw_set(os.environ["MQTT_USER"], os.environ["MQTT_PASSWORD"])
def on_connect(client, userdata, flags, reason_code, properties):
    client.unsubscribe("lorahome/gateway/+/relay/status")
    for topic in (
        "lorahome/sensor/+/data", 
        "lorahome/sensor/+/telemetry", 
        "lorahome/sensor/+/status", 
        "lorahome/gateway/+/status"
    ):
        client.subscribe(topic, qos=1)

client.on_connect = on_connect
client.on_message = on_message
client.connect(os.environ["MQTT_HOST"], int(os.environ["MQTT_PORT"]))
client.loop_forever()


