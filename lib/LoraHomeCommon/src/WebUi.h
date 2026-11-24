#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "LoraHomeCommon.h"

bool isValidSensorMac(const char* mac);

constexpr size_t WEBUI_MAX_SENSORS = 1;
#if defined(T3_V1_6_SX1276)
constexpr int GATEWAY_SENSOR_TX_POWER_MIN_DBM = 2;
constexpr int GATEWAY_SENSOR_TX_POWER_MAX_DBM = 15;
#else
constexpr int GATEWAY_SENSOR_TX_POWER_MIN_DBM = -128;
constexpr int GATEWAY_SENSOR_TX_POWER_MAX_DBM = 127;
#endif

struct GatewayWebSensorSnapshot {
    char mac[18] = {};
    bool online = false;
    uint32_t lastSeenSeconds = 0;
    uint32_t onlineSeconds = 0;
    uint32_t sleepSeconds = 0;
    uint16_t messageCount = 0;
    float temperatureC = 0.0f;
    float batteryVoltage = 0.0f;
    uint8_t batteryPercent = 0;
    bool developmentMode = false;
    bool needsTimeSync = false;
    int8_t txPower = 0;
    uint8_t cpuFrequencyMhz = 0;
    int16_t lastRssi = 0;
    int8_t lastSnr = 0;
    bool debugTelemetryAvailable = false;
    int16_t debugCurrentMa = 0;
    int16_t debugPowerMw = 0;
    int8_t debugCpuTemp = 0;
    uint16_t debugFreeRam = 0;
    int8_t debugLastSnr = 0;
    int16_t debugLastRssi = 0;
    bool configurationPending = false;
    bool configurationConfirmed = false;
    bool mqttResultPending = false;
    char configurationOrigin[9] = {};
    uint8_t desiredSleepSeconds = 0;
    bool desiredDevelopmentMode = false;
    int8_t desiredTxPower = 0;
    uint8_t desiredCpuFrequencyMhz = 0;
    bool radioProfileAvailable = false;
    uint8_t radioProfileSf = 0;
    float radioProfileBandwidthKhz = 0.0f;
    uint8_t radioProfileCodingRate = 0;
    bool radioProfilePending = false;
    char radioProfileStatus[16] = {};
};

struct GatewayWebSnapshot {
    uint32_t uptimeSeconds = 0;
    uint32_t freeHeap = 0;
    uint32_t cpuFrequencyMhz = 0;
    char wifiStatus[24] = {};
    char ip[16] = {};
    int32_t wifiRssi = 0;
    bool mqttConnected = false;
    bool sdReady = false;
    float radioFrequencyMhz = 0.0f;
    float radioBandwidthKhz = 0.0f;
    uint8_t spreadingFactor = 0;
    uint8_t codingRate = 0;
    bool radioAvailable = false;
    int8_t gatewayTxPower = 0;
    bool radioProfilePending = false;
    char radioProfileStatus[16] = {};
    int16_t lastRadioRssi = 0;
    float lastRadioSnr = 0.0f;
    uint32_t countedBurstUs = 0;
    uint32_t gatewayCooldownRemainingMs = 0;
    uint32_t operationMinimumSleepSeconds = 0;
    uint32_t totalBurstCount = 0;
    uint32_t nonCompliantBurstCount = 0;
    float nonCompliantBurstRatePercent = 0.0f;
    float driftFromOnePercent = 0.0f;
    char discoveredSensorMac[18] = {};
    bool discoveredSensorAuthenticated = false;
    uint8_t sensorCount = 0;
    bool relayRequestedClosed = false;
    bool relayFeedbackClosed = false;
    bool relayFailed = false;
    bool relayAutoOff = false;
    bool relayDurable = false;
    uint32_t relayMaxOnSeconds = 7200;
    uint32_t relayRemainingSeconds = 0;
    GatewayWebSensorSnapshot sensors[WEBUI_MAX_SENSORS];
};

