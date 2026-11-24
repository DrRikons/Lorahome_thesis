#pragma once

#ifndef LORAHOMECOMMON_H
#define LORAHOMECOMMON_H

#include <Arduino.h> // For uint8_t, uint16_t, etc.
#include <ArduinoJson.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <math.h>

// Base time offset to reduce LoRa payload sizes (Jan 1, 2024 00:00:00 UTC)
#define CUSTOM_EPOCH 1704067200UL

constexpr uint8_t CONFIG_UPDATE_PENDING_FLAG = 0xA5;
constexpr int8_t SENSOR_TX_POWER_MIN_DBM = -20;
constexpr int8_t SENSOR_TX_POWER_MAX_DBM = 22;

inline bool isSupportedSensorTxPower(int value) {
    return value >= SENSOR_TX_POWER_MIN_DBM && value <= SENSOR_TX_POWER_MAX_DBM;
}

// Shared by Sensor and GateWay: true if currentTime has diverged from the boot-epoch
// estimate (CUSTOM_EPOCH + uptimeSeconds), meaning a real clock sync has occurred.
// Callers supply their own uptimeSeconds source (deep-sleeping devices vs long-running ones).

inline bool isClockValidSince(time_t currentTime, unsigned long uptimeSeconds) {
    time_t expectedBootTime = (time_t)CUSTOM_EPOCH + (time_t)uptimeSeconds;
    if (labs((long)(currentTime - expectedBootTime)) <= 10 || currentTime < (time_t)CUSTOM_EPOCH) {
        return false;
    }
    return true;
}

inline bool hasClockDrift(time_t currentTime, time_t referenceTime, double thresholdSeconds) {
    return fabs(difftime(currentTime, referenceTime)) > thresholdSeconds;
}

#define TELEMETRY_CORE_FIELDS(FIELD, ARRAY, STRING) \
    ARRAY(uint8_t,  mac, 6, "mac") \
    FIELD(uint32_t, bootNonce, "bootNonce") \
    FIELD(uint16_t, msgCount, "msgCount") \
    FIELD(int16_t,  temperature, "temperature") \
    FIELD(uint16_t, battVoltage, "battVoltage") \
    FIELD(uint8_t,  battPercent, "battPercent") \
    FIELD(uint8_t,  isDevMode, "isDevMode") \
    FIELD(uint8_t,  needsTimeSync, "needsTimeSync") \
    FIELD(uint8_t,  sleepInterval, "sleepInterval") \
    FIELD(int8_t,   txPower, "txPower") \
    FIELD(uint8_t,  cpuFrequencyMhz, "cpuFrequencyMhz")

#define TELEMETRY_DEV_FIELDS(FIELD, ARRAY, STRING) \
    FIELD(int16_t,  battCurrent, "battCurrent") \
    FIELD(int16_t,  battPower, "battPower") \
    FIELD(uint16_t, freeRam, "freeRam") \
    FIELD(int8_t,   cpuTemp, "cpuTemp") \
    FIELD(int8_t,   lastSNR, "lastSNR") \
    FIELD(int16_t,  lastRSSI, "lastRSSI")

#define CONFIG_PAYLOAD(FIELD, ARRAY, STRING) \
   STRING(header, 2, "header") \
   ARRAY(uint8_t,  targetMac, 6, "targetMac") \
   FIELD(uint32_t, networkKey, "networkKey") \
   FIELD(uint8_t,  sleepInterval, "sleepInterval") \
   FIELD(uint8_t,  configVersion, "configVersion") \
   FIELD(uint8_t,  isDevMode, "isDevMode") \
   FIELD(int8_t,   txPower, "txPower") \
   FIELD(uint8_t,  cpuFrequencyMhz, "cpuFrequencyMhz") \
   FIELD(uint32_t, timeOffset, "timeOffset")

#define GATEWAY_STATUS(FIELD, ARRAY, STRING) \
   FIELD(int16_t,  gatewayRssi, "gatewayRssi") \
   FIELD(float,    gatewaySnr, "gatewaySnr") \
   STRING(wifiStatus, 20, "wifiStatus") \
   STRING(ip, 16, "ip") \
   FIELD(uint32_t, freeRam, "freeRam")

// Define how to declare a variable inside the struct
#define DECLARE_FIELD(type, name, json_key) type name;
#define DECLARE_ARRAY(type, name, size, json_key) type name[size];
#define DECLARE_STRING(name, size, json_key) char name[size];

struct __attribute__((packed)) TelemetryPayload {
    // --- Core variables ---
    TELEMETRY_CORE_FIELDS(DECLARE_FIELD, DECLARE_ARRAY, DECLARE_STRING)
    
    // --- Dev mode variables ---
    TELEMETRY_DEV_FIELDS(DECLARE_FIELD, DECLARE_ARRAY, DECLARE_STRING)

};

struct __attribute__((packed)) ConfigPayload {

    // --- Config Payload ---
    CONFIG_PAYLOAD(DECLARE_FIELD, DECLARE_ARRAY, DECLARE_STRING)

};

// The Gateway waits this long after the uplink RX IRQ. The Sensor arms beacon
// RX immediately after its matching TX-complete IRQ, before any bookkeeping.
constexpr uint32_t SENSOR_RX_READY_GUARD_MS = 50;
constexpr uint32_t SENSOR_BEACON_RX_MARGIN_MS = 1000;

struct __attribute__((packed)) GatewayStatus {

    unsigned long uptime;

    // --- Config Payload ---
    GATEWAY_STATUS(DECLARE_FIELD, DECLARE_ARRAY, DECLARE_STRING)

};

constexpr size_t TELEMETRY_CORE_SIZE = offsetof(TelemetryPayload, battCurrent);
static_assert(TELEMETRY_CORE_SIZE == 22, "Telemetry core wire format must be 22 bytes");
static_assert(sizeof(TelemetryPayload) == 32, "TelemetryPayload wire format must be 32 bytes");
static_assert(sizeof(ConfigPayload) == 21, "ConfigPayload wire format must be 21 bytes");

constexpr uint8_t TELEMETRY_FRAME_VERSION = 0x01;
constexpr uint8_t TELEMETRY_FRAME_TYPE = 0x01;
constexpr size_t TELEMETRY_FRAME_PREFIX_SIZE = 14;
constexpr size_t TELEMETRY_FRAME_TAG_SIZE = 8;
constexpr size_t TELEMETRY_AUTH_CORE_SIZE = TELEMETRY_FRAME_PREFIX_SIZE +
                                             (TELEMETRY_CORE_SIZE - 12) + TELEMETRY_FRAME_TAG_SIZE;
constexpr size_t TELEMETRY_AUTH_FULL_SIZE = TELEMETRY_FRAME_PREFIX_SIZE +
                                             (sizeof(TelemetryPayload) - 12) + TELEMETRY_FRAME_TAG_SIZE;

// Radio-profile frames are introduced separately from the legacy configuration.
constexpr uint8_t RADIO_PROFILE_FRAME_VERSION = 0x01;
constexpr uint8_t RADIO_PROFILE_REQUEST_TYPE = 0x02;
constexpr uint8_t RADIO_PROFILE_ACK_TYPE = 0x03;
constexpr uint8_t RADIO_PROFILE_TELEMETRY_TYPE = 0x04;
constexpr size_t RADIO_PROFILE_TAG_SIZE = 8;

struct __attribute__((packed)) RadioProfile {
    uint8_t spreadingFactor;
    uint8_t bandwidthIndex;
    uint8_t codingRate;
};

struct __attribute__((packed)) RadioProfileRequest {
    uint8_t version;
    uint8_t type;
    uint8_t sensorMac[6];
    uint32_t sessionNonce;
    uint16_t uplinkCounter;
    RadioProfile profile;
    uint8_t tag[RADIO_PROFILE_TAG_SIZE];
};

struct __attribute__((packed)) RadioProfileAck {
    uint8_t version;
    uint8_t type;
    uint8_t tag[RADIO_PROFILE_TAG_SIZE];
};