struct GatewayWebConfigSnapshot {
    bool wifiPasswordConfigured = false;
    char wifiSsid[33] = {};
    char mqttHost[129] = {};
    uint16_t mqttPort = 8883;
    char mqttUser[65] = {};
    bool mqttPasswordConfigured = false;
    char pairedSensorMac[18] = {};
    uint8_t defaultSleepSeconds = 0;
    bool defaultDevMode = false;
    int8_t defaultTxPower = 0;
    int8_t gatewayTxPower = 0;
    uint8_t defaultCpuFrequencyMhz = 0;
};

enum class GatewayWebCommandType : uint8_t {
    None,
    SensorConfig,
    RelayConfig,
    GatewayConfig,
    ChangePassword
};

struct GatewayWebCommand {
    GatewayWebCommandType type = GatewayWebCommandType::None;
    uint32_t operationId = 0;
    char sensorMac[18] = {};
    bool hasSleep = false;
    uint8_t sleepSeconds = 0;
    bool hasDevelopmentMode = false;
    bool developmentMode = false;
    bool hasTxPower = false;
    int8_t txPower = 0;
    bool hasCpuFrequency = false;
    uint8_t cpuFrequencyMhz = 0;
    bool hasRadioProfile = false;
    RadioProfile radioProfile{};
    bool hasRelayClosed = false;
    bool relayClosed = false;
    bool hasRelayMaxOn = false;
    uint32_t relayMaxOnSeconds = 7200;
    bool inheritDefaults = false;
    bool hasWifiSsid = false;
    char wifiSsid[33] = {};
    bool hasWifiPassword = false;
    char wifiPassword[65] = {};
    bool hasMqttHost = false;
    char mqttHost[129] = {};
    bool hasMqttPort = false;
    uint16_t mqttPort = 8883;
    bool hasMqttUser = false;
    char mqttUser[65] = {};
    bool hasMqttPassword = false;
    char mqttPassword[129] = {};
    bool hasCommandSecret = false;
    char commandSecret[129] = {};
    bool hasDefaultSleep = false;
    uint8_t defaultSleepSeconds = 0;
    bool hasDefaultDevelopmentMode = false;
    bool defaultDevelopmentMode = false;
    bool hasDefaultTxPower = false;
    int8_t defaultTxPower = 0;
    bool hasGatewayTxPower = false;
    int8_t gatewayTxPower = 0;
    bool hasDefaultCpuFrequency = false;
    uint8_t defaultCpuFrequencyMhz = 0;
    bool hasPairedSensorMac = false;
    char pairedSensorMac[18] = {};
    char currentPassword[65] = {};
    char newPassword[65] = {};
};

struct GatewayWebOperationResult {
    uint32_t operationId = 0;
    char status[12] = {};
    char message[72] = {};
};

class WebUi {
public:
    using ActionLogger = void (*)(const char* operation, const char* outcome);
    using CommandHandler = void (*)(const GatewayWebCommand& command);
    using SnapshotProvider = void (*)(GatewayWebSnapshot& snapshot, GatewayWebConfigSnapshot& config);

    bool begin(const char* adminPassword);
    void setActionLogger(ActionLogger logger);
    void setCommandHandler(CommandHandler handler);
    void setSnapshotProvider(SnapshotProvider provider);
    void service();
    void logAction(const char* operation, const char* outcome) const;
    void stop();
    bool running() const;
    void updateSnapshot(const GatewayWebSnapshot& snapshot);
    void updateConfigSnapshot(const GatewayWebConfigSnapshot& snapshot);
    bool pollCommand(GatewayWebCommand& command);
    void updateOperationResult(uint32_t operationId, const char* status, const char* message);
    bool getOperationResult(uint32_t operationId, GatewayWebOperationResult& result) const;
    uint32_t queuedCommandBoundary() const;
    uint8_t discardOlderSensorConfigs(const char* mac, uint32_t boundary);
    void appendLogLine(const char* line);
    void appendMqttEvent(const char* direction, const char* topic, const char* payload, size_t payloadLength = 0);
    bool prepareAdminPassword(const char* password);
    bool commitPreparedAdminPassword(bool& verifierPersisted);
    void discardPreparedAdminPassword();
    bool applyAdminPassword(const char* password, bool& verifierPersisted);
    const char* bootstrapCredential() const;

private:
    bool startServer();
    CommandHandler commandHandler = nullptr;
    SnapshotProvider snapshotProvider = nullptr;
};

extern WebUi gatewayWebUi;