static_assert(sizeof(RadioProfile) == 3, "Radio profile wire format must be 3 bytes");
static_assert(sizeof(RadioProfileRequest) == 25, "Radio profile request must be 25 bytes");
static_assert(sizeof(RadioProfileAck) == 10, "Radio profile ACK must be 10 bytes");
constexpr size_t RADIO_PROFILE_TELEMETRY_CORE_SIZE = TELEMETRY_AUTH_CORE_SIZE + sizeof(RadioProfile);
constexpr size_t RADIO_PROFILE_TELEMETRY_FULL_SIZE = TELEMETRY_AUTH_FULL_SIZE + sizeof(RadioProfile);
constexpr uint8_t RADIO_PROFILE_OPERATION_VERSION = 0x02;
constexpr size_t TELEMETRY_OPERATION_SIZE = TELEMETRY_CORE_SIZE - 1;
constexpr size_t RADIO_PROFILE_OPERATION_SIZE = RADIO_PROFILE_TELEMETRY_CORE_SIZE - 1;
constexpr uint8_t CONFIG_CPU_UNCHANGED = 0;
static_assert(TELEMETRY_OPERATION_SIZE == 21 && RADIO_PROFILE_OPERATION_SIZE == 34,
              "Operation telemetry must omit the CPU frequency byte");

struct TelemetryCoreJsonView {
    const TelemetryPayload& payload;
};

struct TelemetryDevJsonView {
    const TelemetryPayload& payload;
};

inline void addJsonByteArray(JsonObject object, const char* key, const uint8_t* values, size_t count) {
    JsonArray array = object[key].to<JsonArray>();
    for (size_t i = 0; i < count; ++i) array.add(values[i]);
}

template <size_t N>
inline void addJsonFixedString(JsonObject object, const char* key, const char (&value)[N]) {
    char terminated[N + 1];
    memcpy(terminated, value, N);
    terminated[N] = '\0';
    object[key] = terminated;
}

inline void convertToJson(const TelemetryCoreJsonView& source, JsonVariant destination) {
    const TelemetryPayload& payload = source.payload;
    JsonObject object = destination.to<JsonObject>();
    addJsonByteArray(object, "mac", payload.mac, sizeof(payload.mac));
    object["bootNonce"] = payload.bootNonce;
    object["msgCount"] = payload.msgCount;
    object["temperature"] = payload.temperature;
    object["battVoltage"] = payload.battVoltage;
    object["battPercent"] = payload.battPercent;
    object["isDevMode"] = payload.isDevMode;
    object["needsTimeSync"] = payload.needsTimeSync & 0x01;
    object["sleepInterval"] = payload.sleepInterval;
    object["txPower"] = payload.txPower;
}

inline void convertToJson(const TelemetryDevJsonView& source, JsonVariant destination) {
    const TelemetryPayload& payload = source.payload;
    JsonObject object = destination.to<JsonObject>();
    object["txPower"] = payload.txPower;
    object["battCurrent"] = payload.battCurrent;
    object["battPower"] = payload.battPower;
    object["freeRam"] = payload.freeRam;
    object["cpuTemp"] = payload.cpuTemp;
    object["lastSNR"] = payload.lastSNR;
    object["lastRSSI"] = payload.lastRSSI;
    object["cpuFrequencyMhz"] = payload.cpuFrequencyMhz;
}

inline void convertToJson(const GatewayStatus& payload, JsonVariant destination) {
    JsonObject object = destination.to<JsonObject>();
    object["uptime"] = payload.uptime;
    object["gatewayRssi"] = payload.gatewayRssi;
    object["gatewaySnr"] = payload.gatewaySnr;
    addJsonFixedString(object, "wifiStatus", payload.wifiStatus);
    addJsonFixedString(object, "ip", payload.ip);
    object["freeRam"] = payload.freeRam;
}

// ============================================================================
// CLEANUP - Remove all macros at END of file to prevent namespace pollution
// ============================================================================
#undef DECLARE_FIELD  
#undef DECLARE_ARRAY  
#undef DECLARE_STRING

#endif // LORAHOMECOMMON_H
