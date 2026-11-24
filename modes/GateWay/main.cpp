#include <Arduino.h>
#include <RadioLib.h>
#include <LoRaBoards.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <sys/time.h>
#include <time.h>
#include <LoRaHomeCommon.h>
#include <LoRaDutyCycle.h>
#include <RadioProfileSettings.h>
#include <LoraHomeCrypto.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <LogSink.h>
#include <WebUi.h>
#include <uptime.h>
#include <secrets.h> // WiFi and MQTT credentials

#ifndef LORAHOME_PRODUCTION_BUILD
#define LORAHOME_PRODUCTION_BUILD 0
#endif

// NTP Configuration
#define TZ_INFO "EET-2EEST,M3.5.0/3,M10.5.0/4" // Europe/Athens
// MQTT Configuration
// Creds moved to secrets.h
#define MQTT_PORT 8883              // Default port for MQTT over TLS
#define MQTT_TOPIC_PREFIX "lorahome" 

#if     defined(USING_SX1276)
#if defined(T3_V1_6_SX1276)
#ifdef CONFIG_RADIO_FREQ
#undef CONFIG_RADIO_FREQ
#endif
#define CONFIG_RADIO_FREQ           RADIO_PROFILE_EU868_CENTER_FREQUENCY_MHZ
#else
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           868.0
#endif
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   14
#endif
#if defined(T3_V1_6_SX1276) && (CONFIG_RADIO_OUTPUT_POWER < 2 || CONFIG_RADIO_OUTPUT_POWER > 15)
#undef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   14
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             125.0
#endif
SX1276 radio = new Module(RADIO_CS_PIN, RADIO_DIO0_PIN, RADIO_RST_PIN, RADIO_DIO1_PIN);

#elif   defined(USING_SX1278)
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           433.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   17
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             125.0
#endif
SX1278 radio = new Module(RADIO_CS_PIN, RADIO_DIO0_PIN, RADIO_RST_PIN, RADIO_DIO1_PIN);

#elif   defined(USING_SX1262)
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           850.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   22
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             125.0
#endif

SX1262 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

#elif   defined(USING_SX1280)
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           2400.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   13
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             203.125
#endif
SX1280 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

#elif  defined(USING_SX1280PA)
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           2400.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   3           // PA Version power range : -18 ~ 3dBm
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             203.125
#endif
SX1280 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

#elif   defined(USING_SX1268)
#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ           433.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER   22
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW             125.0
#endif
SX1268 radio = new Module(RADIO_CS_PIN, RADIO_DIO1_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

#elif   defined(USING_LR1121)

/*
* Important: LR1121 PA Version
*
* The 2.4G version does not have a power amplifier (PA). The permissible power setting is 13dBm.
*
* If it is a version with a built-in PA, please do not exceed 0dBm in the maximum power setting.
* This is because a power amplifier has been added to the RF front-end; setting it to 0dBm will achieve an output power of 22dBm.
* Setting it to more than 1dBm may damage the PA.
*
* */

#define CONFIG_RADIO_FREQ           2450.0
#define CONFIG_RADIO_OUTPUT_POWER   LILYGO_RADIO_2G4_TX_POWER_LIMIT
#define CONFIG_RADIO_BW             125.0

// The maximum power of LR1121 Sub 1G band can only be set to 22 dBm
// #define CONFIG_RADIO_FREQ           868.0
// #define CONFIG_RADIO_OUTPUT_POWER   22
// #define CONFIG_RADIO_BW             125.0

LR1121 radio = new Module(RADIO_CS_PIN, RADIO_DIO9_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

#ifdef USING_LR1121PA
// LR1121 Version PA RF switch table
static const uint32_t pa_version_rf_switch_dio_pins[] = {
    RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7, RADIOLIB_LR11X0_DIO8, RADIOLIB_NC
};

static const Module::RfSwitchMode_t high_freq_switch_table[] = {
    // mode                  DIO5  DIO6 DIO7 DIO8
    { LR11x0::MODE_STBY,   { LOW,  LOW, LOW, LOW} },
    { LR11x0::MODE_TX,     { LOW,  LOW, LOW, HIGH} },
    { LR11x0::MODE_RX,     { LOW,  LOW, HIGH, LOW} },
    { LR11x0::MODE_TX_HP,  { LOW,  LOW, HIGH, LOW} },
    { LR11x0::MODE_TX_HF,  { LOW,  LOW, HIGH, LOW} },
    { LR11x0::MODE_GNSS,   { LOW,  LOW, LOW, HIGH} },
    { LR11x0::MODE_WIFI,   { LOW,  LOW, LOW, HIGH} },
    END_OF_MODE_TABLE,
};

static const Module::RfSwitchMode_t low_freq_switch_table[] = {
    // mode                  DIO5  DIO6 DIO7 DIO8
    { LR11x0::MODE_STBY,   { LOW,  LOW, LOW, LOW} },
    { LR11x0::MODE_TX,     { LOW,  HIGH, LOW, LOW} },
    { LR11x0::MODE_RX,     { HIGH, LOW, LOW, LOW} },
    { LR11x0::MODE_TX_HP,  { LOW,  HIGH, LOW, LOW} },
    { LR11x0::MODE_TX_HF,  { LOW,  LOW, LOW, LOW} },
    { LR11x0::MODE_GNSS,   { LOW,  LOW, LOW, LOW} },
    { LR11x0::MODE_WIFI,   { LOW,  LOW, LOW, LOW} },
    END_OF_MODE_TABLE,
};

#endif /*USING_LR1121PA*/
#endif /*Radio define end*/

// FreeRTOS semaphore to replace the busy-wait flag
static SemaphoreHandle_t radioSemaphore = NULL;
static String rssi = "0dBm";
static String snr = "0dB";
// Display variables
static float sensorTemp = 0.0;
static float sensorVcc = 0.0;
static uint8_t sensorBatt = 0;
static String sensorMac = "Wait...";
static uint16_t msgCount = 0;
static uint8_t sensorIsDevMode = 0;
static bool sensorNeedsTimeSync = false;
static uint32_t sensorSleepInterval = 0;
static uint32_t lastDisplayUpdate = 0;
static uint8_t screenNum = 0;
static bool hasValidRtcTime = false;
static long lastMqttReconnectAttempt = 0;
static long lastWifiReconnectAttempt = 0;
// MQTT connection state tracking for reconnection logic
static const unsigned long MAX_RECONNECT_ATTEMPTS = 5;   // Maximum retry attempts
static const unsigned long RECONNECT_DELAY_MS = 1000;    // Delay between retries (1 second)
static const unsigned long MAX_MQTT_RECONNECT_DELAY_MS = 60000;
static const unsigned long STATUS_MIN_INTERVAL_MS = 30000;
static const unsigned long STATUS_HEARTBEAT_MS = 120000;
static const unsigned long SENSOR_STATUS_GRACE_MS = 5000;
static const uint8_t MAX_TRACKED_SENSORS = 1;
static const uint8_t MAX_PROCESSED_COMMANDS = 16;
static unsigned long mqttReconnectDelayMs = RECONNECT_DELAY_MS;
static const unsigned long WIFI_RECONNECT_DELAY_MS = 10000;  
static const unsigned long NTP_SYNC_DELAY_MS = 30000;  // Delay between NTP Syncs (30 seconds)
static const unsigned long SD_RETRY_INTERVAL_MS = 5000;
static unsigned long lastSdRetryMs = 0;
static uint32_t gatewayNextTxMs = 0;
static uint32_t gatewayBurstMeasuredUs = 0;
static bool gatewayHasSentAfterSensorRx = false;
static bool gatewayRadioAvailable = false;
static volatile TickType_t lastRadioIrqTick = 0;
static bool gatewayBurstNonCompliant = false;

// Gateway-local relay wiring for the selected T3 V1.6 board. GPIO12 is an
// ESP32 strapping pin, so it is left passive until after boot completes.
static constexpr uint8_t GATEWAY_RELAY_SET_PIN = 4;
static constexpr uint8_t GATEWAY_RELAY_RESET_PIN = 25;
static constexpr uint8_t GATEWAY_RELAY_FEEDBACK_PIN = 12;
static constexpr uint32_t GATEWAY_RELAY_DEFAULT_MAX_ON_SECONDS = 7200;
struct GatewayRelayState {
    bool requestedClosed = false;
    bool feedbackClosed = false;
    bool failed = false;
    bool autoOff = false;
    bool durable = false;
    uint32_t maxOnSeconds = GATEWAY_RELAY_DEFAULT_MAX_ON_SECONDS;
    uint32_t deadline = 0;
    uint8_t lastTrigger = 0;
    bool recoveryRequired = false;
};
static GatewayRelayState gatewayRelay;
static bool gatewayRelayStatusPending = true;
static time_t lastRelayCountdownLog = 0;

#define RTC_GATEWAY_AIRTIME_MAGIC 0x47574155
struct GatewayAirtimeState {
    uint32_t magicWord;
    RadioProfile radioProfile;
    uint32_t calculatedBeaconUs;
    uint32_t calculatedConfigUs;
    uint32_t measuredBurstUs;
    uint32_t countedBurstUs;
    uint32_t nonCompliantBurstCount;
    uint32_t totalBurstCount;
};
RTC_NOINIT_ATTR GatewayAirtimeState rtcGatewayAirtime;

struct SensorPresence {
    char mac[18] = {};
    unsigned long lastPacketMs = 0;
    unsigned long onlineSinceMs = 0;
    uint32_t sleepIntervalSeconds = 0;
    bool online = false;
    uint32_t lastBootNonce = 0;
    bool bootNonceKnown = false;
    bool authenticatedProtocol = false;
    bool radioProfileKnown = false;
    RadioProfile radioProfile{};
    uint16_t lastAuthenticatedMsgCount = 0;
    bool hasSleepOverride = false;
    uint8_t sleepOverrideSeconds = 0;
    bool hasDevModeOverride = false;
    bool devModeOverride = false;
    bool hasTxPowerOverride = false;
    int8_t txPowerOverride = 0;
    bool hasCpuFrequencyOverride = false;
    uint8_t cpuFrequencyOverride = 80;
    bool configurationPending = false;
    bool pendingCpuChange = false;
    bool pendingModeChange = false;
    char pendingCommandId[37] = {};
    char configurationOrigin[9] = {};
    float temperatureC = 0.0f;
    float batteryVoltage = 0.0f;
    uint8_t batteryPercent = 0;
    uint16_t messageCount = 0;
    bool developmentMode = false;
    bool needsTimeSync = false;
    int8_t reportedTxPower = 0;
    uint8_t reportedCpuFrequencyMhz = 0;
    int16_t lastRssi = 0;
    int8_t lastSnr = 0;
    bool linkObserved = false;
    bool linkSignalValid = false;
    int16_t uplinkRssi = 0;
    float uplinkSnr = 0;
    int64_t lastObservedUs = 0;
    bool statusPending = true;
    unsigned long lastStatusPublishMs = 0;
    bool debugTelemetryAvailable = false;
    int16_t debugCurrentMa = 0;
    int16_t debugPowerMw = 0;
    int8_t debugCpuTemp = 0;
    uint16_t debugFreeRam = 0;
    int8_t debugLastSnr = 0;
    int16_t debugLastRssi = 0;
};

struct SensorStatus {
    bool online;
    unsigned long uptime;
};

inline void convertToJson(const SensorStatus& status, JsonVariant destination) {
    JsonObject object = destination.to<JsonObject>();
    object["online"] = status.online;
    object["uptime"] = status.uptime;
}

static SensorPresence sensorPresence[MAX_TRACKED_SENSORS];
struct GatewayRadioProfileState {
    RadioProfile active = RADIO_PROFILE_DEFAULT;
    RadioProfile requested = RADIO_PROFILE_DEFAULT;
    RadioProfileRequest request{};
    bool pending = false;
    bool awaitingAck = false;
    uint8_t phase = 0;
    char status[16] = "applied";
    char commandId[37] = {};
};
static GatewayRadioProfileState gatewayRadioProfile;
static uint32_t gatewayProfileAckDeadlineMs = 0;
static char discoveredSensorMac[18] = {};
static bool discoveredSensorAuthenticated = false;
static char processedCommandIds[MAX_PROCESSED_COMMANDS][37] = {};
static time_t processedCommandExpiry[MAX_PROCESSED_COMMANDS] = {};
static uint8_t nextProcessedCommandSlot = 0;

struct GatewayConfig {
    String wifiSsid;
    String wifiPassword;
    String mqttHost;
    uint16_t mqttPort = MQTT_PORT;
    String mqttUser;
    String mqttPassword;
    String controlCommandSecret = CONTROL_COMMAND_SECRET;
    String webAdminPassword = WEBUI_ADMIN_PASSWORD;
    uint8_t sensorSleepSeconds = 21;
    bool sensorDevMode = false;
    int8_t sensorTxPower = CONFIG_RADIO_OUTPUT_POWER;
    int8_t radioTxPower = CONFIG_RADIO_OUTPUT_POWER;
    uint8_t sensorCpuFrequencyMhz = 80;
    String pairedSensorMac;
};

static GatewayConfig gatewayConfig;
static bool gatewayConfigFromNvs = false;
static String mqttRootCa;
static bool gatewayRadioOutputPowerApplied = false;
static int8_t gatewayAppliedRadioTxPower = 0;

static bool isPlaceholderCredential(const String& value) {
    return value.length() == 0 ||
           value.startsWith("YOUR_") ||
           value == "PASSWORD" ||
           value == "PRIVATE_SHARED_SECRET" ||
           value == "12_OR_MORE_CHARACTERS";
}

static bool gatewayCredentialsProvisioned() {
    if (NETWORK_KEY == 0 || !LoraHomeCrypto::isConfiguredAesKey(AES_NETWORK_KEY)) return false;
    if (isPlaceholderCredential(gatewayConfig.wifiSsid) ||
        isPlaceholderCredential(gatewayConfig.mqttHost) ||
        isPlaceholderCredential(gatewayConfig.controlCommandSecret) ||
        isPlaceholderCredential(gatewayConfig.webAdminPassword)) return false;
    if (gatewayConfig.wifiPassword.length() && isPlaceholderCredential(gatewayConfig.wifiPassword)) return false;
    if (gatewayConfig.mqttUser.length() && isPlaceholderCredential(gatewayConfig.mqttUser)) return false;
    if (gatewayConfig.mqttPassword.length() && isPlaceholderCredential(gatewayConfig.mqttPassword)) return false;
    return true;
}

struct DesiredSensorConfig {
    uint8_t sleepInterval;
    bool developmentMode;
    int8_t txPower;
    uint8_t cpuFrequencyMhz;
};

static DesiredSensorConfig desiredSensorConfig(const SensorPresence* sensor) {
    return {
        (sensor && sensor->hasSleepOverride) ? sensor->sleepOverrideSeconds : gatewayConfig.sensorSleepSeconds,
        (sensor && sensor->hasDevModeOverride) ? sensor->devModeOverride : gatewayConfig.sensorDevMode,
        (sensor && sensor->hasTxPowerOverride) ? sensor->txPowerOverride : gatewayConfig.sensorTxPower,
        (sensor && sensor->hasCpuFrequencyOverride) ? sensor->cpuFrequencyOverride : gatewayConfig.sensorCpuFrequencyMhz,
    };
}

static bool telemetryConfirmsConfig(const TelemetryPayload& payload, const DesiredSensorConfig& desired, const SensorPresence* sensor) {
    return payload.sleepInterval == desired.sleepInterval &&
           (payload.isDevMode != 0) == desired.developmentMode &&
           payload.txPower == desired.txPower &&
           (!(sensor && sensor->pendingCpuChange) ||
            (payload.isDevMode != 0 && payload.cpuFrequencyMhz == desired.cpuFrequencyMhz));
}

// Forward Function declarations
void setup();
void loop();
void drawMain();
void configureSystemTime();
bool refreshRtcTrustFromSystemClock();
bool mqttPublish(const char* topic, JsonDocument& doc, bool retained = false);
bool sendUpdateBeacon();
bool isClockValid(time_t currentTime);
void mqttConnect();
String mqttTopic(const String* mac, const char* mode);
void addJsonMetadata(JsonDocument& doc, const String* mac);
void initRadio();
bool loadGatewayConfig(bool sdAvailable, bool* created = nullptr);
bool configureMqttTls(bool sdAvailable);
void maintainSdCard();
bool tryMountSdCard();
void applyNetworkConfiguration();
SensorPresence* updateSensorPresence(const char* mac, uint32_t sleepIntervalSeconds,
                                     uint32_t receivedAtMs, uint32_t bootNonce);
bool publishSensorStatus(SensorPresence& presence);
SensorPresence* findSensorPresence(const char* mac);
void mqttCallback(char* topic, byte* payload, unsigned int length);
void handleMqttConfigCommand(const char* topic, const byte* payload, unsigned int length);
static String gatewayId();
bool publishConfigResult(const char* mac, const char* commandId, const char* status);
uint32_t sensorMinimumTotalIntervalSeconds(bool developmentMode);
bool TxCycleCheck();
void finishGatewayTxBurst();
void handleWebUiCommand(const GatewayWebCommand& command);
void provideWebUiSnapshots(GatewayWebSnapshot& snapshot, GatewayWebConfigSnapshot& config);
bool saveGatewayConfig();
static bool isValidCommandId(const char* commandId);

template <typename T>
void jsonBuild(const T& payload, JsonDocument& doc, const String* mac = nullptr) {
    doc.clear();
    if (!doc.set(payload)) {
        GW_LOG_ERROR("JSON conversion failed");
        return;
    }
    addJsonMetadata(doc, mac);
    if (doc.overflowed()) GW_LOG_ERROR("JSON document capacity exceeded");
}

void populateSensorRadioSnapshot(JsonObject radioObject, const SensorPresence* presence,
                                 bool includeLastDownlinkMetrics) {
    const bool available = presence && presence->radioProfileKnown;
    radioObject["available"] = available;
    if (available) {
        radioObject["frequencyMhz"] = CONFIG_RADIO_FREQ;
        radioObject["bandwidthKhz"] = RADIO_PROFILE_BANDWIDTHS_KHZ[presence->radioProfile.bandwidthIndex];
        radioObject["spreadingFactor"] = presence->radioProfile.spreadingFactor;
        radioObject["codingRate"] = presence->radioProfile.codingRate;
    } else {
        radioObject["frequencyMhz"] = nullptr;
        radioObject["bandwidthKhz"] = nullptr;
        radioObject["spreadingFactor"] = nullptr;
        radioObject["codingRate"] = nullptr;
    }
    if (presence) radioObject["txPowerDbm"] = presence->reportedTxPower;
    else radioObject["txPowerDbm"] = nullptr;
    if (presence && presence->linkSignalValid) {
        radioObject["uplinkRssiDbm"] = presence->uplinkRssi;
        radioObject["uplinkSnrDb"] = presence->uplinkSnr;
    } else {
        radioObject["uplinkRssiDbm"] = nullptr;
        radioObject["uplinkSnrDb"] = nullptr;
    }
    if (includeLastDownlinkMetrics && presence && presence->debugTelemetryAvailable) {
        radioObject["lastDownlinkRssiDbm"] = presence->debugLastRssi;
        radioObject["lastDownlinkSnrDb"] = presence->debugLastSnr;
    } else {
        radioObject["lastDownlinkRssiDbm"] = nullptr;
        radioObject["lastDownlinkSnrDb"] = nullptr;
    }
}

void populateGatewayRadioSnapshot(JsonObject radioObject) {
    radioObject["available"] = gatewayRadioAvailable;
    if (gatewayRadioAvailable) {
        radioObject["activity"] = (int32_t)(gatewayNextTxMs - millis()) > 0 ? "cooldown" : "listening";
        radioObject["frequencyMhz"] = CONFIG_RADIO_FREQ;
        radioObject["bandwidthKhz"] = RADIO_PROFILE_BANDWIDTHS_KHZ[gatewayRadioProfile.active.bandwidthIndex];
        radioObject["spreadingFactor"] = gatewayRadioProfile.active.spreadingFactor;
        radioObject["codingRate"] = gatewayRadioProfile.active.codingRate;
        if (gatewayRadioOutputPowerApplied) radioObject["txPowerDbm"] = gatewayAppliedRadioTxPower;
        else radioObject["txPowerDbm"] = nullptr;
        const float rssi = radio.getRSSI();
        const float snr = radio.getSNR();
        if (isfinite(rssi)) radioObject["rssiDbm"] = rssi;
        else radioObject["rssiDbm"] = nullptr;
        if (isfinite(snr)) radioObject["snrDb"] = snr;
        else radioObject["snrDb"] = nullptr;
    } else {
        radioObject["activity"] = nullptr;
        radioObject["frequencyMhz"] = nullptr;
        radioObject["bandwidthKhz"] = nullptr;
        radioObject["spreadingFactor"] = nullptr;
        radioObject["codingRate"] = nullptr;
        radioObject["txPowerDbm"] = nullptr;
        radioObject["rssiDbm"] = nullptr;
        radioObject["snrDb"] = nullptr;
    }
    JsonObject duty = radioObject.createNestedObject("duty");
    duty["countedBurstUs"] = rtcGatewayAirtime.countedBurstUs;
    duty["cooldownMs"] = (int32_t)(gatewayNextTxMs - millis()) > 0 ? gatewayNextTxMs - millis() : 0;
    duty["totalBurstCount"] = rtcGatewayAirtime.totalBurstCount;
    duty["nonCompliantBurstCount"] = rtcGatewayAirtime.nonCompliantBurstCount;
    duty["rulePercent"] = 1.0f;
    const float nonCompliantBurstRatePercent = rtcGatewayAirtime.totalBurstCount == 0 ? 0.0f :
        (static_cast<float>(rtcGatewayAirtime.nonCompliantBurstCount) * 100.0f / rtcGatewayAirtime.totalBurstCount);
    duty["nonCompliantBurstRatePercent"] = nonCompliantBurstRatePercent;
    duty["driftFromOnePercent"] = rtcGatewayAirtime.totalBurstCount == 0 ? 0.0f : nonCompliantBurstRatePercent - 1.0f;
    if (gatewayRadioAvailable) {
        duty["minimumSleepOperationSeconds"] = sensorMinimumTotalIntervalSeconds(false);
        duty["minimumSleepDevelopmentSeconds"] = sensorMinimumTotalIntervalSeconds(true);
    } else {
        duty["minimumSleepOperationSeconds"] = nullptr;
        duty["minimumSleepDevelopmentSeconds"] = nullptr;
    }
}

WiFiClientSecure espClient; // Use secure client for TLS
PubSubClient mqttClient(espClient);

static bool gatewayRelayFeedback() {
    gatewayRelay.feedbackClosed = digitalRead(GATEWAY_RELAY_FEEDBACK_PIN) == HIGH;
    return gatewayRelay.feedbackClosed;
}

static bool applyGatewayRelay(bool closed) {
    gatewayRelay.failed = false;
    gatewayRelay.autoOff = false;
    const uint8_t expected = closed ? HIGH : LOW;
    gatewayRelayFeedback();
    if (digitalRead(GATEWAY_RELAY_FEEDBACK_PIN) == expected) return true;
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        GW_LOG_INFO("Relay trigger %c attempt %u", closed ? 'A' : 'B', (unsigned)(attempt + 1));
        digitalWrite(closed ? GATEWAY_RELAY_SET_PIN : GATEWAY_RELAY_RESET_PIN, HIGH);
        delay(10);
        digitalWrite(GATEWAY_RELAY_SET_PIN, LOW);
        digitalWrite(GATEWAY_RELAY_RESET_PIN, LOW);
        gatewayRelay.lastTrigger = closed ? 1 : 2;
        const uint32_t started = millis();
        while (millis() - started < 100) {
            if (digitalRead(GATEWAY_RELAY_FEEDBACK_PIN) == expected) {
                gatewayRelay.feedbackClosed = closed;
                gatewayRelay.requestedClosed = closed;
                GW_LOG_INFO("Relay feedback reached target=%u after attempt %u", (unsigned)closed, (unsigned)(attempt + 1));
                return true;
            }
            delay(5);
        }
    }
    gatewayRelayFeedback();
    gatewayRelay.failed = true;
    return false;
}

static void serviceGatewayRelay() {
    gatewayRelayFeedback();
    const time_t relayNow = time(nullptr);
    if (gatewayRelay.requestedClosed && gatewayRelay.deadline > relayNow &&
        (lastRelayCountdownLog == 0 || relayNow - lastRelayCountdownLog >= 10)) {
        GW_LOG_INFO("Gateway relay ON countdown: remainingSeconds=%lu.",
                    (unsigned long)(gatewayRelay.deadline - relayNow));
        lastRelayCountdownLog = relayNow;
    }
    if (gatewayRelay.requestedClosed && gatewayRelay.deadline != 0 && relayNow >= gatewayRelay.deadline) {
        if (applyGatewayRelay(false)) {
            gatewayRelay.requestedClosed = false;
            gatewayRelay.deadline = 0;
            if (lastRelayCountdownLog != 0) {
                GW_LOG_INFO("Gateway relay ON countdown: remainingSeconds=0 (auto-off).");
                lastRelayCountdownLog = 0;
            }
            gatewayRelay.autoOff = true;
            gatewayRelay.durable = saveGatewayConfig();
            if (!gatewayRelay.durable) {
                gatewayRelay.recoveryRequired = true;
                GW_LOG_ERROR("Gateway relay auto-off succeeded, but the OFF state could not be persisted.");
            }
            gatewayRelayStatusPending = true;
        } else {
            gatewayRelay.deadline = relayNow + gatewayRelay.maxOnSeconds;
            gatewayRelay.durable = saveGatewayConfig();
            if (!gatewayRelay.durable) gatewayRelay.recoveryRequired = true;
            gatewayRelayStatusPending = true;
            GW_LOG_ERROR("Relay automatic OFF failed; countdown restarted for %lu seconds.",
                         (unsigned long)gatewayRelay.maxOnSeconds);
        }
    }
}

// Check if clock is set != 1970 or CUSTOM_EPOCH + boot
bool isClockValid(time_t timeNow) {
    return isClockValidSince(timeNow, (unsigned long)uptime::getSeconds());
}

void configureSystemTime() {
    configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov", "time.google.com");
}

bool loadGatewayConfig(bool sdAvailable, bool* created) {
    if (created) *created = false;
    gatewayConfigFromNvs = false;
    File file;
    bool configIsEmpty = false;
    bool configFromNvs = false;
    DynamicJsonDocument doc(4096);
    if (sdAvailable) {
        file = SD.open("/config.json", FILE_READ);
        if (file) {
            configIsEmpty = file.size() == 0;
            if (!configIsEmpty && (deserializeJson(doc, file) || !doc.is<JsonObject>())) doc.clear();
            file.close();
        } else {
            configIsEmpty = true;
        }
        if (configIsEmpty && SD.exists("/config.bak")) {
            file = SD.open("/config.bak", FILE_READ);
            if (file) {
                doc.clear();
                const bool validBackup = !deserializeJson(doc, file) && doc.is<JsonObject>();
                file.close();
                if (validBackup && (!SD.exists("/config.json") || SD.remove("/config.json")) &&
                    SD.rename("/config.bak", "/config.json")) {
                    configIsEmpty = false;
                    GW_LOG_WARN("Recovered interrupted Gateway SD configuration replacement.");
                } else {
                    doc.clear();
                }
            }
        }
    }
    if (!doc.is<JsonObject>()) {
        Preferences backup;
        if (backup.begin("gateway-backup", true)) {
            String saved = backup.getString("config", "");
            backup.end();
            if (saved.length() && !deserializeJson(doc, saved) && doc.is<JsonObject>()) {
                configFromNvs = true;
                GW_LOG_WARN("Using Gateway configuration backup from NVS; SD configuration unavailable or invalid.");
            } else {
                doc.clear();
            }
        }
    }
    if (!sdAvailable && !configFromNvs) return false;

    if (configIsEmpty && !configFromNvs) {
        gatewayRelay.recoveryRequired = true;
        DynamicJsonDocument defaults(4096);
        defaults["wifi"]["ssid"] = gatewayConfig.wifiSsid;
        defaults["wifi"]["password"] = gatewayConfig.wifiPassword;
        defaults["mqtt"]["host"] = gatewayConfig.mqttHost;
        defaults["mqtt"]["port"] = gatewayConfig.mqttPort;
        defaults["mqtt"]["user"] = gatewayConfig.mqttUser;
        defaults["mqtt"]["password"] = gatewayConfig.mqttPassword;
        defaults["sensor"]["defaultSleepSeconds"] = gatewayConfig.sensorSleepSeconds;
        defaults["sensor"]["defaultDevMode"] = gatewayConfig.sensorDevMode;
        defaults["sensor"]["defaultTxPower"] = gatewayConfig.sensorTxPower;
        defaults["sensor"]["defaultCpuFrequencyMhz"] = gatewayConfig.sensorCpuFrequencyMhz;
        defaults["sensor"]["pairedMac"] = gatewayConfig.pairedSensorMac;
        defaults["gateway"]["radioTxPower"] = gatewayConfig.radioTxPower;
        for (const char* name : {"active", "requested"}) {
            defaults["radioProfile"][name]["spreadingFactor"] = RADIO_PROFILE_DEFAULT.spreadingFactor;
            defaults["radioProfile"][name]["bandwidthKhz"] = RADIO_PROFILE_BANDWIDTHS_KHZ[RADIO_PROFILE_DEFAULT.bandwidthIndex];
            defaults["radioProfile"][name]["codingRate"] = "4/5";
        }
        defaults["radioProfile"]["_help"] = "Fixed 868.300 MHz center; occupied edges must remain in 868.0-868.6 MHz. SF 7-12; bandwidth kHz: 7.8, 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125, 250, 500; coding rate: 4/5, 4/6, 4/7, 4/8. Match Sensor and Gateway; radio and sleep limits apply.";
        defaults["radioProfile"]["pending"] = false;
        defaults["radioProfile"]["phase"] = 0;
        defaults["radioProfile"]["status"] = "applied";
        defaults["relay"]["requestedClosed"] = false;
        defaults["relay"]["maxOnSeconds"] = gatewayRelay.maxOnSeconds;
        defaults["control"]["commandSecret"] = gatewayConfig.controlCommandSecret;
        defaults["webui"]["adminPassword"] = gatewayConfig.webAdminPassword;
        file = SD.open("/config.json", FILE_WRITE);
        if (file) {
            serializeJsonPretty(defaults, file);
            file.close();
            if (created) *created = true;
        }
        return false;
    }

    if (!doc.is<JsonObject>()) return false;
    gatewayConfigFromNvs = configFromNvs;

    bool configUpdated = false;
    JsonObject wifi = doc["wifi"].as<JsonObject>();
    if (wifi.isNull()) {
        doc.remove("wifi");
        wifi = doc.createNestedObject("wifi");
        configUpdated = true;
    }
    if (!wifi.containsKey("ssid")) { wifi["ssid"] = gatewayConfig.wifiSsid; configUpdated = true; }
    if (!wifi.containsKey("password")) { wifi["password"] = gatewayConfig.wifiPassword; configUpdated = true; }

    JsonObject mqtt = doc["mqtt"].as<JsonObject>();
    if (mqtt.isNull()) {
        doc.remove("mqtt");
        mqtt = doc.createNestedObject("mqtt");
        configUpdated = true;
    }
    if (!mqtt.containsKey("host")) { mqtt["host"] = gatewayConfig.mqttHost; configUpdated = true; }
    if (!mqtt.containsKey("port")) { mqtt["port"] = gatewayConfig.mqttPort; configUpdated = true; }
    if (!mqtt.containsKey("user")) { mqtt["user"] = gatewayConfig.mqttUser; configUpdated = true; }
    if (!mqtt.containsKey("password")) { mqtt["password"] = gatewayConfig.mqttPassword; configUpdated = true; }

    JsonObject sensor = doc["sensor"].as<JsonObject>();
    if (sensor.isNull()) {
        doc.remove("sensor");
        sensor = doc.createNestedObject("sensor");
        configUpdated = true;
    }
    if (!sensor.containsKey("defaultSleepSeconds")) { sensor["defaultSleepSeconds"] = gatewayConfig.sensorSleepSeconds; configUpdated = true; }
    if (!sensor.containsKey("defaultDevMode")) { sensor["defaultDevMode"] = gatewayConfig.sensorDevMode; configUpdated = true; }
    if (!sensor.containsKey("defaultTxPower")) { sensor["defaultTxPower"] = gatewayConfig.sensorTxPower; configUpdated = true; }
    if (!sensor.containsKey("defaultCpuFrequencyMhz")) { sensor["defaultCpuFrequencyMhz"] = gatewayConfig.sensorCpuFrequencyMhz; configUpdated = true; }
    if (!sensor.containsKey("pairedMac")) { sensor["pairedMac"] = gatewayConfig.pairedSensorMac; configUpdated = true; }

    JsonObject gateway = doc["gateway"].as<JsonObject>();
    if (gateway.isNull()) {
        doc.remove("gateway");
        gateway = doc.createNestedObject("gateway");
        configUpdated = true;
    }
    if (!gateway.containsKey("radioTxPower") || !gateway["radioTxPower"].is<int>() ||
        gateway["radioTxPower"].as<int>() < GATEWAY_SENSOR_TX_POWER_MIN_DBM ||
        gateway["radioTxPower"].as<int>() > GATEWAY_SENSOR_TX_POWER_MAX_DBM ||
        !isSupportedSensorTxPower(gateway["radioTxPower"].as<int>())) {
        gateway["radioTxPower"] = gatewayConfig.radioTxPower;
        configUpdated = true;
    }

    JsonObject radioProfile = doc["radioProfile"].as<JsonObject>();
    if (radioProfile.isNull()) {
        doc.remove("radioProfile");
        radioProfile = doc.createNestedObject("radioProfile");
        configUpdated = true;
    }
    const char* profileNames[] = {"active", "requested"};
    const char* oldSf[] = {"activeSf", "requestedSf"};
    const char* oldBw[] = {"activeBw", "requestedBw"};
    const char* oldCr[] = {"activeCr", "requestedCr"};
    RadioProfile parsedProfiles[] = {RADIO_PROFILE_DEFAULT, RADIO_PROFILE_DEFAULT};
    bool validProfiles[] = {false, false};
    for (size_t n = 0; n < 2; ++n) {
        JsonObject profile = radioProfile[profileNames[n]].as<JsonObject>();
        int sf = -1, bw = -1, cr = -1;
        if (!profile.isNull()) {
            sf = profile["spreadingFactor"] | -1;
            const float bandwidth = profile["bandwidthKhz"] | -1.0f;
            for (size_t i = 0; i < RADIO_PROFILE_BANDWIDTH_COUNT; ++i) {
                if (fabsf(bandwidth - RADIO_PROFILE_BANDWIDTHS_KHZ[i]) < 0.01f) {
                    bw = static_cast<int>(i);
                    break;
                }
            }
            const char* coding = profile["codingRate"] | "";
            if (strlen(coding) == 3 && coding[0] == '4' && coding[1] == '/' && coding[2] >= '5' && coding[2] <= '8') cr = coding[2] - '0';
        } else if (radioProfile[oldSf[n]].is<int>() && radioProfile[oldBw[n]].is<int>() && radioProfile[oldCr[n]].is<int>()) {
            sf = radioProfile[oldSf[n]].as<int>();
            bw = radioProfile[oldBw[n]].as<int>();
            cr = radioProfile[oldCr[n]].as<int>();
        }
        validProfiles[n] = sf >= 7 && sf <= 12 && bw >= 0 && bw < RADIO_PROFILE_BANDWIDTH_COUNT && cr >= 5 && cr <= 8 &&
            radioProfileBandwidthFitsEu868SubBand(static_cast<size_t>(bw));
        if (validProfiles[n]) parsedProfiles[n] = {static_cast<uint8_t>(sf), static_cast<uint8_t>(bw), static_cast<uint8_t>(cr)};
        else if (n == 1 && validProfiles[0]) parsedProfiles[n] = parsedProfiles[0];
        if (profile.isNull() || !validProfiles[n]) {
            radioProfile.remove(profileNames[n]);
            profile = radioProfile.createNestedObject(profileNames[n]);
            profile["spreadingFactor"] = parsedProfiles[n].spreadingFactor;
            profile["bandwidthKhz"] = RADIO_PROFILE_BANDWIDTHS_KHZ[parsedProfiles[n].bandwidthIndex];
            char coding[4] = {'4', '/', static_cast<char>('0' + parsedProfiles[n].codingRate), '\0'};
            profile["codingRate"] = String(coding);
            configUpdated = true;
        }
        if (radioProfile.containsKey(oldSf[n]) || radioProfile.containsKey(oldBw[n]) || radioProfile.containsKey(oldCr[n])) {
            radioProfile.remove(oldSf[n]);
            radioProfile.remove(oldBw[n]);
            radioProfile.remove(oldCr[n]);
            configUpdated = true;
        }
    }
    if (!radioProfile.containsKey("_help")) {
        radioProfile["_help"] = "Fixed 868.300 MHz center; occupied edges must remain in 868.0-868.6 MHz. SF 7-12; bandwidth kHz: 7.8, 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125, 250, 500; coding rate: 4/5, 4/6, 4/7, 4/8. Match Sensor and Gateway; radio and sleep limits apply.";
        configUpdated = true;
    }
    if (!validProfiles[0] || !validProfiles[1]) {
        radioProfile["pending"] = false;
        radioProfile["awaitingAck"] = false;
        radioProfile["phase"] = 0;
        radioProfile["status"] = "applied";
        GW_LOG_WARN("Stored Gateway radio profile is invalid or missing; using build defaults.");
    }
    gatewayRadioProfile.active = parsedProfiles[0];
    gatewayRadioProfile.requested = parsedProfiles[1];
    gatewayRadioProfile.pending = radioProfile["pending"] | false;
    gatewayRadioProfile.phase = radioProfile["phase"] | 0;
    const char* radioStatus = radioProfile["status"] | "applied";
    snprintf(gatewayRadioProfile.status, sizeof(gatewayRadioProfile.status), "%s", radioStatus);
    snprintf(gatewayRadioProfile.commandId, sizeof(gatewayRadioProfile.commandId), "%s", radioProfile["commandId"] | "");
    JsonArrayConst requestBytes = radioProfile["request"].as<JsonArrayConst>();
    if (requestBytes.size() == sizeof(gatewayRadioProfile.request)) {
        uint8_t* rawRequest = reinterpret_cast<uint8_t*>(&gatewayRadioProfile.request);
        size_t index = 0;
        for (JsonVariantConst byteValue : requestBytes) rawRequest[index++] = byteValue.as<uint8_t>();
    }
    gatewayRadioProfile.awaitingAck = radioProfile["awaitingAck"] | false;
    if (gatewayRadioProfile.awaitingAck) {
        gatewayRadioProfile.awaitingAck = false;
        snprintf(gatewayRadioProfile.status, sizeof(gatewayRadioProfile.status), "%s", "unconfirmed");
    }

    JsonObject control = doc["control"].as<JsonObject>();
    if (control.isNull()) {
        doc.remove("control");
        control = doc.createNestedObject("control");
        configUpdated = true;
    }
    if (!control.containsKey("commandSecret")) { control["commandSecret"] = gatewayConfig.controlCommandSecret; configUpdated = true; }

    JsonObject webui = doc["webui"].as<JsonObject>();
    if (webui.isNull()) {
        doc.remove("webui");
        webui = doc.createNestedObject("webui");
        configUpdated = true;
    }
    if (!webui.containsKey("adminPassword")) { webui["adminPassword"] = gatewayConfig.webAdminPassword; configUpdated = true; }

    if (configUpdated && !configFromNvs) {
        const char* temporaryPath = "/config.tmp";
        const char* backupPath = "/config.bak";
        if (SD.exists(temporaryPath)) SD.remove(temporaryPath);
        file = SD.open(temporaryPath, FILE_WRITE);
        if (!file || serializeJsonPretty(doc, file) == 0) {
            if (file) file.close();
            SD.remove(temporaryPath);
            GW_LOG_ERROR("Unable to write normalized Gateway configuration.");
            return false;
        }
        file.flush();
        file.close();

        if (SD.exists(backupPath)) SD.remove(backupPath);
        if (SD.exists("/config.json") && !SD.rename("/config.json", backupPath)) {
            SD.remove(temporaryPath);
            GW_LOG_ERROR("Unable to back up Gateway configuration before normalization.");
            return false;
        }
        if (!SD.rename(temporaryPath, "/config.json")) {
            if (SD.exists(backupPath)) SD.rename(backupPath, "/config.json");
            GW_LOG_ERROR("Unable to install normalized Gateway configuration.");
            return false;
        }
        if (SD.exists(backupPath)) SD.remove(backupPath);
        GW_LOG_INFO("Added missing or repaired configuration values in /config.json.");
    }

    const char* value = wifi["ssid"].as<const char*>();
    if (value && strlen(value) <= 32) gatewayConfig.wifiSsid = value;
    else GW_LOG_WARN("Ignoring invalid SD WiFi SSID.");
    value = wifi["password"].as<const char*>();
    if (value && strlen(value) <= 64) gatewayConfig.wifiPassword = value;
    else GW_LOG_WARN("Ignoring invalid SD WiFi password.");

    value = mqtt["host"].as<const char*>();
    if (value && strlen(value) <= 128) gatewayConfig.mqttHost = value;
    else GW_LOG_WARN("Ignoring invalid SD MQTT host.");
    if (mqtt["port"].is<int>() && mqtt["port"].as<int>() >= 1 && mqtt["port"].as<int>() <= UINT16_MAX)
        gatewayConfig.mqttPort = mqtt["port"].as<int>();
    else GW_LOG_WARN("Ignoring invalid SD MQTT port.");
    value = mqtt["user"].as<const char*>();
    if (value && strlen(value) <= 64) gatewayConfig.mqttUser = value;
    else GW_LOG_WARN("Ignoring invalid SD MQTT user.");
    value = mqtt["password"].as<const char*>();
    if (value && strlen(value) <= 128) gatewayConfig.mqttPassword = value;
    else GW_LOG_WARN("Ignoring invalid SD MQTT password.");

    const char* pairedMac = sensor["pairedMac"] | "";
    if (isValidSensorMac(pairedMac)) gatewayConfig.pairedSensorMac = pairedMac;
    else if (!pairedMac[0]) gatewayConfig.pairedSensorMac = "";
    int32_t sleepSeconds = sensor["defaultSleepSeconds"] | 0;
    if (sensor["defaultSleepSeconds"].is<int>() && sleepSeconds >= 10 && sleepSeconds <= UINT8_MAX) gatewayConfig.sensorSleepSeconds = (uint8_t)sleepSeconds;
    if (sensor["defaultDevMode"].is<bool>()) gatewayConfig.sensorDevMode = sensor["defaultDevMode"].as<bool>();
    if (sensor["defaultTxPower"].is<int>() && sensor["defaultTxPower"].as<int>() >= GATEWAY_SENSOR_TX_POWER_MIN_DBM && sensor["defaultTxPower"].as<int>() <= GATEWAY_SENSOR_TX_POWER_MAX_DBM && isSupportedSensorTxPower(sensor["defaultTxPower"].as<int>())) {
        gatewayConfig.sensorTxPower = sensor["defaultTxPower"].as<int>();
    } else {
        GW_LOG_WARN("Ignoring invalid SD Sensor default TX power.");
    }
    gatewayConfig.radioTxPower = gateway["radioTxPower"].as<int>();
    int cpuFrequency = sensor["defaultCpuFrequencyMhz"] | gatewayConfig.sensorCpuFrequencyMhz;
    if (sensor["defaultCpuFrequencyMhz"].is<int>() && (cpuFrequency == 80 || cpuFrequency == 160 || cpuFrequency == 240)) gatewayConfig.sensorCpuFrequencyMhz = (uint8_t)cpuFrequency;

    JsonObject relay = doc["relay"].as<JsonObject>();
    if (!relay.isNull()) {
        const bool hasRequestedState = relay["requestedClosed"].is<bool>();
        if (hasRequestedState) gatewayRelay.requestedClosed = relay["requestedClosed"].as<bool>();
        int maxOn = relay["maxOnSeconds"] | 0;
        const bool hasValidMaxOn = relay["maxOnSeconds"].is<int>() && maxOn >= 1 && maxOn <= 7200;
        if (hasValidMaxOn) gatewayRelay.maxOnSeconds = maxOn;
        uint32_t deadline = relay["deadline"] | 0;
        gatewayRelay.deadline = gatewayRelay.requestedClosed ? deadline : 0;
        gatewayRelay.durable = true;
        if (!hasRequestedState || !hasValidMaxOn || (gatewayRelay.requestedClosed && deadline == 0)) {
            gatewayRelay.recoveryRequired = true;
        }
    } else {
        gatewayRelay.recoveryRequired = true;
    }

    value = control["commandSecret"].as<const char*>();
    if (value && value[0]) gatewayConfig.controlCommandSecret = value;
    value = webui["adminPassword"].as<const char*>();
    if (value && strlen(value) >= 12 && strlen(value) <= 64) gatewayConfig.webAdminPassword = value;
    else GW_LOG_WARN("Ignoring invalid SD WebUI administrator password.");

    JsonObject overrides = doc["sensorOverrides"].as<JsonObject>();
    if (gatewayConfig.pairedSensorMac.length() == 0 && !overrides.isNull()) {
        uint8_t legacyOverrideCount = 0;
        String legacyMac;
        for (JsonPair entry : overrides) {
            const char* candidateMac = entry.key().c_str();
            if (!isValidSensorMac(candidateMac)) continue;
            ++legacyOverrideCount;
            legacyMac = candidateMac;
        }
        if (legacyOverrideCount == 1) {
            gatewayConfig.pairedSensorMac = legacyMac;
            configUpdated = true;
            GW_LOG_INFO("Migrated the only persisted Sensor identity to the paired remote Sensor.");
        } else if (legacyOverrideCount > 1) {
            GW_LOG_WARN("Multiple persisted Sensor identities found; explicit pairing is required.");
        }
    }
    if (sensorPresence[0].mac[0] &&
        strcasecmp(sensorPresence[0].mac, gatewayConfig.pairedSensorMac.c_str()) != 0) {
        memset(&sensorPresence[0], 0, sizeof(sensorPresence[0]));
    }
    for (SensorPresence& presence : sensorPresence) {
        presence.hasSleepOverride = false;
        presence.hasDevModeOverride = false;
        presence.hasTxPowerOverride = false;
        presence.hasCpuFrequencyOverride = false;
        presence.configurationPending = false;
        presence.pendingCommandId[0] = '\0';
        presence.configurationOrigin[0] = '\0';
    }
    if (!overrides.isNull()) {
        for (JsonPair entry : overrides) {
            const char* savedMac = entry.key().c_str();
            if (!isValidSensorMac(savedMac)) continue;
            SensorPresence* retained = findSensorPresence(savedMac);
            if (!retained) continue;
            snprintf(retained->mac, sizeof(retained->mac), "%s", savedMac);
            JsonObject saved = entry.value().as<JsonObject>();
            int sleep = saved["sleepSeconds"] | 0;
            if (saved["sleepSeconds"].is<int>() && sleep >= 10 && sleep <= UINT8_MAX) { retained->hasSleepOverride = true; retained->sleepOverrideSeconds = sleep; }
            if (saved["developmentMode"].is<bool>()) { retained->hasDevModeOverride = true; retained->devModeOverride = saved["developmentMode"]; }
            if (saved.containsKey("txPower")) {
                if (saved["txPower"].is<int>() && saved["txPower"].as<int>() >= GATEWAY_SENSOR_TX_POWER_MIN_DBM && saved["txPower"].as<int>() <= GATEWAY_SENSOR_TX_POWER_MAX_DBM && isSupportedSensorTxPower(saved["txPower"].as<int>())) {
                    retained->hasTxPowerOverride = true;
                    retained->txPowerOverride = saved["txPower"].as<int>();
                } else {
                    GW_LOG_WARN("Ignoring invalid SD Sensor TX power override.");
                }
            }
            int cpu = saved["cpuFrequencyMhz"] | 0;
            if (saved["cpuFrequencyMhz"].is<int>() && (cpu == 80 || cpu == 160 || cpu == 240)) { retained->hasCpuFrequencyOverride = true; retained->cpuFrequencyOverride = cpu; }
            const bool hasOverride = retained->hasSleepOverride || retained->hasDevModeOverride ||
                                     retained->hasTxPowerOverride || retained->hasCpuFrequencyOverride;
            retained->configurationPending = saved["pending"].is<bool>()
                                                 ? saved["pending"].as<bool>()
                                                 : hasOverride;
            retained->pendingCpuChange = saved["pendingCpuChange"].is<bool>()
                                             ? saved["pendingCpuChange"].as<bool>()
                                             : (retained->configurationPending && retained->hasCpuFrequencyOverride);
            retained->pendingModeChange = saved["pendingModeChange"] | false;
            const char* origin = saved["origin"] | "";
            if (strcmp(origin, "WebUI") == 0) origin = "UI";
            if (strcmp(origin, "UI") == 0 || strcmp(origin, "MQTT") == 0 ||
                strcmp(origin, "SD") == 0 || strcmp(origin, "Defaults") == 0) {
                snprintf(retained->configurationOrigin, sizeof(retained->configurationOrigin), "%s", origin);
            } else if (retained->hasSleepOverride || retained->hasDevModeOverride || retained->hasTxPowerOverride ||
                       retained->hasCpuFrequencyOverride) {
                snprintf(retained->configurationOrigin, sizeof(retained->configurationOrigin), "%s", "SD");
            }
            const char* pendingCommandId = saved["pendingCommandId"] | "";
            if (isValidCommandId(pendingCommandId)) {
                snprintf(retained->pendingCommandId, sizeof(retained->pendingCommandId), "%s", pendingCommandId);
            }
            retained->authenticatedProtocol = saved["authenticatedProtocol"] | false;
            retained->lastAuthenticatedMsgCount = saved["lastAuthenticatedMsgCount"] | 0;
            if (saved.containsKey("lastAuthenticatedBootNonce")) {
                retained->lastBootNonce = saved["lastAuthenticatedBootNonce"] | retained->lastBootNonce;
                retained->bootNonceKnown = true;
            }
        }
    }

    memset(processedCommandIds, 0, sizeof(processedCommandIds));
    memset(processedCommandExpiry, 0, sizeof(processedCommandExpiry));
    nextProcessedCommandSlot = 0;
    JsonArrayConst savedProcessedCommands = doc["processedCommandIds"].as<JsonArrayConst>();
    if (!savedProcessedCommands.isNull()) {
        for (JsonVariantConst savedCommand : savedProcessedCommands) {
            const char* value = savedCommand.is<const char*>()
                                    ? savedCommand.as<const char*>()
                                    : savedCommand["id"].as<const char*>();
            if (!isValidCommandId(value)) continue;
            snprintf(processedCommandIds[nextProcessedCommandSlot],
                     sizeof(processedCommandIds[nextProcessedCommandSlot]), "%s", value);
            processedCommandExpiry[nextProcessedCommandSlot] = savedCommand.is<JsonObjectConst>()
                                                                  ? savedCommand["expiresAt"].as<time_t>()
                                                                  : 0;
            nextProcessedCommandSlot = (nextProcessedCommandSlot + 1) % MAX_PROCESSED_COMMANDS;
        }
    }

    if (!configFromNvs) {
        DynamicJsonDocument backupDoc(3072);
        backupDoc["wifi"] = doc["wifi"];
        backupDoc["mqtt"] = doc["mqtt"];
        backupDoc["sensor"] = doc["sensor"];
        backupDoc["gateway"] = doc["gateway"];
        backupDoc["relay"] = doc["relay"];
        backupDoc["radioProfile"] = doc["radioProfile"];
        backupDoc["control"]["commandSecret"] = doc["control"]["commandSecret"];
        backupDoc["webui"]["adminPassword"] = doc["webui"]["adminPassword"];
        String backupJson;
        Preferences backup;
        if (backupDoc.overflowed() || !serializeJson(backupDoc, backupJson) || !backup.begin("gateway-backup", false)) {
            GW_LOG_ERROR("Unable to prepare Gateway NVS configuration backup.");
        } else {
            if (backup.getString("config", "") != backupJson && backup.putString("config", backupJson) != backupJson.length())
                GW_LOG_ERROR("Unable to save Gateway NVS configuration backup.");
            backup.end();
        }
    }

    wifi["password"] = "***";
    mqtt["password"] = "***";
    control["commandSecret"] = "***";
    webui["adminPassword"] = "***";
    String configTrace;
    serializeJson(doc, configTrace);
    GW_LOG_DEBUG("Parsed SD configuration: %s", configTrace.c_str());
    return true;
}

bool configureMqttTls(bool sdAvailable) {
    String candidate;
    if (sdAvailable) {
        File caFile = SD.open("/mqtt-ca.pem", FILE_READ);
        if (caFile && caFile.size() > 0 && caFile.size() <= 8192) candidate = caFile.readString();
        if (caFile) caFile.close();
    }
    const bool validSdCa = candidate.indexOf("-----BEGIN CERTIFICATE-----") >= 0 &&
                           candidate.indexOf("-----END CERTIFICATE-----") >= 0;
    Preferences backup;
    if (validSdCa) {
        if (backup.begin("gateway-backup", false)) {
            const size_t savedLength = backup.getBytesLength("mqtt-ca");
            char* saved = savedLength == candidate.length() ? static_cast<char*>(malloc(savedLength)) : nullptr;
            const bool unchanged = saved && backup.getBytes("mqtt-ca", saved, savedLength) == savedLength &&
                                   memcmp(saved, candidate.c_str(), savedLength) == 0;
            free(saved);
            if (!unchanged && backup.putBytes("mqtt-ca", candidate.c_str(), candidate.length()) != candidate.length())
                GW_LOG_ERROR("Unable to back up MQTT CA certificate to NVS.");
            backup.end();
        } else {
            GW_LOG_ERROR("Unable to open MQTT CA certificate backup.");
        }
    } else {
        candidate = "";
        if (backup.begin("gateway-backup", true)) {
            const size_t length = backup.getBytesLength("mqtt-ca");
            if (length > 0 && length <= 8192) {
                char* saved = static_cast<char*>(malloc(length + 1));
                if (saved) {
                    if (backup.getBytes("mqtt-ca", saved, length) == length) {
                        saved[length] = '\0';
                        candidate = saved;
                    }
                    free(saved);
                }
            }
            backup.end();
        }
        if (candidate.indexOf("-----BEGIN CERTIFICATE-----") < 0 || candidate.indexOf("-----END CERTIFICATE-----") < 0) {
            GW_LOG_ERROR("No valid MQTT CA certificate on SD or in NVS; MQTT TLS cannot connect.");
            return false;
        }
        GW_LOG_WARN("Using MQTT CA certificate backup from NVS.");
    }
    mqttRootCa = candidate;
    espClient.setCACert(mqttRootCa.c_str());
    return true;
}

void maintainSdCard() {
    if (gatewayLogSink.isSdReady() || millis() - lastSdRetryMs < SD_RETRY_INTERVAL_MS) return;
    lastSdRetryMs = millis();
    if (sensorPresence[0].configurationPending) return;
    if (tryMountSdCard()) {
        const GatewayRadioProfileState previousRadio = gatewayRadioProfile;
        const SensorPresence previousSensor = sensorPresence[0];
        const String previousAdminPassword = gatewayConfig.webAdminPassword;
        gatewayLogSink.begin(true);
        bool configCreated = false;
        if (loadGatewayConfig(true, &configCreated)) {
            bool dutyConfigUpdated = false;
            const bool radioChanged = memcmp(&previousRadio.active, &gatewayRadioProfile.active, sizeof(RadioProfile)) != 0;
            const bool sensorConnected = previousSensor.online && previousSensor.mac[0] &&
                strcasecmp(previousSensor.mac, gatewayConfig.pairedSensorMac.c_str()) == 0 &&
                previousSensor.sleepIntervalSeconds != 0 &&
                millis() - previousSensor.lastPacketMs < previousSensor.sleepIntervalSeconds * 3000UL + SENSOR_STATUS_GRACE_MS;
            if (radioChanged && sensorConnected) {
                gatewayRadioProfile = previousRadio;
                dutyConfigUpdated = true;
                GW_LOG_WARN("Preserving the connected Sensor radio profile over the SD profile.");
            } else if (radioChanged && !gatewayConfigFromNvs) {
                GW_LOG_WARN("SD radio profile changed while Sensor is offline; restarting Gateway to apply it.");
                ESP.restart();
                return;
            }
            const uint32_t defaultMinimum = sensorMinimumTotalIntervalSeconds(gatewayConfig.sensorDevMode);
            if (defaultMinimum <= UINT8_MAX && gatewayConfig.sensorSleepSeconds < defaultMinimum) {
                gatewayConfig.sensorSleepSeconds = static_cast<uint8_t>(defaultMinimum);
                dutyConfigUpdated = true;
                GW_LOG_WARN("Raised SD default Sensor sleep to %lus for duty cycle.", (unsigned long)defaultMinimum);
            }
            for (SensorPresence& retained : sensorPresence) {
                if (!retained.mac[0]) continue;
                const bool developmentMode = retained.hasDevModeOverride ? retained.devModeOverride : gatewayConfig.sensorDevMode;
                const uint32_t minimum = sensorMinimumTotalIntervalSeconds(developmentMode);
                const uint32_t sleep = retained.hasSleepOverride ? retained.sleepOverrideSeconds : gatewayConfig.sensorSleepSeconds;
                if (minimum <= UINT8_MAX && sleep < minimum) {
                    retained.hasSleepOverride = true;
                    retained.sleepOverrideSeconds = static_cast<uint8_t>(minimum);
                    dutyConfigUpdated = true;
                    GW_LOG_WARN("Raised SD Sensor sleep to %lus for duty cycle.", (unsigned long)minimum);
                }
            }
            if (dutyConfigUpdated && !saveGatewayConfig()) GW_LOG_ERROR("Failed to persist duty-cycle-adjusted Sensor configuration.");
            GW_LOG_INFO("Sensor pairing: %s; MAC=%s.", gatewayConfig.pairedSensorMac.length() ? "paired" : "unpaired",
                        gatewayConfig.pairedSensorMac.length() ? gatewayConfig.pairedSensorMac.c_str() : "none");
            GW_LOG_INFO("Loaded SD network configuration: WiFi SSID length %u, MQTT host length %u.",
                        gatewayConfig.wifiSsid.length(), gatewayConfig.mqttHost.length());
            bool verifierPersisted = false;
            if (gatewayWebUi.running() && previousAdminPassword != gatewayConfig.webAdminPassword &&
                !gatewayWebUi.applyAdminPassword(gatewayConfig.webAdminPassword.c_str(), verifierPersisted)) {
                GW_LOG_ERROR("Failed to derive the SD WebUI administrator password verifier.");
            } else if (gatewayWebUi.running() && previousAdminPassword != gatewayConfig.webAdminPassword && !verifierPersisted) {
                GW_LOG_WARN("WebUI verifier cache could not be saved; SD configuration remains authoritative.");
            }
            configureMqttTls(true);
            applyNetworkConfiguration();
            GW_LOG_INFO("SD card initialized and configuration applied.");
        } else if (configCreated) {
            GW_LOG_WARN("Created /config.json template; edit it to configure WiFi and MQTT.");
        } else {
            GW_LOG_WARN("SD card initialized but /config.json is unreadable or invalid.");
        }
    }
}

bool tryMountSdCard() {
#if defined(SD_SHARE_SPI_BUS)
    bool mounted = SD.begin(SDCARD_CS);
#else
    bool mounted = SD.begin(SDCARD_CS, SDCardSPI);
#endif
    if (mounted) deviceOnline |= SDCARD_ONLINE;
    return mounted;
}

void applyNetworkConfiguration() {
    mqttClient.disconnect();
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    if (gatewayConfig.wifiSsid.length() > 0) {
        WiFi.begin(gatewayConfig.wifiSsid.c_str(), gatewayConfig.wifiPassword.c_str());
        lastWifiReconnectAttempt = millis();
    } else {
        GW_LOG_WARN("WiFi is not configured; edit /config.json on the SD card.");
    }

    if (gatewayConfig.mqttHost.length() > 0) {
        mqttClient.setServer(gatewayConfig.mqttHost.c_str(), gatewayConfig.mqttPort);
    } else {
        GW_LOG_WARN("MQTT is not configured; edit /config.json on the SD card.");
    }
}

bool refreshRtcTrustFromSystemClock() {
    time_t currentTime;
    time(&currentTime);
    if (isClockValid(currentTime)) {
        hasValidRtcTime = true;
    }
    return hasValidRtcTime;
}

bool sendUpdateBeacon() {
    uint8_t beacon = CONFIG_UPDATE_PENDING_FLAG;
    xSemaphoreTake(radioSemaphore, 0); // clear any pending semaphore
    uint32_t txStartedUs = micros();
    int txState = radio.startTransmit(&beacon, 1);
    if (txState != RADIOLIB_ERR_NONE) {
        GW_LOG_ERROR("Update beacon startTransmit failed, code %d", txState);
        return false;
    }
    if (xSemaphoreTake(radioSemaphore, pdMS_TO_TICKS(1000)) != pdTRUE) {
        GW_LOG_WARN("Update beacon TX timeout!");
        gatewayBurstMeasuredUs += micros() - txStartedUs;
        return false;
    }
    uint32_t measuredUs = micros() - txStartedUs;
    gatewayBurstMeasuredUs += measuredUs;
    GW_LOG_INFO("Update beacon transmitted: calculated=%luus measured=%luus.",
                (unsigned long)rtcGatewayAirtime.calculatedBeaconUs, (unsigned long)measuredUs);
    return true;
}

uint32_t sensorMinimumTotalIntervalSeconds(bool developmentMode) {
    const uint32_t telemetryUs = radio.getTimeOnAir(
        developmentMode ? RADIO_PROFILE_TELEMETRY_FULL_SIZE : RADIO_PROFILE_OPERATION_SIZE);
    const uint32_t ackUs = radio.getTimeOnAir(sizeof(RadioProfileAck));
    return max(LoRaDutyCycle::minimumSleepSeconds(telemetryUs),
               LoRaDutyCycle::minimumSleepSeconds(ackUs));
}

bool TxCycleCheck() {
    if (!gatewayHasSentAfterSensorRx || (int32_t)(millis() - gatewayNextTxMs) >= 0) return true;
    GW_LOG_WARN("Gateway TX delayed by duty cycle; %lums cooldown remains.",
                (unsigned long)(gatewayNextTxMs - millis()));
    return false;
}

void finishGatewayTxBurst() {
    rtcGatewayAirtime.measuredBurstUs = gatewayBurstMeasuredUs;
    const uint32_t calculatedBurstUs = rtcGatewayAirtime.calculatedBeaconUs + rtcGatewayAirtime.calculatedConfigUs;
    rtcGatewayAirtime.countedBurstUs = max(calculatedBurstUs, gatewayBurstMeasuredUs);
    const uint32_t silenceMs = LoRaDutyCycle::requiredSilenceMs(rtcGatewayAirtime.countedBurstUs);
    gatewayNextTxMs = millis() + silenceMs;
    gatewayHasSentAfterSensorRx = true;
    ++rtcGatewayAirtime.totalBurstCount;
    if (gatewayBurstNonCompliant) ++rtcGatewayAirtime.nonCompliantBurstCount;
    GW_LOG_INFO("Gateway TX burst: calculated=%luus measured=%luus counted=%luus cooldown=%lums, nonCompliant=%u/%u.",
                (unsigned long)calculatedBurstUs, (unsigned long)gatewayBurstMeasuredUs,
                (unsigned long)rtcGatewayAirtime.countedBurstUs, (unsigned long)silenceMs,
                (unsigned)rtcGatewayAirtime.nonCompliantBurstCount,
                (unsigned)rtcGatewayAirtime.totalBurstCount);
    gatewayBurstNonCompliant = false;
    gatewayBurstMeasuredUs = 0;
}

const char* getWifiStatusString(wl_status_t status) {
  switch (status) {
    case 0:      return "IDLE_STATUS";
    case 1:      return "NO_SSID_AVAIL";
    case 2:      return "SCAN_COMPLETED";
    case 3:      return "CONNECTED";
    case 4:      return "CONNECT_FAILED";
    case 5:      return "CONNECTION_LOST";
    case 6:      return "DISCONNECTED";
    default:     return "UNKNOWN_STATUS";
  }
}

// this function is called when a complete packet
// is received by the module
// IMPORTANT: this function MUST be 'void' type
//            and MUST NOT have any arguments!
#if defined(ESP8266)
ICACHE_RAM_ATTR
#elif defined(ESP32)
IRAM_ATTR
#endif
void setFlag(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    lastRadioIrqTick = xTaskGetTickCountFromISR();
    if (radioSemaphore != NULL) {
        xSemaphoreGiveFromISR(radioSemaphore, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void setup()
{
    uptime::calculateUptime();

    // SD/VFS mount failures are reported by the Gateway logger below.
    esp_log_level_set("sd_diskio", ESP_LOG_NONE);
    esp_log_level_set("vfs_api", ESP_LOG_NONE);

    setupBoards(false, false, true);
    bool sdAvailable = (deviceOnline & SDCARD_ONLINE) != 0;
    bool loadedSdConfig = loadGatewayConfig(sdAvailable);
    if (!loadedSdConfig) gatewayRelay.recoveryRequired = true;
    pinMode(GATEWAY_RELAY_SET_PIN, OUTPUT);
    pinMode(GATEWAY_RELAY_RESET_PIN, OUTPUT);
    digitalWrite(GATEWAY_RELAY_SET_PIN, LOW);
    digitalWrite(GATEWAY_RELAY_RESET_PIN, LOW);
    pinMode(GATEWAY_RELAY_FEEDBACK_PIN, INPUT_PULLUP);
    gatewayRelayFeedback();
    if (gatewayRelay.recoveryRequired) {
        GW_LOG_ERROR("Relay persistence is unavailable; forcing Gateway relay OFF.");
        gatewayRelay.requestedClosed = false;
        applyGatewayRelay(false);
    } else if (!applyGatewayRelay(gatewayRelay.requestedClosed)) {
        GW_LOG_ERROR("Persisted Gateway relay state could not be verified.");
    }

    // FAT timestamps use the ESP32 system clock. Start at the project epoch
    // until NTP supplies real UTC, rather than creating pre-2024 log files.
    time_t bootClock;
    time(&bootClock);
    if (bootClock < (time_t)CUSTOM_EPOCH) {
        struct timeval fallbackTime;
        fallbackTime.tv_sec = (time_t)CUSTOM_EPOCH;
        fallbackTime.tv_usec = 0;
        settimeofday(&fallbackTime, nullptr);
    }

    // Elog queues each completed line for its sole UART/SD fan-out task.
    gatewayLogSink.begin(sdAvailable);
    if (gatewayRelay.failed && !gatewayRelay.requestedClosed && gatewayRelay.feedbackClosed) {
        gatewayRelay.requestedClosed = true;
        gatewayRelay.deadline = time(nullptr) + gatewayRelay.maxOnSeconds;
        gatewayRelay.durable = saveGatewayConfig();
        if (!gatewayRelay.durable) gatewayRelay.recoveryRequired = true;
        gatewayRelayStatusPending = true;
        GW_LOG_ERROR("Relay OFF failed at startup; countdown restarted for %lu seconds.",
                     (unsigned long)gatewayRelay.maxOnSeconds);
    }
    GW_LOG_INFO(gatewayConfigFromNvs ? "Loaded NVS configuration backup." :
                loadedSdConfig ? "Loaded SD configuration." : "Using compiled default configuration.");
    GW_LOG_INFO("Sensor pairing: %s; MAC=%s.", gatewayConfig.pairedSensorMac.length() ? "paired" : "unpaired",
                gatewayConfig.pairedSensorMac.length() ? gatewayConfig.pairedSensorMac.c_str() : "none");
    if (!sdAvailable) GW_LOG_ERROR("SD card does not exist or is unreadable.");
    if (!gatewayCredentialsProvisioned()) {
        if (LORAHOME_PRODUCTION_BUILD) {
            GW_LOG_ERROR("Production credentials are not provisioned; Gateway services remain disabled.");
            return;
        }
        GW_LOG_WARN("Development provisioning mode: placeholder credentials detected; secrets are not logged.");
    }
    // Initialize the FreeRTOS semaphore
    radioSemaphore = xSemaphoreCreateBinary();

    configureMqttTls(sdAvailable);

    // Initialize WiFi and NTP in the background (non-blocking)
    WiFi.setHostname(WIFI_HOSTNAME);
    applyNetworkConfiguration();

    mqttClient.setBufferSize(512);
    mqttClient.setCallback(mqttCallback);

    
    // Attempt to recover time from ESP32's hardware RTC (survives software resets)
    time_t sysTime;
    time(&sysTime);
    refreshRtcTrustFromSystemClock();  // Check and set hasValidRtcTime based on current RTC time
    
    if (hasValidRtcTime) {
        GW_LOG_INFO("Recovered time from internal ESP32 RTC after soft reset.");
    } else {
        configureSystemTime(); // Ensure SNTP is configured if RTC time is not valid
        time(&sysTime);
        GW_LOG_INFO("RTC not valid, syncing time from NTP server.");
        
        // Check if NTP sync succeeded
        if (!isClockValid(sysTime)) {
            GW_LOG_WARN("NTP sync failed. setup will proceed with invalid time.");
        }
    }
    

    // When the power is turned on, a delay is required.
    vTaskDelay(pdMS_TO_TICKS(1500));

    gatewayLogSink.setLineObserver([](const char* line) { gatewayWebUi.appendLogLine(line); });
    gatewayWebUi.setActionLogger([](const char* operation, const char* outcome) {
        if (strcmp(outcome, "queued") == 0) {
            GW_LOG_INFO("WebUI queued action: %s.", operation);
        } else {
            GW_LOG_INFO("WebUI action: %s (%s).", operation, outcome);
        }
    });
    gatewayWebUi.setCommandHandler(handleWebUiCommand);
    gatewayWebUi.setSnapshotProvider(provideWebUiSnapshots);
    if (gatewayWebUi.begin(gatewayConfig.webAdminPassword.c_str())) {
        const char* bootstrap = gatewayWebUi.bootstrapCredential();
        if (bootstrap && bootstrap[0]) {
            // Deliberately bypass the persistent logger: the first-boot secret is UART-only.
            Serial.printf("\n[WEBUI] First-login user: admin\n[WEBUI] First-login password: %s\n", bootstrap);
        }
    } else {
        GW_LOG_ERROR("Gateway WebUI HTTP service failed to initialize.");
    }

    if (loadedSdConfig && gatewayRadioProfile.pending && gatewayRadioProfile.phase == 2 &&
        !gatewayRadioProfile.awaitingAck &&
        gatewayRadioProfile.request.version == RADIO_PROFILE_FRAME_VERSION &&
        gatewayRadioProfile.request.type == RADIO_PROFILE_REQUEST_TYPE &&
        memcmp(&gatewayRadioProfile.active, &gatewayRadioProfile.request.profile,
               sizeof(gatewayRadioProfile.active)) == 0 &&
        memcmp(&gatewayRadioProfile.requested, &gatewayRadioProfile.request.profile,
               sizeof(gatewayRadioProfile.requested)) == 0) {
        GW_LOG_INFO("Authenticated radio profile ACK previously received from %02X:%02X:%02X:%02X:%02X:%02X for SF%u BW%.1fkHz CR4/%u; awaiting new-PHY telemetry.",
                    gatewayRadioProfile.request.sensorMac[0], gatewayRadioProfile.request.sensorMac[1],
                    gatewayRadioProfile.request.sensorMac[2], gatewayRadioProfile.request.sensorMac[3],
                    gatewayRadioProfile.request.sensorMac[4], gatewayRadioProfile.request.sensorMac[5],
                    gatewayRadioProfile.active.spreadingFactor,
                    RADIO_PROFILE_BANDWIDTHS_KHZ[gatewayRadioProfile.active.bandwidthIndex],
                    gatewayRadioProfile.active.codingRate);
    }

    initRadio();

    drawMain();
}

void initRadio() {
#ifdef  RADIO_TCXO_ENABLE
    pinMode(RADIO_TCXO_ENABLE, OUTPUT);
    digitalWrite(RADIO_TCXO_ENABLE, HIGH);
#endif

    // initialize radio with default settings
    int state = radio.begin();

    if (state == RADIOLIB_ERR_NONE) {
        GW_LOG_INFO("[%s]: Radio Initializing ... success!", RADIO_TYPE_STR);
    } else {
        GW_LOG_ERROR("[%s]: Radio Initializing ... failed, code %d", RADIO_TYPE_STR, state);
        while (true);
    }

    uint32_t profileCoreUs = 0, profileDevelopmentUs = 0, profileRequestUs = 0, profileAckUs = 0;
    if (!radioProfileAirtimes(radio, gatewayRadioProfile.active, profileCoreUs,
                              profileDevelopmentUs, profileRequestUs, profileAckUs)) {
        GW_LOG_ERROR("Stored Gateway radio profile is invalid for this radio or exceeds sleep limits; using build default.");
        gatewayRadioProfile.active = RADIO_PROFILE_DEFAULT;
    }

    // Set interrupt callback for packet reception
    radio.setPacketReceivedAction(setFlag);

    /*
    *   Sets carrier frequency.
    *   SX1278/SX1276 : Allowed values range from 137.0 MHz to 525.0 MHz.
    *   SX1268/SX1262 : Allowed values are in range from 150.0 to 960.0 MHz.
    *   SX1280        : Allowed values are in range from 2400.0 to 2500.0 MHz.
    *   LR1121        : Allowed values are in range from 150.0 to 960.0 MHz, 1900 - 2200 MHz and 2400 - 2500 MHz. Will also perform calibrations.
    * * * */

    // Declare configErrors before use
    int configErrors = 0;

    if (radio.setFrequency(CONFIG_RADIO_FREQ) == RADIOLIB_ERR_INVALID_FREQUENCY) {
        GW_LOG_ERROR("Selected frequency is invalid for this module!");
        configErrors++;
    } else {
        GW_LOG_INFO("Freq: %.3f MHz OK", CONFIG_RADIO_FREQ);
    }

    /*
    *   Sets LoRa link bandwidth.
    *   SX1278/SX1276 : Allowed values are 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125, 250 and 500 kHz. Only available in %LoRa mode.
    *   SX1268/SX1262 : Allowed values are 7.8, 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125.0, 250.0 and 500.0 kHz.
    *   SX1280        : Allowed values are 203.125, 406.25, 812.5 and 1625.0 kHz.
    *   LR1121        : Allowed values are 62.5, 125.0, 250.0 and 500.0 kHz.
    * * * */
    if (radio.setBandwidth(RADIO_PROFILE_BANDWIDTHS_KHZ[gatewayRadioProfile.active.bandwidthIndex]) != RADIOLIB_ERR_NONE) {
        GW_LOG_ERROR("Selected bandwidth is invalid for this module!");
        configErrors++;
    } else {
        GW_LOG_INFO("BW: %.1f kHz OK", RADIO_PROFILE_BANDWIDTHS_KHZ[gatewayRadioProfile.active.bandwidthIndex]);
    }


    /*
    * Sets LoRa link spreading factor.
    * SX1278/SX1276 :  Allowed values range from 6 to 12. Only available in LoRa mode.
    * SX1262        :  Allowed values range from 5 to 12.
    * SX1280        :  Allowed values range from 5 to 12.
    * LR1121        :  Allowed values range from 5 to 12.
    * * * */
    if (radio.setSpreadingFactor(gatewayRadioProfile.active.spreadingFactor) != RADIOLIB_ERR_NONE) {
        GW_LOG_ERROR("Selected spreading factor is invalid for this module!");
        configErrors++;
    } else {
        GW_LOG_INFO("SF: %u OK", gatewayRadioProfile.active.spreadingFactor);
    }

    /*
    * Sets LoRa coding rate denominator.
    * SX1278/SX1276/SX1268/SX1262 : Allowed values range from 5 to 8. Only available in LoRa mode.
    * SX1280        :  Allowed values range from 5 to 8.
    * LR1121        :  Allowed values range from 5 to 8.
    * * * */
    if (radio.setCodingRate(gatewayRadioProfile.active.codingRate) != RADIOLIB_ERR_NONE) {
        GW_LOG_ERROR("Selected coding rate is invalid for this module!");
        configErrors++;
    } else {
        GW_LOG_INFO("CR: 4/%u OK", gatewayRadioProfile.active.codingRate);
    }

    /*
    * Sets LoRa sync word.
    * SX1278/SX1276/SX1268/SX1262/SX1280 : Sets LoRa sync word. Only available in LoRa mode.
    * * */
    if (radio.setSyncWord(0xAB) != RADIOLIB_ERR_NONE) {
        GW_LOG_ERROR("Unable to set sync word!");
        configErrors++;
    } else {
        GW_LOG_INFO("SW: 0xAB OK");
    }

    /*
    * Sets transmission output power.
    * SX1278/SX1276 :  Allowed values range from -3 to 15 dBm (RFO pin) or +2 to +17 dBm (PA_BOOST pin). High power +20 dBm operation is also supported, on the PA_BOOST pin. Defaults to PA_BOOST.
    * SX1262        :  Allowed values are in range from -9 to 22 dBm. This method is virtual to allow override from the SX1261 class.
    * SX1268        :  Allowed values are in range from -9 to 22 dBm.
    * SX1280        :  Allowed values are in range from -18 to 13 dBm. PA Version range : -18 ~ 3dBm
    * LR1121        :  Allowed values are in range from -17 to 22 dBm (high-power PA) or -18 to 13 dBm (High-frequency PA), PA Version range : -9 ~ 0dBm
    * * * */
    if (radio.setOutputPower(gatewayConfig.radioTxPower) == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
        GW_LOG_ERROR("Selected output power is invalid for this module!");
        configErrors++;
    } else {
        gatewayRadioOutputPowerApplied = true;
        gatewayAppliedRadioTxPower = gatewayConfig.radioTxPower;
        GW_LOG_INFO("TX Power: %ddBm OK", gatewayAppliedRadioTxPower);
    }

#if !defined(USING_SX1280) && !defined(USING_LR1121) && !defined(USING_SX1280PA)
    /*
    * Sets current limit for over current protection at transmitter amplifier.
    * SX1278/SX1276 : Allowed values range from 45 to 120 mA in 5 mA steps and 120 to 240 mA in 10 mA steps.
    * SX1262/SX1268 : Allowed values range from 45 to 120 mA in 2.5 mA steps and 120 to 240 mA in 10 mA steps.
    * NOTE: set value to 0 to disable overcurrent protection
    * * * */
    if (radio.setCurrentLimit(140) == RADIOLIB_ERR_INVALID_CURRENT_LIMIT) {
        GW_LOG_ERROR("Selected current limit is invalid for this module!");
        configErrors++;
    } else {
        GW_LOG_INFO("Current Limit: 140 mA OK");
    }
#endif

    /*
    * Sets preamble length for LoRa or FSK modem.
    * SX1278/SX1276 : Allowed values range from 6 to 65535 in %LoRa mode or 0 to 65535 in FSK mode.
    * SX1262/SX1268 : Allowed values range from 1 to 65535.
    * SX1280        : Allowed values range from 1 to 65535. preamble length is multiple of 4
    * LR1121        : Allowed values range from 1 to 65535.
    * * */
    // Removed preamble override to let it use default (8) matching the sensor code.
    /*if (radio.setPreambleLength(16) == RADIOLIB_ERR_INVALID_PREAMBLE_LENGTH) {
        Serial.println(F("Selected preamble length is invalid for this module!"));
        while (true);
    }*/

    // Enables or disables CRC check of received packets.
    if (radio.setCRC(true) == RADIOLIB_ERR_INVALID_CRC_CONFIGURATION) {
        GW_LOG_ERROR("Selected CRC configuration is invalid for this module!");
        configErrors++;
    } else {
        GW_LOG_INFO("CRC: Enabled OK");
    }

    if (configErrors == 0) {
        GW_LOG_INFO("Boot radio profile applied successfully: SF%u BW%.1fkHz CR4/%u.",
                    gatewayRadioProfile.active.spreadingFactor,
                    RADIO_PROFILE_BANDWIDTHS_KHZ[gatewayRadioProfile.active.bandwidthIndex],
                    gatewayRadioProfile.active.codingRate);
    }

    if (profileCoreUs != radio.getTimeOnAir(RADIO_PROFILE_OPERATION_SIZE) ||
        profileDevelopmentUs != radio.getTimeOnAir(RADIO_PROFILE_TELEMETRY_FULL_SIZE) ||
        profileRequestUs != radio.getTimeOnAir(sizeof(RadioProfileRequest)) ||
        profileAckUs != radio.getTimeOnAir(sizeof(RadioProfileAck))) {
        GW_LOG_ERROR("Stateless RadioLib profile airtime differs from the active modem airtime.");
        configErrors++;
    }

    if (rtcGatewayAirtime.magicWord != RTC_GATEWAY_AIRTIME_MAGIC ||
        memcmp(&rtcGatewayAirtime.radioProfile, &gatewayRadioProfile.active, sizeof(gatewayRadioProfile.active)) != 0) {
        memset(&rtcGatewayAirtime, 0, sizeof(rtcGatewayAirtime));
        rtcGatewayAirtime.magicWord = RTC_GATEWAY_AIRTIME_MAGIC;
        rtcGatewayAirtime.radioProfile = gatewayRadioProfile.active;
    }
    rtcGatewayAirtime.radioProfile = gatewayRadioProfile.active;
    rtcGatewayAirtime.calculatedBeaconUs = radio.getTimeOnAir(1);
    rtcGatewayAirtime.calculatedConfigUs = max(radio.getTimeOnAir(sizeof(ConfigPayload)),
                                               radio.getTimeOnAir(sizeof(RadioProfileRequest)));
    const uint32_t calculatedBurstUs = rtcGatewayAirtime.calculatedBeaconUs + rtcGatewayAirtime.calculatedConfigUs;
    rtcGatewayAirtime.countedBurstUs = max(calculatedBurstUs, rtcGatewayAirtime.measuredBurstUs);
    // A Gateway restart has no active Sensor receive window. Wait for the next
    // uplink, then send the first response immediately and start pacing there.
    gatewayNextTxMs = 0;
    gatewayHasSentAfterSensorRx = false;
    uint32_t configuredMinimum = sensorMinimumTotalIntervalSeconds(gatewayConfig.sensorDevMode);
    bool dutyConfigUpdated = false;
    if (gatewayConfig.sensorSleepSeconds < configuredMinimum) {
        GW_LOG_WARN("Rejected default sleep %us: minimum=%lus for %s payload; using minimum.",
                    gatewayConfig.sensorSleepSeconds, (unsigned long)configuredMinimum,
                    gatewayConfig.sensorDevMode ? "development" : "operation");
        gatewayConfig.sensorSleepSeconds = (uint8_t)configuredMinimum;
        dutyConfigUpdated = true;
    }
    for (SensorPresence& retained : sensorPresence) {
        if (!retained.mac[0]) continue;
        const bool developmentMode = retained.hasDevModeOverride ? retained.devModeOverride : gatewayConfig.sensorDevMode;
        const uint32_t minimum = sensorMinimumTotalIntervalSeconds(developmentMode);
        const uint32_t sleep = retained.hasSleepOverride ? retained.sleepOverrideSeconds : gatewayConfig.sensorSleepSeconds;
        if (sleep < minimum && minimum <= UINT8_MAX) {
            retained.hasSleepOverride = true;
            retained.sleepOverrideSeconds = static_cast<uint8_t>(minimum);
            dutyConfigUpdated = true;
            GW_LOG_WARN("Raised retained sleep for %s to %lus to satisfy the 1%% duty cycle.",
                        retained.mac, static_cast<unsigned long>(minimum));
        }
    }
    if (dutyConfigUpdated && gatewayLogSink.isSdReady() && !saveGatewayConfig())
        GW_LOG_ERROR("Failed to persist duty-cycle-adjusted Sensor configuration.");
    GW_LOG_INFO("Duty cycle: beacon=%luus config=%luus burst=%luus cooldown=%lums; Sensor minimum sleep=%lus.",
                (unsigned long)rtcGatewayAirtime.calculatedBeaconUs,
                (unsigned long)rtcGatewayAirtime.calculatedConfigUs,
                (unsigned long)rtcGatewayAirtime.countedBurstUs,
                (unsigned long)LoRaDutyCycle::requiredSilenceMs(rtcGatewayAirtime.countedBurstUs),
                (unsigned long)configuredMinimum);

    // ============================================================================
    // VALIDATE CONFIGURATION AND REPORT STATUS
    // ============================================================================
    
    if (configErrors > 0) {
        GW_LOG_WARN("Radio configuration completed with %d error(s).", configErrors);
        GW_LOG_WARN("The radio may still function, but some parameters were not set correctly.");
        GW_LOG_WARN("Please check hardware connections and power supply stability.");
    } else {
        GW_LOG_INFO("All radio parameters configured successfully!");
    }

    // Delay to allow radio internal circuits to stabilize after configuration
    vTaskDelay(pdMS_TO_TICKS(100));

#if  defined(USING_LR1121)
#if defined(USING_LR1121PA)
    if (CONFIG_RADIO_FREQ < 2400) {
        GW_LOG_INFO("LR1121 PA Version Using low frequency switch table for PA version");
        radio.setRfSwitchTable(pa_version_rf_switch_dio_pins, low_freq_switch_table);
    } else {
        GW_LOG_INFO("LR1121 PA Version Using high frequency switch table for PA version");
        radio.setRfSwitchTable(pa_version_rf_switch_dio_pins, high_freq_switch_table);
    }
#else   //  Version without PA rf switch table
    GW_LOG_INFO("LR1121 without PA Version");
    static const uint32_t rfswitch_dio_pins[] = {
        RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6,
        RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC
    };
    static const Module::RfSwitchMode_t rfswitch_table[] = {
        // mode                  DIO5  DIO6
        { LR11x0::MODE_STBY,   { LOW,  LOW  } },
        { LR11x0::MODE_RX,     { HIGH, LOW  } },
        { LR11x0::MODE_TX,     { LOW,  HIGH } },
        { LR11x0::MODE_TX_HP,  { LOW,  HIGH } },
        { LR11x0::MODE_TX_HF,  { LOW,  LOW  } },
        { LR11x0::MODE_GNSS,   { LOW,  LOW  } },
        { LR11x0::MODE_WIFI,   { LOW,  LOW  } },
        END_OF_MODE_TABLE,
    };
    radio.setRfSwitchTable(rfswitch_dio_pins, rfswitch_table);
#endif /*USING_LR1121PA*/

    // LR1121 TCXO Voltage 2.85~3.15V
    radio.setTCXO(3.0);

#endif /*USING_LR1121*/

#ifdef USING_DIO2_AS_RF_SWITCH
#ifdef USING_SX1262
    // Some SX126x modules use DIO2 as RF switch. To enable
    // this feature, the following method can be used.
    // NOTE: As long as DIO2 is configured to control RF switch,
    //       it can't be used as interrupt pin!
    if (radio.setDio2AsRfSwitch() != RADIOLIB_ERR_NONE) {
        GW_LOG_ERROR("Failed to set DIO2 as RF switch!");
        while (true);
    }
#endif //USING_SX1262
#endif //USING_DIO2_AS_RF_SWITCH


#ifdef RADIO_RX_PIN
    // SX1280 PA Version
    radio.setRfSwitchPins(RADIO_RX_PIN, RADIO_TX_PIN);
#endif


#ifdef RADIO_SWITCH_PIN
    // T-MOTION
    const uint32_t pins[] = {
        RADIO_SWITCH_PIN, RADIO_SWITCH_PIN, RADIOLIB_NC,
    };
    static const Module::RfSwitchMode_t table[] = {
        {Module::MODE_IDLE,  {0,  0} },
        {Module::MODE_RX,    {1, 0} },
        {Module::MODE_TX,    {0, 1} },
        END_OF_MODE_TABLE,
    };
    radio.setRfSwitchTable(pins, table);
#endif

#ifdef RADIO_CTRL
    GW_LOG_INFO("Turn on LAN, Enter Rx mode.");
    /*
    * 2W and BPF LoRa LAN Control ,set HIGH turn on LAN ,RX Mode
    * */
    digitalWrite(RADIO_CTRL, HIGH);
#endif /*RADIO_CTRL*/

    vTaskDelay(pdMS_TO_TICKS(1000));

    // start listening for LoRa packets
    state = radio.startReceive();
    gatewayRadioAvailable = configErrors == 0 && state == RADIOLIB_ERR_NONE;
    if (state == RADIOLIB_ERR_NONE) {
        GW_LOG_INFO("Radio started listening successfully!");
    } else {
        GW_LOG_ERROR("Radio failed to start, code %d", state);
    }
}

void loop()
{
    serviceGatewayRelay();
    if (gatewayRadioProfile.awaitingAck && gatewayProfileAckDeadlineMs != 0 &&
        static_cast<int32_t>(millis() - gatewayProfileAckDeadlineMs) >= 0) {
        gatewayRadioProfile.awaitingAck = false;
        snprintf(gatewayRadioProfile.status, sizeof(gatewayRadioProfile.status), "%s", "unconfirmed");
        gatewayProfileAckDeadlineMs = 0;
        if (!saveGatewayConfig()) GW_LOG_ERROR("Radio profile ACK timeout could not be persisted.");
        else GW_LOG_WARN("Radio profile ACK timeout; transition remains unconfirmed and will not be replayed automatically.");
    }
    // Radio processing always precedes SD, network, display, and other background work.
    while (xSemaphoreTake(radioSemaphore, 0) == pdTRUE) {
        const TickType_t packetIrqTick = lastRadioIrqTick;
        const uint32_t rxDispatchDelayMs = (xTaskGetTickCount() - packetIrqTick) * portTICK_PERIOD_MS;
        
        // process received data from the radio 
        int numBytes = radio.getPacketLength();
        byte byteArr[256];
        
        // Memory safety: Validate packet length against buffer size
        if (numBytes <= 0 || numBytes > sizeof(byteArr)) {
            GW_LOG_WARN("Invalid packet length from radio: %d bytes", numBytes);
            xSemaphoreTake(radioSemaphore, 0);
            radio.startReceive();
            continue; // Skip to next semaphore check in loop
        }
        
        int state = radio.readData(byteArr, numBytes);
        const uint32_t packetReceivedMs = millis();

        if (state == RADIOLIB_ERR_NONE) {
            // capture reception information
            const float receivedRssi = radio.getRSSI();
            const float receivedSnr = radio.getSNR();
            const int64_t receivedAtUs = esp_timer_get_time();
            rssi = String(receivedRssi) + "dBm";
            snr = String(receivedSnr) + "dB";

            if (numBytes == sizeof(RadioProfileAck) &&
                byteArr[0] == RADIO_PROFILE_FRAME_VERSION && byteArr[1] == RADIO_PROFILE_ACK_TYPE) {
                RadioProfileAck ack{};
                memcpy(&ack, byteArr, sizeof(ack));
                uint8_t key[LoraHomeCrypto::TELEMETRY_AUTH_KEY_SIZE] = {};
                uint8_t input[3 + sizeof(gatewayRadioProfile.request) + 2] = {'A', 'C', 'K'};
                memcpy(input + 3, &gatewayRadioProfile.request, sizeof(gatewayRadioProfile.request));
                input[3 + sizeof(gatewayRadioProfile.request)] = ack.version;
                input[4 + sizeof(gatewayRadioProfile.request)] = ack.type;
                const bool validAck = gatewayRadioProfile.pending && gatewayRadioProfile.awaitingAck &&
                    LoraHomeCrypto::deriveRadioProfileAuthKey(AES_NETWORK_KEY, key) &&
                    LoraHomeCrypto::verifyTelemetryTag(input, sizeof(input), key, ack.tag);
                memset(key, 0, sizeof(key));
                memset(input, 0, sizeof(input));
                char boundMac[18] = {};
                snprintf(boundMac, sizeof(boundMac), "%02X:%02X:%02X:%02X:%02X:%02X",
                         gatewayRadioProfile.request.sensorMac[0], gatewayRadioProfile.request.sensorMac[1],
                         gatewayRadioProfile.request.sensorMac[2], gatewayRadioProfile.request.sensorMac[3],
                         gatewayRadioProfile.request.sensorMac[4], gatewayRadioProfile.request.sensorMac[5]);
                if (!validAck || gatewayRadioProfile.request.version != RADIO_PROFILE_FRAME_VERSION ||
                    gatewayRadioProfile.request.type != RADIO_PROFILE_REQUEST_TYPE ||
                    memcmp(&gatewayRadioProfile.request.profile, &gatewayRadioProfile.requested,
                           sizeof(gatewayRadioProfile.requested)) != 0 ||
                    gatewayConfig.pairedSensorMac.length() != 17 ||
                    strcasecmp(boundMac, gatewayConfig.pairedSensorMac.c_str()) != 0) {
                    GW_LOG_WARN("Ignored stale, invalid, or unbound radio profile ACK.");
                } else {
                    const GatewayRadioProfileState previousProfileState = gatewayRadioProfile;
                    gatewayRadioProfile.active = gatewayRadioProfile.request.profile;
                    gatewayRadioProfile.awaitingAck = false;
                    gatewayRadioProfile.phase = 2;
                    snprintf(gatewayRadioProfile.status, sizeof(gatewayRadioProfile.status), "%s", "unconfirmed");
                    if (saveGatewayConfig()) {
                        if (gatewayRadioProfile.commandId[0]) {
                            publishConfigResult(boundMac, gatewayRadioProfile.commandId, "unconfirmed");
                        }
                        GW_LOG_INFO("Authenticated radio profile ACK received from %s for SF%u BW%.1fkHz CR4/%u; restarting Gateway on requested PHY.",
                                    boundMac, gatewayRadioProfile.active.spreadingFactor,
                                    RADIO_PROFILE_BANDWIDTHS_KHZ[gatewayRadioProfile.active.bandwidthIndex],
                                    gatewayRadioProfile.active.codingRate);
                        ESP.restart();
                        return;
                    }
                    gatewayRadioProfile = previousProfileState;
                    gatewayRadioProfile.awaitingAck = false;
                    snprintf(gatewayRadioProfile.status, sizeof(gatewayRadioProfile.status), "%s", "failed");
                    if (gatewayRadioProfile.commandId[0]) {
                        publishConfigResult(boundMac, gatewayRadioProfile.commandId, "failed");
                    }
                    GW_LOG_ERROR("Profile ACK received but Gateway profile commit failed; Sensor may already have switched. Manual recovery required.");
                }
                radio.startReceive();
                continue;
            }

            // decrypt the received payload
            GW_LOG_DEBUG("Received %d bytes from radio", numBytes);
            const bool profileTelemetryFrame =
                ((numBytes == RADIO_PROFILE_OPERATION_SIZE && byteArr[0] == RADIO_PROFILE_OPERATION_VERSION) ||
                 ((numBytes == RADIO_PROFILE_TELEMETRY_CORE_SIZE || numBytes == RADIO_PROFILE_TELEMETRY_FULL_SIZE) &&
                  byteArr[0] == RADIO_PROFILE_FRAME_VERSION)) && byteArr[1] == RADIO_PROFILE_TELEMETRY_TYPE;
            const bool authenticatedFrame = profileTelemetryFrame ||
                ((numBytes == TELEMETRY_AUTH_CORE_SIZE || numBytes == TELEMETRY_AUTH_FULL_SIZE) &&
                 byteArr[0] == TELEMETRY_FRAME_VERSION && byteArr[1] == TELEMETRY_FRAME_TYPE);
            const bool legacyFrame = numBytes == TELEMETRY_CORE_SIZE || numBytes == sizeof(TelemetryPayload);
            if (authenticatedFrame || legacyFrame) {
                uint8_t decoded[sizeof(TelemetryPayload)] = {};
                TelemetryPayload rxPayload{};
                // FIX: Cast to uint8_t* to treat both source and destination as raw byte arrays
                // This prevents incorrect interpretation of bytes when copying to mixed-type struct
                uint8_t* payloadPtr = (uint8_t*)&rxPayload;
                size_t encryptedLength = 0;
                size_t decodedLength = 0;
                RadioProfile receivedProfile{};
                SensorPresence* authenticatedPresence = nullptr;
                const uint8_t* candidate = byteArr + (authenticatedFrame ? 2 : 0);
                char candidateMac[18];
                snprintf(candidateMac, sizeof(candidateMac), "%02X:%02X:%02X:%02X:%02X:%02X",
                         candidate[0], candidate[1], candidate[2], candidate[3], candidate[4], candidate[5]);
                const char* rejectReason = nullptr;
                bool identityAuthenticated = false;
                if (authenticatedFrame) {
                    const bool decodedFrame = profileTelemetryFrame
                        ? LoraHomeCrypto::decodeAuthenticatedProfileTelemetryFrame(
                              byteArr, numBytes, AES_NETWORK_KEY, decoded, sizeof(decoded),
                              &decodedLength, &receivedProfile)
                        : LoraHomeCrypto::decodeAuthenticatedTelemetryFrame(
                              byteArr, numBytes, AES_NETWORK_KEY, decoded, sizeof(decoded), &decodedLength);
                    if (!decodedFrame) {
                        rejectReason = "authentication failed";
                    } else {
                        identityAuthenticated = true;
                        encryptedLength = decodedLength - 12;
                        memcpy(payloadPtr, decoded, decodedLength);
                    }
                } else {
                    memcpy(decoded, byteArr, numBytes);
                    memcpy(payloadPtr, decoded, 12);
                    encryptedLength = numBytes - 12;
                    decodedLength = numBytes;
                }
                if (!rejectReason && (gatewayConfig.pairedSensorMac.length() != 17 ||
                    strcasecmp(gatewayConfig.pairedSensorMac.c_str(), candidateMac) != 0)) {
                    rejectReason = gatewayConfig.pairedSensorMac.length() == 0 ? "unpaired" : "pairing mismatch";
                }
                if (!rejectReason && authenticatedFrame) {
                    authenticatedPresence = findSensorPresence(candidateMac);
                    if (!authenticatedPresence) {
                        rejectReason = "pairing mismatch";
                    } else if (authenticatedPresence->authenticatedProtocol && authenticatedPresence->bootNonceKnown &&
                               authenticatedPresence->lastBootNonce == rxPayload.bootNonce &&
                               rxPayload.msgCount <= authenticatedPresence->lastAuthenticatedMsgCount) {
                        rejectReason = "stale or replayed frame";
                    }
                }
                if (!rejectReason && profileTelemetryFrame && !radioProfileIsValid(radio, receivedProfile)) {
                    rejectReason = "invalid advertised radio profile";
                }
                if (!rejectReason && profileTelemetryFrame &&
                    memcmp(&receivedProfile, &gatewayRadioProfile.active, sizeof(receivedProfile)) != 0) {
                    rejectReason = "radio profile does not match active Gateway PHY";
                }
                if (rejectReason) {
                    if ((strcmp(rejectReason, "unpaired") == 0 || strcmp(rejectReason, "pairing mismatch") == 0) &&
                        isValidSensorMac(candidateMac) &&
                        LoraHomeCrypto::cryptPayload(decoded + 12, encryptedLength, AES_NETWORK_KEY,
                                                     rxPayload.mac, rxPayload.bootNonce, rxPayload.msgCount)) {
                        snprintf(discoveredSensorMac, sizeof(discoveredSensorMac), "%s", candidateMac);
                        discoveredSensorAuthenticated = identityAuthenticated;
                    }
                    static char lastRejectedMac[18] = {};
                    static const char* lastRejectReason = nullptr;
                    static uint32_t lastRejectLogMs = 0;
                    if (strcmp(lastRejectedMac, candidateMac) != 0 || lastRejectReason != rejectReason ||
                        millis() - lastRejectLogMs >= 60000UL) {
                        GW_LOG_WARN("Sensor %s identity=%s pairing=%s: %s; frame quarantined.", candidateMac,
                                    identityAuthenticated ? "authenticated" : "unverified",
                                    gatewayConfig.pairedSensorMac.length() ? "configured" : "unpaired", rejectReason);
                        snprintf(lastRejectedMac, sizeof(lastRejectedMac), "%s", candidateMac);
                        lastRejectReason = rejectReason;
                        lastRejectLogMs = millis();
                    }
                    radio.startReceive();
                    continue;
                }
                uint8_t* dataPtr = decoded + 12;
                if (!LoraHomeCrypto::cryptPayload(dataPtr, encryptedLength, AES_NETWORK_KEY,
                                                  rxPayload.mac, rxPayload.bootNonce, rxPayload.msgCount)) {
                    GW_LOG_WARN("Discarding telemetry: decryption failed.");
                    radio.startReceive();
                    continue;
                }
                memcpy(payloadPtr, decoded, decodedLength);
                if (rxPayload.isDevMode > 1) {
                    GW_LOG_WARN("Discarding telemetry with invalid mode flag.");
                    radio.startReceive();
                    continue;
                }
                if (numBytes == RADIO_PROFILE_OPERATION_SIZE && rxPayload.isDevMode != 0) {
                    GW_LOG_WARN("Discarding Operation telemetry with Development Mode flag.");
                    radio.startReceive();
                    continue;
                }
                if (rxPayload.isDevMode == 1 && decodedLength != sizeof(TelemetryPayload)) {
                    GW_LOG_WARN("Discarding Development telemetry with incomplete payload.");
                    radio.startReceive();
                    continue;
                }

                // --- TIME-CRITICAL SECTION: Respond to Sensor ---
                // This block must execute as fast as possible to meet the sensor's short RX window.
                // construct sensor configuration payload

                char targetMacStr[18];
                sprintf(targetMacStr, "%02X:%02X:%02X:%02X:%02X:%02X",
                        rxPayload.mac[0], rxPayload.mac[1], rxPayload.mac[2],
                        rxPayload.mac[3], rxPayload.mac[4], rxPayload.mac[5]);
                SensorPresence* targetPresence = findSensorPresence(targetMacStr);
                if (legacyFrame && (gatewayConfigFromNvs || (targetPresence && targetPresence->authenticatedProtocol))) {
                    GW_LOG_WARN("Discarding legacy telemetry downgrade from protected Sensor %s.", targetMacStr);
                    radio.startReceive();
                    continue;
                }
                // This uplink can confirm only a downlink completed before it.
                // A downlink sent in response to this uplink is checked later.
                const bool sensorRestarted = targetPresence && targetPresence->bootNonceKnown &&
                                             targetPresence->lastBootNonce != rxPayload.bootNonce;
                if (targetPresence) {
                    targetPresence->lastBootNonce = rxPayload.bootNonce;
                    targetPresence->bootNonceKnown = true;
                    if (authenticatedFrame) {
                        targetPresence->authenticatedProtocol = true;
                        targetPresence->lastAuthenticatedMsgCount = rxPayload.msgCount;
                        if (profileTelemetryFrame) {
                            targetPresence->radioProfile = receivedProfile;
                            targetPresence->radioProfileKnown = true;
                        }
                        if (!saveGatewayConfig()) {
                            static bool warnedVolatileReplay = false;
                            if (!warnedVolatileReplay) {
                                GW_LOG_WARN("SD persistence unavailable; authenticated telemetry replay state is held in RAM only.");
                                warnedVolatileReplay = true;
                            }
                        }
                    }
                }
                if (profileTelemetryFrame && gatewayRadioProfile.pending && gatewayRadioProfile.phase == 2 &&
                    memcmp(&receivedProfile, &gatewayRadioProfile.requested, sizeof(receivedProfile)) == 0) {
                    const GatewayRadioProfileState previousProfileState = gatewayRadioProfile;
                    gatewayRadioProfile.pending = false;
                    gatewayRadioProfile.phase = 0;
                    snprintf(gatewayRadioProfile.status, sizeof(gatewayRadioProfile.status), "%s", "applied");
                    const bool profilePersisted = saveGatewayConfig();
                    if (!profilePersisted) {
                        gatewayRadioProfile = previousProfileState;
                        GW_LOG_ERROR("New-PHY Sensor telemetry matched, but applied state could not be persisted; transition remains unconfirmed.");
                    } else {
                        if (gatewayRadioProfile.commandId[0]) {
                            publishConfigResult(targetMacStr, gatewayRadioProfile.commandId, "applied");
                        }
                        GW_LOG_INFO("Radio profile transition confirmed by fresh authenticated Sensor telemetry.");
                    }
                }
                const DesiredSensorConfig desired = desiredSensorConfig(targetPresence);

                ConfigPayload txConfig;
                txConfig.header[0] = 'C'; // harcoded
                txConfig.header[1] = 'F'; // hardcoded
                memcpy(txConfig.targetMac, rxPayload.mac, 6);
                txConfig.networkKey = NETWORK_KEY;
                txConfig.sleepInterval = desired.sleepInterval;
                txConfig.configVersion = 4;
                txConfig.isDevMode = desired.developmentMode ? 1 : 0;
                txConfig.txPower = desired.txPower;
                txConfig.cpuFrequencyMhz = targetPresence && targetPresence->pendingCpuChange && !targetPresence->pendingModeChange &&
                                           rxPayload.isDevMode == 1
                                               ? desired.cpuFrequencyMhz : CONFIG_CPU_UNCHANGED;

                //ensure RTC is synced before transmiting timeoffset
                if (!hasValidRtcTime) {
                    if (!refreshRtcTrustFromSystemClock()){
                        GW_LOG_WARN("Unable to Sync from NTP server.");
                    } 
                } 
                
                // calculate time offset
                time_t currentSysTime;
                time(&currentSysTime);
                if (hasValidRtcTime && isClockValid(currentSysTime)) {
                    txConfig.timeOffset = (uint32_t)(currentSysTime - CUSTOM_EPOCH);
                } else {
                    txConfig.timeOffset = 0;
                    if ((rxPayload.needsTimeSync & 1) != 0) GW_LOG_WARN("Sensor time sync deferred until Gateway time is valid.");
                }

                // determine and transmit the update beacon 
                bool sensorNeedsConfig = !telemetryConfirmsConfig(rxPayload, desired, targetPresence);
                if (targetPresence && targetPresence->pendingCpuChange && !rxPayload.isDevMode) {
                    sensorNeedsConfig = rxPayload.sleepInterval != desired.sleepInterval ||
                        desired.developmentMode || rxPayload.txPower != desired.txPower;
                }
                bool timeSyncPending = (rxPayload.needsTimeSync & 1) && txConfig.timeOffset > 0;
                const bool profileCanDeliver = gatewayRadioProfile.pending && !gatewayRadioProfile.awaitingAck &&
                    rxPayload.isDevMode == 1 && gatewayConfig.pairedSensorMac.length() == 17 &&
                    strcasecmp(targetMacStr, gatewayConfig.pairedSensorMac.c_str()) == 0;
                // Three pulse attempts belong to one config delivery. A failed
                // report ends that delivery until another user config is queued.
                
                // Configuration delivery is independent of NTP. A missing trusted
                // time only defers the time field, not mode/power/sleep/CPU changes.
                bool updatePending = sensorNeedsConfig || timeSyncPending || profileCanDeliver;
                const bool firstGatewayResponse = !gatewayHasSentAfterSensorRx;
                const bool forceImmediateResponse = firstGatewayResponse || sensorRestarted;
                const bool sendResponse = updatePending;
                if (sendResponse && (forceImmediateResponse || TxCycleCheck())) {
                    gatewayBurstNonCompliant = forceImmediateResponse;
                    if (forceImmediateResponse) {
                        GW_LOG_WARN("Immediate Gateway response to %s after %s; counting non-compliant burst.",
                                    targetMacStr, sensorRestarted ? "Sensor restart" : "Gateway restart");
                    }
                    // The Sensor arms RX immediately after its TX-complete IRQ.
                    // Hold the shared guard, then require enough time for the
                    // complete beacon before the bounded RX window closes.
                    const uint32_t rxAgeMs = (xTaskGetTickCount() - packetIrqTick) * portTICK_PERIOD_MS;
                    if (rxAgeMs < SENSOR_RX_READY_GUARD_MS) {
                        vTaskDelay(pdMS_TO_TICKS(SENSOR_RX_READY_GUARD_MS - rxAgeMs));
                    }
                    const uint32_t beaconStartDelayMs = (xTaskGetTickCount() - packetIrqTick) * portTICK_PERIOD_MS;
                    const uint32_t latestBeaconStartMs = SENSOR_RX_READY_GUARD_MS + SENSOR_BEACON_RX_MARGIN_MS;
                    if (beaconStartDelayMs >= latestBeaconStartMs) {
                        // The beacon would finish after the Sensor's bounded RX
                        // window. Keep the config pending for its next uplink.
                        GW_LOG_WARN("Downlink to %s skipped: beacon TX start=%lums after RX IRQ, deadline=%lums (dispatch=%lums).",
                                    targetMacStr, (unsigned long)beaconStartDelayMs,
                                    (unsigned long)latestBeaconStartMs, (unsigned long)rxDispatchDelayMs);
                        gatewayBurstNonCompliant = false;
                    } else if (sendUpdateBeacon()) { // check if beacon was transmitted
                        GW_LOG_INFO("Downlink timing for %s: dispatch=%lums, beaconStart=%lums after RX IRQ; Sensor beacon window is about %lums.",
                                    targetMacStr, (unsigned long)rxDispatchDelayMs,
                                    (unsigned long)beaconStartDelayMs,
                                    (unsigned long)((rtcGatewayAirtime.calculatedBeaconUs + 999) / 1000 + SENSOR_BEACON_RX_MARGIN_MS));
                        xSemaphoreTake(radioSemaphore, 0); // clear before tx
                        uint32_t configTxStartedUs = micros();
                        int txState = RADIOLIB_ERR_NONE;
                        if (profileCanDeliver) {
                            RadioProfileRequest request{};
                            request.version = RADIO_PROFILE_FRAME_VERSION;
                            request.type = RADIO_PROFILE_REQUEST_TYPE;
                            memcpy(request.sensorMac, rxPayload.mac, sizeof(request.sensorMac));
                            request.sessionNonce = rxPayload.bootNonce;
                            request.uplinkCounter = rxPayload.msgCount;
                            request.profile = gatewayRadioProfile.requested;
                            uint8_t key[LoraHomeCrypto::TELEMETRY_AUTH_KEY_SIZE] = {};
                            const bool signedRequest = LoraHomeCrypto::deriveRadioProfileAuthKey(AES_NETWORK_KEY, key) &&
                                LoraHomeCrypto::computeTelemetryTag(reinterpret_cast<const uint8_t*>(&request),
                                    sizeof(request) - sizeof(request.tag), key, request.tag);
                            memset(key, 0, sizeof(key));
                            if (!signedRequest) {
                                txState = RADIOLIB_ERR_UNKNOWN;
                            } else {
                                const GatewayRadioProfileState previousProfileState = gatewayRadioProfile;
                                gatewayRadioProfile.request = request;
                                gatewayRadioProfile.awaitingAck = true;
                                gatewayRadioProfile.phase = 1;
                                snprintf(gatewayRadioProfile.status, sizeof(gatewayRadioProfile.status), "%s", "unconfirmed");
                                if (!saveGatewayConfig()) {
                                    gatewayRadioProfile = previousProfileState;
                                    txState = RADIOLIB_ERR_UNKNOWN;
                                    GW_LOG_ERROR("Unable to durably record radio transition request; request withheld.");
                                } else {
                                    txState = radio.startTransmit(reinterpret_cast<const uint8_t*>(&request), sizeof(request));
                                    if (txState != RADIOLIB_ERR_NONE) {
                                        gatewayRadioProfile.awaitingAck = false;
                                        gatewayRadioProfile.phase = 0;
                                        snprintf(gatewayRadioProfile.status, sizeof(gatewayRadioProfile.status), "%s", "queued");
                                        saveGatewayConfig();
                                    }
                                }
                            }
                        } else {
                            txState = radio.startTransmit((uint8_t*)&txConfig, sizeof(txConfig)); // transmit sensor config
                        }
                        if (txState == RADIOLIB_ERR_NONE) {
                            if (profileCanDeliver) {
                                const uint32_t sensorSilenceMs = LoRaDutyCycle::requiredSilenceMs(
                                    radio.getTimeOnAir(RADIO_PROFILE_TELEMETRY_FULL_SIZE));
                                gatewayProfileAckDeadlineMs = millis() + sensorSilenceMs + 30000UL;
                            }
                            if (xSemaphoreTake(radioSemaphore, pdMS_TO_TICKS(5000)) == pdTRUE) {
                                gatewayBurstMeasuredUs += micros() - configTxStartedUs;
                                if (profileCanDeliver) {
                                    if (gatewayRadioProfile.commandId[0]) {
                                        publishConfigResult(targetMacStr, gatewayRadioProfile.commandId, "unconfirmed");
                                    }
                                    GW_LOG_INFO("Radio profile request sent to Dev Mode Sensor %s: SF%u BW%.1fkHz CR4/%u.",
                                        targetMacStr, gatewayRadioProfile.requested.spreadingFactor,
                                        RADIO_PROFILE_BANDWIDTHS_KHZ[gatewayRadioProfile.requested.bandwidthIndex],
                                        gatewayRadioProfile.requested.codingRate);
                                } else {
                                    GW_LOG_INFO("Config sent to %s: sleep=%u, devMode=%u, txPower=%ddBm, cpu=%uMHz, timeOffset=%lu",
                                        targetMacStr, txConfig.sleepInterval, txConfig.isDevMode, txConfig.txPower,
                                        txConfig.cpuFrequencyMhz, (unsigned long)txConfig.timeOffset);
                                }
                            } else {
                                GW_LOG_ERROR(profileCanDeliver ? "Radio profile request TX timeout" : "Config TX timeout");
                            }
                        } else {
                            GW_LOG_ERROR(profileCanDeliver ? "Radio profile request startTransmit failed, code %d" : "Config startTransmit failed, code %d", txState);
                        }
                        finishGatewayTxBurst();
                    } else if (gatewayBurstMeasuredUs > 0) {
                        finishGatewayTxBurst();
                    } else {
                        gatewayBurstNonCompliant = false;
                    }
                } else if (sendResponse) {
                    GW_LOG_INFO("Config pending for %s: config=%u, timeSync=%u; TX deferred until duty-cycle cooldown ends.",
                                targetMacStr, sensorNeedsConfig, timeSyncPending);
                } else {
                    GW_LOG_INFO("No config or time update pending.");
                }
                // --- END OF TIME-CRITICAL SECTION ---
                // Clear any lingering semaphore from the TX operations
                xSemaphoreTake(radioSemaphore, 0);

                // Put radio back into receive mode immediately to not miss the next packet
                radio.startReceive();

                // --- NON-CRITICAL SECTION: Logging, Display, and MQTT ---
                // Now that the LoRa transaction is complete, we can perform slower tasks.

                // Both compact operation-mode and full development-mode packets
                // contain the core telemetry fields and must be published.
                    GW_LOG_DEBUG("Sensor payload has data");
                    GW_LOG_DEBUG("Reconstructing MAC Adress");
                    char macStr[18];
                    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", 
                            rxPayload.mac[0], rxPayload.mac[1], rxPayload.mac[2],
                            rxPayload.mac[3], rxPayload.mac[4], rxPayload.mac[5]);
                    sensorMac = String(macStr);
                    GW_LOG_DEBUG("Normalising telemetry values for debug");
                    sensorTemp = rxPayload.temperature / 100.0f;
                    sensorVcc = rxPayload.battVoltage / 1000.0f;
                    sensorBatt = rxPayload.battPercent;
                    msgCount = rxPayload.msgCount;
                    sensorIsDevMode = rxPayload.isDevMode;
                    sensorNeedsTimeSync = (rxPayload.needsTimeSync & 1) != 0;
                    sensorSleepInterval = rxPayload.sleepInterval;
                    SensorPresence* presence = updateSensorPresence(macStr, sensorSleepInterval,
                                                                    packetReceivedMs, rxPayload.bootNonce);
                    if (presence) {
                        presence->temperatureC = sensorTemp;
                        presence->batteryVoltage = sensorVcc;
                        presence->batteryPercent = sensorBatt;
                        presence->messageCount = msgCount;
                        presence->developmentMode = sensorIsDevMode != 0;
                        presence->needsTimeSync = sensorNeedsTimeSync;
                        presence->reportedTxPower = rxPayload.txPower;
                        presence->reportedCpuFrequencyMhz = rxPayload.cpuFrequencyMhz;
                        presence->lastRssi = (int16_t)receivedRssi;
                        presence->lastSnr = (int8_t)receivedSnr;
                        presence->linkObserved = true;
                        presence->linkSignalValid = isfinite(receivedRssi) && isfinite(receivedSnr);
                        if (presence->linkSignalValid) {
                            presence->uplinkRssi = (int16_t)receivedRssi;
                            presence->uplinkSnr = receivedSnr;
                        }
                        presence->lastObservedUs = receivedAtUs;
                        presence->statusPending = true;
                        presence->debugTelemetryAvailable = rxPayload.isDevMode == 1;
                        if (presence->debugTelemetryAvailable) {
                            presence->debugCurrentMa = rxPayload.battCurrent;
                            presence->debugPowerMw = rxPayload.battPower;
                            presence->debugCpuTemp = rxPayload.cpuTemp;
                            presence->debugFreeRam = rxPayload.freeRam;
                            presence->debugLastSnr = rxPayload.lastSNR;
                            presence->debugLastRssi = rxPayload.lastRSSI;
                        }
                    }
                    if (rxPayload.isDevMode == 1){
                        GW_LOG_DEBUG("DEV Mode -> CPU Temp: %dC | RAM: %uKB | I: %dmA | Pow: %dmW | TX: %ddBm | LastCfg SNR: %ddB | LastCfg RSSI: %ddBm",
                                     rxPayload.cpuTemp, rxPayload.freeRam, rxPayload.battCurrent, rxPayload.battPower,
                                     rxPayload.txPower, rxPayload.lastSNR, rxPayload.lastRSSI);
                    }        

                    // A Gateway command remains pending until a later telemetry
                    // payload reports every desired configuration value.
                    if (presence) {
                        const DesiredSensorConfig confirmedDesired = desiredSensorConfig(presence);
                        if (presence->pendingModeChange &&
                            (rxPayload.isDevMode != 0) == confirmedDesired.developmentMode) {
                            presence->pendingModeChange = false;
                            if (!saveGatewayConfig()) GW_LOG_WARN("Sensor mode confirmation is held in RAM only.");
                        }
                        const bool configurationConfirmed = telemetryConfirmsConfig(rxPayload, confirmedDesired, presence);
                        if (configurationConfirmed && presence->configurationPending) {
                            presence->configurationPending = false;
                            presence->pendingCpuChange = false;
                            if (!saveGatewayConfig()) {
                                GW_LOG_WARN("Sensor %s configuration confirmation is held in RAM only.", macStr);
                            } else {
                                GW_LOG_INFO("Sensor %s confirmed %s configuration in telemetry.", macStr,
                                            presence->configurationOrigin[0] ? presence->configurationOrigin : "unknown-origin");
                            }
                        } else if (!configurationConfirmed && !presence->configurationPending) {
                            presence->configurationPending = true;
                            if (!saveGatewayConfig()) {
                                GW_LOG_WARN("Sensor %s pending configuration is held in RAM only.", macStr);
                            }
                        }

                        // A non-empty command ID with pending=false means the
                        // confirming payload has been observed and the final MQTT
                        // result still needs publication.
                        if (!presence->configurationPending && presence->pendingCommandId[0] && mqttClient.connected()) {
                            char confirmedCommandId[sizeof(presence->pendingCommandId)];
                            snprintf(confirmedCommandId, sizeof(confirmedCommandId), "%s", presence->pendingCommandId);
                            if (publishConfigResult(macStr, confirmedCommandId, "applied")) {
                                presence->pendingCommandId[0] = '\0';
                                if (!saveGatewayConfig()) {
                                    GW_LOG_WARN("Applied result for %s cleared in RAM only.", macStr);
                                }
                            }
                        }
                    }
                    GW_LOG_INFO("Radio Received packet! MAC: %s | Msg: %u", macStr, msgCount);
                    GW_LOG_INFO("Temp: %.1fC | VCC: %.2fV | Batt: %d%% | Sleep: %lu | DevMode: %u | NeedsTimeSync: %s",
                                sensorTemp, sensorVcc, sensorBatt, (unsigned long)sensorSleepInterval, sensorIsDevMode,
                                sensorNeedsTimeSync ? "yes" : "no");
                    
                    // compose telemetry json and publish to mqtt broker                    
                    if (mqttClient.connected()) {
                        static StaticJsonDocument<2048> docCore;
                        jsonBuild(TelemetryCoreJsonView{rxPayload}, docCore, &sensorMac);
                        populateSensorRadioSnapshot(docCore.createNestedObject("sensorRadio"), presence, false);
                        populateGatewayRadioSnapshot(docCore.createNestedObject("gatewayRadio"));
                        String telemetryDataTopic = mqttTopic(&sensorMac, "core");
                        if (docCore.overflowed()) {
                            GW_LOG_ERROR("Core telemetry radio snapshot exceeded JSON capacity.");
                        } else if (mqttPublish(telemetryDataTopic.c_str(), docCore)) {
                            GW_LOG_DEBUG("Sensor telemetry data published: %s : %s", telemetryDataTopic.c_str(), docCore.as<String>().c_str());
                        }

                        if (presence) {
                            publishSensorStatus(*presence);
                        }

                        if (rxPayload.isDevMode == 1){
                            static StaticJsonDocument<2048> docDev;
                            jsonBuild(TelemetryDevJsonView{rxPayload}, docDev, &sensorMac);
                            populateSensorRadioSnapshot(docDev.createNestedObject("sensorRadio"), presence, true);
                            populateGatewayRadioSnapshot(docDev.createNestedObject("gatewayRadio"));
                            String devTelemetryDataTopic = mqttTopic(&sensorMac, "dev");
                            if (docDev.overflowed()) {
                                GW_LOG_ERROR("Development telemetry radio snapshot exceeded JSON capacity.");
                            } else if (mqttPublish(devTelemetryDataTopic.c_str(), docDev)) {
                                GW_LOG_DEBUG("Sensor dev telemetry data published: %s : %s", devTelemetryDataTopic.c_str(), docDev.as<String>().c_str());
                            }
                        }
                    }
                    
                screenNum = 0; // Force switch to data screen on new packet
                lastDisplayUpdate = millis();
                drawMain();

            } else {
                GW_LOG_WARN("Received unknown packet of %d bytes", numBytes);
                xSemaphoreTake(radioSemaphore, 0);
                radio.startReceive();
            }

        } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
            // packet was received, but is malformed
            GW_LOG_ERROR("CRC error!");
            xSemaphoreTake(radioSemaphore, 0);
            radio.startReceive();
        } else {
            // some other error occurred
            GW_LOG_ERROR("Receive failed, code %d", state);
            xSemaphoreTake(radioSemaphore, 0);
            radio.startReceive();
        }

    }

    // A newly queued radio event takes priority over all background work.
    if (uxSemaphoreGetCount(radioSemaphore) > 0) return;

    // Background tasks run only while no radio event is pending.
    maintainSdCard();
    gatewayWebUi.service();

    // Connect WiFi/MQTT only after any pending downlink has been sent.
    static bool wasWifiConnected = false;
    static bool mqttWasConnected = false;
    if (WiFi.isConnected()) {
        if (!wasWifiConnected) {
            GW_LOG_INFO("WiFi connected. Device IP: %s", WiFi.localIP().toString().c_str());
            if (gatewayWebUi.running()) {
                GW_LOG_INFO("Gateway WebUI HTTP service started at http://%s", WiFi.localIP().toString().c_str());
            }
            wasWifiConnected = true;
        }
        if (!mqttClient.connected()) {
            mqttConnect();
        }
        bool mqttJustConnected = mqttClient.connected() && !mqttWasConnected;
        mqttWasConnected = mqttClient.connected();
        mqttClient.loop();
        if (mqttJustConnected && sensorPresence[0].mac[0] != '\0') {
            publishSensorStatus(sensorPresence[0]);
        }
        GatewayStatus status{};
        status.uptime = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
        status.gatewayRssi = (int16_t)radio.getRSSI();
        status.gatewaySnr = radio.getSNR();
        snprintf(status.wifiStatus, sizeof(status.wifiStatus), "%s", getWifiStatusString(WiFi.status()));
        snprintf(status.ip, sizeof(status.ip), "%s", WiFi.localIP().toString().c_str());
        status.freeRam = ESP.getFreeHeap();
        static GatewayStatus lastStatus{};
        static GatewayRelayState lastRelayStatus{};
        static bool hasPublishedStatus = false;
        static unsigned long lastStatusPublishMs = 0;
        static bool lastSdReady = false;
        const bool sdReady = gatewayLogSink.isSdReady() && !gatewayConfigFromNvs;
        // Heap and uptime fluctuate continuously; include them in published status but do
        // not let them independently trigger another MQTT publication.
        // Radio metrics (RSSI, SNR) use dead-bands to ignore minor fluctuations.
        bool statusChanged = !hasPublishedStatus ||
            strcmp(status.wifiStatus, lastStatus.wifiStatus) != 0 ||
            strcmp(status.ip, lastStatus.ip) != 0 ||
            abs((int)status.gatewayRssi - (int)lastStatus.gatewayRssi) > 3 ||
            fabsf(status.gatewaySnr - lastStatus.gatewaySnr) > 1.0f;
        bool statusDue = (millis() - lastStatusPublishMs) >= STATUS_HEARTBEAT_MS;
        bool changeDue = statusChanged && (millis() - lastStatusPublishMs) >= STATUS_MIN_INTERVAL_MS;
        bool relayChanged = gatewayRelayStatusPending || !hasPublishedStatus ||
            gatewayRelay.requestedClosed != lastRelayStatus.requestedClosed ||
            gatewayRelay.feedbackClosed != lastRelayStatus.feedbackClosed ||
            gatewayRelay.failed != lastRelayStatus.failed || gatewayRelay.autoOff != lastRelayStatus.autoOff ||
            gatewayRelay.maxOnSeconds != lastRelayStatus.maxOnSeconds || gatewayRelay.durable != lastRelayStatus.durable;
        if (mqttJustConnected || changeDue || statusDue || relayChanged || sdReady != lastSdReady) {
            static StaticJsonDocument<3072> statusDoc;
            jsonBuild(status, statusDoc);
            statusDoc["requestedClosed"] = gatewayRelay.requestedClosed;
            statusDoc["feedbackClosed"] = gatewayRelay.feedbackClosed;
            statusDoc["failed"] = gatewayRelay.failed;
            statusDoc["autoOff"] = gatewayRelay.autoOff;
            statusDoc["maxOnSeconds"] = gatewayRelay.maxOnSeconds;
            statusDoc["durable"] = gatewayRelay.durable;
            statusDoc["SD"] = sdReady ? "ok" : "failed";
            JsonObject radioStatus = statusDoc.createNestedObject("radio");
            radioStatus["available"] = gatewayRadioAvailable;
            if (gatewayRadioAvailable) {
                radioStatus["activity"] = (int32_t)(gatewayNextTxMs - millis()) > 0 ? "cooldown" : "listening";
                radioStatus["frequencyMhz"] = CONFIG_RADIO_FREQ;
                radioStatus["bandwidthKhz"] = RADIO_PROFILE_BANDWIDTHS_KHZ[gatewayRadioProfile.active.bandwidthIndex];
                radioStatus["spreadingFactor"] = gatewayRadioProfile.active.spreadingFactor;
                radioStatus["codingRate"] = gatewayRadioProfile.active.codingRate;
                if (gatewayRadioOutputPowerApplied) radioStatus["txPowerDbm"] = gatewayAppliedRadioTxPower;
                else radioStatus["txPowerDbm"] = nullptr;
            } else {
                radioStatus["activity"] = nullptr;
                radioStatus["frequencyMhz"] = nullptr;
                radioStatus["bandwidthKhz"] = nullptr;
                radioStatus["spreadingFactor"] = nullptr;
                radioStatus["codingRate"] = nullptr;
                radioStatus["txPowerDbm"] = nullptr;
            }
            JsonObject duty = statusDoc.createNestedObject("duty");
            duty["countedBurstUs"] = rtcGatewayAirtime.countedBurstUs;
            duty["cooldownMs"] = (int32_t)(gatewayNextTxMs - millis()) > 0 ? gatewayNextTxMs - millis() : 0;
            duty["totalBurstCount"] = rtcGatewayAirtime.totalBurstCount;
            duty["nonCompliantBurstCount"] = rtcGatewayAirtime.nonCompliantBurstCount;
            duty["rulePercent"] = 1.0f;
            const float nonCompliantBurstRatePercent = rtcGatewayAirtime.totalBurstCount == 0 ? 0.0f :
                (static_cast<float>(rtcGatewayAirtime.nonCompliantBurstCount) * 100.0f / rtcGatewayAirtime.totalBurstCount);
            duty["nonCompliantBurstRatePercent"] = nonCompliantBurstRatePercent;
            duty["driftFromOnePercent"] = rtcGatewayAirtime.totalBurstCount == 0 ? 0.0f : nonCompliantBurstRatePercent - 1.0f;
            if (gatewayRadioAvailable) {
                duty["minimumSleepOperationSeconds"] = sensorMinimumTotalIntervalSeconds(false);
                duty["minimumSleepDevelopmentSeconds"] = sensorMinimumTotalIntervalSeconds(true);
            } else {
                duty["minimumSleepOperationSeconds"] = nullptr;
                duty["minimumSleepDevelopmentSeconds"] = nullptr;
            }
            populateGatewayRadioSnapshot(statusDoc.createNestedObject("gatewayRadio"));
            JsonObject health = statusDoc.createNestedObject("health");
            health["wifiRssiDbm"] = WiFi.RSSI();
            health["cpuFrequencyMhz"] = getCpuFrequencyMhz();
            const float cpuTemperature = temperatureRead();
            if (isfinite(cpuTemperature)) health["cpuTemperatureC"] = cpuTemperature;
            else health["cpuTemperatureC"] = nullptr;
            const uint32_t totalHeap = ESP.getHeapSize();
            const uint32_t freeHeap = ESP.getFreeHeap();
            health["ramTotalBytes"] = totalHeap;
            health["ramUsedBytes"] = totalHeap >= freeHeap ? totalHeap - freeHeap : 0;
            health["sdReady"] = sdReady;
            health["mqttConnected"] = mqttClient.connected();
            JsonObject network = statusDoc.createNestedObject("network");
            network["wifiSsid"] = gatewayConfig.wifiSsid;
            network["mqttHost"] = gatewayConfig.mqttHost;
            network["mqttPort"] = gatewayConfig.mqttPort;
            String statusTopic = mqttTopic(nullptr, "gatewayStatus");
            if (!statusDoc.overflowed() && mqttPublish(statusTopic.c_str(), statusDoc, true)) {
                lastStatus = status;
                lastRelayStatus = gatewayRelay;
                lastSdReady = sdReady;
                gatewayRelayStatusPending = false;
                hasPublishedStatus = true;
                lastStatusPublishMs = millis();
            }
        }

        for (SensorPresence& presence : sensorPresence) {
            if (!presence.mac[0]) continue;
            const unsigned long offlineAfterMs = (presence.sleepIntervalSeconds * 3UL * 1000UL) + SENSOR_STATUS_GRACE_MS;
            if (presence.online && presence.sleepIntervalSeconds != 0 && millis() - presence.lastPacketMs >= offlineAfterMs) {
                presence.online = false;
                presence.statusPending = true;
            }
            if (mqttClient.connected() && (presence.statusPending ||
                millis() - presence.lastStatusPublishMs >= STATUS_HEARTBEAT_MS)) publishSensorStatus(presence);
        }
    } else {
        mqttWasConnected = false;
        if (wasWifiConnected) {
            GW_LOG_WARN("WiFi %s. Reconnecting..", getWifiStatusString(WiFi.status()));
            wasWifiConnected = false;
        }
        if (millis() - lastWifiReconnectAttempt > WIFI_RECONNECT_DELAY_MS) {
            GW_LOG_WARN("WiFi %s. Reconnecting...", getWifiStatusString(WiFi.status()));
            WiFi.reconnect();
            lastWifiReconnectAttempt = millis();
        }
    }

    static unsigned long lastNtpCheck = 0;
    if (millis() - lastNtpCheck > NTP_SYNC_DELAY_MS) {
        time_t now;
        time(&now);
        if (WiFi.isConnected() && !isClockValid(now)) {
            GW_LOG_WARN("Time invalid, NTP syncing...");
            configureSystemTime();
        }
        lastNtpCheck = millis();
    }

    // Rotate screen every 5 seconds
    if (millis() - lastDisplayUpdate > 5000) {
        screenNum = (screenNum + 1) % 2;
        lastDisplayUpdate = millis();
        drawMain();
    }
}

String mqttTopic(const String* mac, const char* mode) {
    if (!mode) return String(MQTT_TOPIC_PREFIX) + "/error";
    
    switch(mode[0]) { 
    case 'c': { // "core" starts with 'c'
        if (!mac) return String(MQTT_TOPIC_PREFIX) + "/error";
        return String(MQTT_TOPIC_PREFIX) + "/sensor/" + mac->c_str() + "/data";
    }
    case 'd': { // "dev" starts with 'd'  
        if (!mac) return String(MQTT_TOPIC_PREFIX) + "/error";
        return String(MQTT_TOPIC_PREFIX) + "/sensor/" + mac->c_str() + "/telemetry";
    }
    case 's': { // "sensorStatus" starts with 's'
        if (!mac) return String(MQTT_TOPIC_PREFIX) + "/error";
        return String(MQTT_TOPIC_PREFIX) + "/sensor/" + mac->c_str() + "/status";
    }
    case 'g': { // "gatewayStatus" starts with 'g'
        return String(MQTT_TOPIC_PREFIX) + "/gateway/" + gatewayId() + "/status";
    }
    // disabled config topic will be read for updates
    // case 'p': { // "payloadConfig" starts with 'p' (since 'c' is taken by core)
    //     return String(MQTT_TOPIC_PREFIX) + "/config/" + mac->c_str();
    // }
    default:
        return String(MQTT_TOPIC_PREFIX) + "/error";
    }
}

SensorPresence* findSensorPresence(const char* mac) {
    if (!isValidSensorMac(mac) || gatewayConfig.pairedSensorMac.length() != 17 ||
        strcasecmp(gatewayConfig.pairedSensorMac.c_str(), mac) != 0) return nullptr;
    SensorPresence& presence = sensorPresence[0];
    if (!presence.mac[0]) snprintf(presence.mac, sizeof(presence.mac), "%s", mac);
    if (strcasecmp(presence.mac, mac) == 0) return &presence;
    return nullptr;
}

bool isValidSensorMac(const char* mac) {
    if (!mac || strlen(mac) != 17) return false;
    for (uint8_t i = 0; i < 17; i++) {
        if ((i + 1) % 3 == 0) {
            if (mac[i] != ':') return false;
        } else if (!((mac[i] >= '0' && mac[i] <= '9') || (mac[i] >= 'A' && mac[i] <= 'F'))) {
            return false;
        }
    }
    return true;
}

static bool getConfigCommandMac(const char* topic, char* mac) {
    const char prefix[] = MQTT_TOPIC_PREFIX "/sensor/";
    const size_t prefixLength = sizeof(prefix) - 1;
    if (!topic || strncmp(topic, prefix, prefixLength) != 0) return false;
    if (strlen(topic) != prefixLength + 17 + strlen("/config")) return false;
    memcpy(mac, topic + prefixLength, 17);
    mac[17] = '\0';
    return isValidSensorMac(mac) && strcmp(topic + prefixLength + 17, "/config") == 0;
}

static bool isValidCommandId(const char* commandId) {
    if (!commandId || strlen(commandId) != 36) return false;
    for (uint8_t i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (commandId[i] != '-') return false;
        } else if (!isxdigit((unsigned char)commandId[i])) {
            return false;
        }
    }
    return true;
}

static bool parseCommandExpiry(const char* expiresAt, time_t* expiresAtUtc) {
    if (!expiresAt || strlen(expiresAt) != 25 ||
        expiresAt[4] != '-' || expiresAt[7] != '-' || expiresAt[10] != 'T' ||
        expiresAt[13] != ':' || expiresAt[16] != ':' || strcmp(expiresAt + 19, "+00:00") != 0) {
        return false;
    }

    struct tm expires = {};
    if (sscanf(expiresAt, "%4d-%2d-%2dT%2d:%2d:%2d", &expires.tm_year, &expires.tm_mon,
               &expires.tm_mday, &expires.tm_hour, &expires.tm_min, &expires.tm_sec) != 6) {
        return false;
    }
    expires.tm_year -= 1900;
    expires.tm_mon -= 1;

    // Convert the validated UTC calendar value without using mktime(), which
    // applies the Gateway's Europe/Athens timezone, or timegm(), which is not
    // available in this ESP32 toolchain.
    int year = expires.tm_year + 1900;
    unsigned int month = (unsigned int)expires.tm_mon + 1;
    unsigned int day = (unsigned int)expires.tm_mday;
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned int yearOfEra = (unsigned int)(year - era * 400);
    const unsigned int marchMonth = month > 2 ? month - 3 : month + 9;
    const unsigned int dayOfYear = (153 * marchMonth + 2) / 5 + day - 1;
    const unsigned int dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    const int64_t daysSinceEpoch = (int64_t)era * 146097 + dayOfEra - 719468;
    time_t parsed = (time_t)(daysSinceEpoch * 86400 + expires.tm_hour * 3600 +
                             expires.tm_min * 60 + expires.tm_sec);
    if (parsed < (time_t)CUSTOM_EPOCH) return false;

    struct tm verified = {};
    gmtime_r(&parsed, &verified);
    if (verified.tm_year != expires.tm_year || verified.tm_mon != expires.tm_mon ||
        verified.tm_mday != expires.tm_mday || verified.tm_hour != expires.tm_hour ||
        verified.tm_min != expires.tm_min || verified.tm_sec != expires.tm_sec) {
        return false;
    }
    *expiresAtUtc = parsed;
    return true;
}

static bool isDuplicateCommand(const char* commandId) {
    time_t now;
    time(&now);
    const bool trustedNow = isClockValid(now);
    for (const char* processedId : processedCommandIds) {
        const size_t index = (size_t)(processedId - &processedCommandIds[0][0]) / sizeof(processedCommandIds[0]);
        if (trustedNow && processedId[0] != '\0' && processedCommandExpiry[index] != 0 &&
            processedCommandExpiry[index] <= now) {
            processedCommandIds[index][0] = '\0';
            processedCommandExpiry[index] = 0;
            continue;
        }
        if (processedId[0] != '\0' && strcmp(processedId, commandId) == 0) return true;
    }
    return false;
}

static void rememberCommand(const char* commandId, time_t expiresAt = 0) {
    strncpy(processedCommandIds[nextProcessedCommandSlot], commandId,
            sizeof(processedCommandIds[nextProcessedCommandSlot]) - 1);
    processedCommandIds[nextProcessedCommandSlot][sizeof(processedCommandIds[nextProcessedCommandSlot]) - 1] = '\0';
    processedCommandExpiry[nextProcessedCommandSlot] = expiresAt;
    nextProcessedCommandSlot = (nextProcessedCommandSlot + 1) % MAX_PROCESSED_COMMANDS;
}

static bool verifyCommandSignature(const char* signature, const String& canonicalJson) {
    return LoraHomeCrypto::verifyHmacSha256Hex(
        (const uint8_t*)gatewayConfig.controlCommandSecret.c_str(),
        gatewayConfig.controlCommandSecret.length(),
        (const uint8_t*)canonicalJson.c_str(), canonicalJson.length(), signature);
}

bool publishConfigResult(const char* mac, const char* commandId, const char* status) {
    StaticJsonDocument<128> result;
    result["commandId"] = commandId;
    result["status"] = status;
    char payload[128];
    size_t length = serializeJson(result, payload, sizeof(payload));
    String topic = String(MQTT_TOPIC_PREFIX) + "/sensor/" + mac + "/config/result";
    if (length == 0 || !mqttClient.publish(topic.c_str(), payload, false)) return false;
    gatewayWebUi.appendMqttEvent("TX", topic.c_str(), payload);
    return true;
}

static String gatewayId() { return WiFi.macAddress(); }

static void handleMqttRelayCommand(const byte* payload, unsigned int length) {
    DynamicJsonDocument command(640);
    if (length == 0 || length > 384 || deserializeJson(command, payload, length)) return;
    const char* commandId = command["commandId"].as<const char*>();
    const char* expiresAt = command["expiresAt"].as<const char*>();
    const char* signature = command["signature"].as<const char*>();
    JsonObjectConst config = command["config"].as<JsonObjectConst>();
    if (!isValidCommandId(commandId) || !expiresAt || !signature || config.isNull() || command["schemaVersion"] != 1) return;
    const bool hasClosed = config.containsKey("relayClosed");
    const bool hasMax = config.containsKey("relayMaxOnSeconds");
    if ((!hasClosed && !hasMax) || (hasClosed && !config["relayClosed"].is<bool>()) ||
        (hasMax && (!config["relayMaxOnSeconds"].is<int>() || config["relayMaxOnSeconds"].as<int>() < 1 || config["relayMaxOnSeconds"].as<int>() > 7200))) return;
    StaticJsonDocument<256> canonical;
    canonical["commandId"] = commandId;
    JsonObject canonicalConfig = canonical.createNestedObject("config");
    if (hasClosed) canonicalConfig["relayClosed"] = config["relayClosed"].as<bool>();
    if (hasMax) canonicalConfig["relayMaxOnSeconds"] = config["relayMaxOnSeconds"].as<int>();
    canonical["expiresAt"] = expiresAt; canonical["schemaVersion"] = 1;
    String canonicalJson; serializeJson(canonical, canonicalJson);
    if (!verifyCommandSignature(signature, canonicalJson)) return;
    time_t expiry; time_t now; time(&now);
    if (!parseCommandExpiry(expiresAt, &expiry) || !isClockValid(now) || now >= expiry) return;
    if (isDuplicateCommand(commandId)) return;
    GatewayRelayState previous = gatewayRelay;
    const uint8_t previousProcessedSlot = nextProcessedCommandSlot;
    char previousProcessedId[sizeof(processedCommandIds[0])] = {};
    const time_t previousProcessedExpiry = processedCommandExpiry[previousProcessedSlot];
    snprintf(previousProcessedId, sizeof(previousProcessedId), "%s",
             processedCommandIds[previousProcessedSlot]);
    rememberCommand(commandId, expiry);
    if (hasMax) gatewayRelay.maxOnSeconds = config["relayMaxOnSeconds"].as<uint32_t>();
    if (hasClosed) { gatewayRelay.requestedClosed = config["relayClosed"].as<bool>(); gatewayRelay.deadline = gatewayRelay.requestedClosed ? now + gatewayRelay.maxOnSeconds : 0; }
    gatewayRelay.durable = saveGatewayConfig();
    const char* result = "applied";
    if (!gatewayRelay.durable) {
        gatewayRelay = previous;
        snprintf(processedCommandIds[previousProcessedSlot], sizeof(processedCommandIds[previousProcessedSlot]),
                 "%s", previousProcessedId);
        nextProcessedCommandSlot = previousProcessedSlot;
        processedCommandExpiry[previousProcessedSlot] = previousProcessedExpiry;
        result = "rejected";
    }
    else if (hasClosed && !applyGatewayRelay(gatewayRelay.requestedClosed)) {
        if (!gatewayRelay.requestedClosed) {
            gatewayRelay.requestedClosed = true;
            gatewayRelay.deadline = time(nullptr) + gatewayRelay.maxOnSeconds;
            gatewayRelay.durable = saveGatewayConfig();
            if (!gatewayRelay.durable) gatewayRelay.recoveryRequired = true;
            GW_LOG_ERROR("Relay OFF failed; countdown restarted for %lu seconds.",
                         (unsigned long)gatewayRelay.maxOnSeconds);
        }
        result = "failed";
    }
    StaticJsonDocument<160> response; response["commandId"] = commandId; response["status"] = result;
    String resultTopic = String(MQTT_TOPIC_PREFIX) + "/gateway/" + gatewayId() + "/relay/result";
    mqttPublish(resultTopic.c_str(), response, false);
    gatewayRelayStatusPending = true;
}

void handleMqttConfigCommand(const char* topic, const byte* payload, unsigned int length) {
    char mac[18] = {};
    if (!getConfigCommandMac(topic, mac)) {
        GW_LOG_WARN("Rejected MQTT config command with invalid topic");
        return;
    }

    static StaticJsonDocument<640> command;
    command.clear();
    if (length == 0 || length > 384 || deserializeJson(command, payload, length)) {
        GW_LOG_WARN("Rejected MQTT config command for %s: invalid JSON", mac);
        return;
    }

    const char* commandId = command["commandId"].as<const char*>();
    const char* expiresAt = command["expiresAt"].as<const char*>();
    const char* signature = command["signature"].as<const char*>();
    JsonObjectConst config = command["config"].as<JsonObjectConst>();
    if (!isValidCommandId(commandId) || !expiresAt || !signature || config.isNull() ||
        command.size() != 5 || !command.containsKey("schemaVersion") || command["schemaVersion"].as<int>() != 1 ||
        !command.containsKey("commandId") || !command.containsKey("expiresAt") || !command.containsKey("config") ||
        !command.containsKey("signature")) {
        if (isValidCommandId(commandId)) publishConfigResult(mac, commandId, "rejected");
        GW_LOG_WARN("Rejected MQTT config command for %s: invalid schema", mac);
        return;
    }

    bool hasSleep = config.containsKey("defaultSleepSeconds");
    bool hasDevMode = config.containsKey("defaultDevMode");
    bool hasTxPower = config.containsKey("defaultTxPower");
    bool hasCpuFrequency = config.containsKey("defaultCpuFrequencyMhz");
    bool hasProfileSf = config.containsKey("spreadingFactor");
    bool hasProfileBw = config.containsKey("bandwidthKhz");
    bool hasProfileCr = config.containsKey("codingRate");
    bool hasProfilePreset = config.containsKey("radioProfilePreset");
    const bool hasRadioProfile = hasProfileSf || hasProfileBw || hasProfileCr || hasProfilePreset;
    bool hasInheritDefaults = config.containsKey("inheritDefaults");
    if (config.size() == 0 || config.size() != (hasSleep ? 1 : 0) + (hasDevMode ? 1 : 0) +
            (hasTxPower ? 1 : 0) + (hasCpuFrequency ? 1 : 0) + (hasProfileSf ? 1 : 0) +
            (hasProfileBw ? 1 : 0) + (hasProfileCr ? 1 : 0) + (hasProfilePreset ? 1 : 0) + (hasInheritDefaults ? 1 : 0) ||
        (hasProfilePreset && (hasProfileSf || hasProfileBw || hasProfileCr)) ||
        ((hasProfileSf || hasProfileBw || hasProfileCr) && !(hasProfileSf && hasProfileBw && hasProfileCr)) ||
        (hasSleep && !config["defaultSleepSeconds"].is<int>()) ||
        (hasDevMode && !config["defaultDevMode"].is<bool>()) ||
        (hasTxPower && !config["defaultTxPower"].is<int>()) ||
        (hasCpuFrequency && !config["defaultCpuFrequencyMhz"].is<int>()) ||
        (hasInheritDefaults && (!config["inheritDefaults"].is<bool>() || !config["inheritDefaults"].as<bool>())) ||
        (hasProfileSf && !config["spreadingFactor"].is<int>()) ||
        (hasProfileBw && !config["bandwidthKhz"].is<float>() && !config["bandwidthKhz"].is<int>()) ||
        (hasProfileCr && !config["codingRate"].is<int>()) ||
        (hasProfilePreset && !config["radioProfilePreset"].is<const char*>()) ||
        (hasInheritDefaults && (hasSleep || hasDevMode || hasTxPower || hasCpuFrequency || hasRadioProfile))) {
        publishConfigResult(mac, commandId, "rejected");
        GW_LOG_WARN("Rejected MQTT config command %s: invalid config fields", commandId);
        return;
    }

    int sleepSeconds = hasSleep ? config["defaultSleepSeconds"].as<int>() : 0;
    if (hasSleep && (sleepSeconds < 10 || sleepSeconds > UINT8_MAX)) {
        publishConfigResult(mac, commandId, "rejected");
        GW_LOG_WARN("Rejected MQTT config command %s: sleep interval out of range", commandId);
        return;
    }

    SensorPresence* existingSensor = findSensorPresence(mac);
    const bool sensorDevEligible = existingSensor && existingSensor->online && existingSensor->authenticatedProtocol &&
        existingSensor->developmentMode && existingSensor->linkObserved &&
        millis() - existingSensor->lastPacketMs <= existingSensor->sleepIntervalSeconds * 1000UL + SENSOR_STATUS_GRACE_MS;
    const bool inheritChangesMode = hasInheritDefaults && existingSensor &&
        desiredSensorConfig(existingSensor).developmentMode != gatewayConfig.sensorDevMode;
    const bool inheritChangesCpu = hasInheritDefaults && existingSensor && existingSensor->hasCpuFrequencyOverride &&
        existingSensor->cpuFrequencyOverride != gatewayConfig.sensorCpuFrequencyMhz;
    if ((hasDevMode && (hasCpuFrequency || hasRadioProfile)) ||
        (inheritChangesCpu && desiredSensorConfig(existingSensor).developmentMode != gatewayConfig.sensorDevMode) ||
        ((hasCpuFrequency || inheritChangesCpu) && !sensorDevEligible)) {
        publishConfigResult(mac, commandId, "rejected");
        GW_LOG_WARN("Rejected MQTT config %s: CPU/profile changes require separate Development Mode commands.", commandId);
        return;
    }
    bool resultingDevMode = hasInheritDefaults
                                ? gatewayConfig.sensorDevMode
                                : hasDevMode
                                ? config["defaultDevMode"].as<bool>()
                                : ((existingSensor && existingSensor->hasDevModeOverride)
                                       ? existingSensor->devModeOverride
                                       : gatewayConfig.sensorDevMode);
    int resultingInterval = hasInheritDefaults
                                ? gatewayConfig.sensorSleepSeconds
                                : hasSleep
                                ? sleepSeconds
                                : ((existingSensor && existingSensor->hasSleepOverride)
                                       ? existingSensor->sleepOverrideSeconds
                                       : gatewayConfig.sensorSleepSeconds);
    uint32_t minimumInterval = sensorMinimumTotalIntervalSeconds(resultingDevMode);
    if (resultingInterval < (int)minimumInterval) {
        publishConfigResult(mac, commandId, "rejected");
        GW_LOG_WARN("Rejected MQTT config command %s: sleep=%ds minimum=%lus payload=%u.",
                    commandId, resultingInterval, (unsigned long)minimumInterval,
                    resultingDevMode ? TELEMETRY_AUTH_FULL_SIZE : TELEMETRY_AUTH_CORE_SIZE);
        return;
    }

    int txPower = hasTxPower ? config["defaultTxPower"].as<int>() : 0;
    if (hasTxPower && (txPower < GATEWAY_SENSOR_TX_POWER_MIN_DBM || txPower > GATEWAY_SENSOR_TX_POWER_MAX_DBM || !isSupportedSensorTxPower(txPower))) {
        publishConfigResult(mac, commandId, "rejected");
        GW_LOG_WARN("Rejected MQTT config command %s: TX power out of range", commandId);
        return;
    }
    int cpuFrequency = hasCpuFrequency ? config["defaultCpuFrequencyMhz"].as<int>() : 0;
    if (hasCpuFrequency && cpuFrequency != 80 && cpuFrequency != 160 && cpuFrequency != 240) {
        publishConfigResult(mac, commandId, "rejected");
        GW_LOG_WARN("Rejected MQTT config command %s: CPU frequency must be 80, 160, or 240 MHz", commandId);
        return;
    }
    RadioProfile requestedProfile = gatewayRadioProfile.active;
    if (hasRadioProfile) {
        if (hasProfilePreset) {
            const char* presetName = config["radioProfilePreset"];
            if (strcmp(presetName, "fast") == 0) requestedProfile = RADIO_PROFILE_FAST;
            else if (strcmp(presetName, "default") == 0) requestedProfile = RADIO_PROFILE_DEFAULT;
            else if (strcmp(presetName, "range") == 0) requestedProfile = RADIO_PROFILE_RANGE;
            else {
                publishConfigResult(mac, commandId, "rejected");
                GW_LOG_WARN("Rejected MQTT radio profile command %s: unknown preset", commandId);
                return;
            }
        } else {
            const int sf = config["spreadingFactor"].as<int>();
            const int cr = config["codingRate"].as<int>();
            const float bandwidthKhz = config["bandwidthKhz"].as<float>();
            int bandwidthIndex = -1;
            for (size_t i = 0; i < RADIO_PROFILE_BANDWIDTH_COUNT; ++i) {
                if (fabsf(bandwidthKhz - RADIO_PROFILE_BANDWIDTHS_KHZ[i]) < 0.01f) {
                    bandwidthIndex = static_cast<int>(i);
                    break;
                }
            }
            if (sf < 7 || sf > 12 || cr < 5 || cr > 8 || bandwidthIndex < 0) {
                publishConfigResult(mac, commandId, "rejected");
                GW_LOG_WARN("Rejected MQTT radio profile command %s: unsupported SF/BW/CR values", commandId);
                return;
            }
            requestedProfile = {static_cast<uint8_t>(sf), static_cast<uint8_t>(bandwidthIndex), static_cast<uint8_t>(cr)};
        }
        uint32_t coreUs = 0, developmentUs = 0, requestUs = 0, ackUs = 0;
        if (!radioProfileAirtimes(radio, requestedProfile, coreUs, developmentUs, requestUs, ackUs)) {
            publishConfigResult(mac, commandId, "rejected");
            GW_LOG_WARN("Rejected MQTT radio profile command %s: radio or duty-cycle limit failed", commandId);
            return;
        }
        const uint32_t profileMinimum = max(
            LoRaDutyCycle::minimumSleepSeconds(resultingDevMode ? developmentUs : coreUs),
            LoRaDutyCycle::minimumSleepSeconds(ackUs));
        if (hasRadioProfile && resultingInterval < static_cast<int>(profileMinimum)) {
            publishConfigResult(mac, commandId, "rejected");
            GW_LOG_WARN("Rejected MQTT radio profile command %s: sleep=%d minimum=%lu seconds",
                        commandId, resultingInterval, static_cast<unsigned long>(profileMinimum));
            return;
        }
        if (!existingSensor || !existingSensor->online || !existingSensor->authenticatedProtocol ||
            !existingSensor->developmentMode || existingSensor->lastPacketMs == 0 ||
            millis() - existingSensor->lastPacketMs >
                (static_cast<unsigned long>(existingSensor->sleepIntervalSeconds) * 1000UL + SENSOR_STATUS_GRACE_MS) ||
            (hasDevMode && !config["defaultDevMode"].as<bool>()) ||
            strcasecmp(existingSensor->mac, gatewayConfig.pairedSensorMac.c_str()) != 0 ||
            gatewayRadioProfile.pending) {
            publishConfigResult(mac, commandId, "rejected");
            GW_LOG_WARN("Rejected MQTT radio profile command %s: paired Sensor must be online in Dev Mode and no transition may be pending", commandId);
            return;
        }
    }
    DynamicJsonDocument canonical(512);
    canonical["commandId"] = commandId;
    JsonObject canonicalConfig = canonical.createNestedObject("config");
    if (hasProfileBw) canonicalConfig["bandwidthKhz"] = RADIO_PROFILE_BANDWIDTHS_KHZ[requestedProfile.bandwidthIndex];
    if (hasProfileCr) canonicalConfig["codingRate"] = requestedProfile.codingRate;
    if (hasCpuFrequency) canonicalConfig["defaultCpuFrequencyMhz"] = cpuFrequency;
    if (hasDevMode) canonicalConfig["defaultDevMode"] = config["defaultDevMode"].as<bool>();
    if (hasSleep) canonicalConfig["defaultSleepSeconds"] = sleepSeconds;
    if (hasTxPower) canonicalConfig["defaultTxPower"] = txPower;
    if (hasInheritDefaults) canonicalConfig["inheritDefaults"] = true;
    if (hasProfilePreset) canonicalConfig["radioProfilePreset"] = config["radioProfilePreset"].as<const char*>();
    if (hasProfileSf) canonicalConfig["spreadingFactor"] = requestedProfile.spreadingFactor;
    canonical["expiresAt"] = expiresAt;
    canonical["schemaVersion"] = 1;
    String canonicalJson;
    serializeJson(canonical, canonicalJson);
    if (!verifyCommandSignature(signature, canonicalJson)) {
        publishConfigResult(mac, commandId, "rejected");
        GW_LOG_WARN("Rejected MQTT config command %s: invalid signature", commandId);
        return;
    }

    if (isDuplicateCommand(commandId)) {
        publishConfigResult(mac, commandId, "rejected");
        GW_LOG_WARN("Rejected duplicate MQTT config command %s", commandId);
        return;
    }

    time_t expiresAtUtc;
    time_t now;
    time(&now);
    if (!parseCommandExpiry(expiresAt, &expiresAtUtc) || !isClockValid(now)) {
        publishConfigResult(mac, commandId, "rejected");
        GW_LOG_WARN("Rejected MQTT config command %s: cannot validate expiry", commandId);
        return;
    }
    if (now >= expiresAtUtc) {
        const uint8_t previousProcessedSlot = nextProcessedCommandSlot;
        char previousProcessedId[sizeof(processedCommandIds[0])] = {};
        const time_t previousProcessedExpiry = processedCommandExpiry[previousProcessedSlot];
        snprintf(previousProcessedId, sizeof(previousProcessedId), "%s",
                 processedCommandIds[previousProcessedSlot]);
        rememberCommand(commandId, expiresAtUtc);
        if (gatewayLogSink.isSdReady() && !saveGatewayConfig()) {
            snprintf(processedCommandIds[previousProcessedSlot], sizeof(processedCommandIds[previousProcessedSlot]),
                     "%s", previousProcessedId);
            nextProcessedCommandSlot = previousProcessedSlot;
            processedCommandExpiry[previousProcessedSlot] = previousProcessedExpiry;
        }
        publishConfigResult(mac, commandId, "expired");
        GW_LOG_WARN("Expired MQTT config command %s", commandId);
        return;
    }

    SensorPresence* sensor = findSensorPresence(mac);
    if (!sensor) {
        publishConfigResult(mac, commandId, "rejected");
        GW_LOG_WARN("Rejected MQTT config command %s: unknown sensor %s", commandId, mac);
        return;
    }
    const uint32_t queuedCommandBoundary = gatewayWebUi.queuedCommandBoundary();
    SensorPresence previousSensor = *sensor;
    const GatewayRadioProfileState previousProfileState = gatewayRadioProfile;
    char supersededCommandId[sizeof(sensor->pendingCommandId)] = {};
    if (sensor->pendingCommandId[0] && strcmp(sensor->pendingCommandId, commandId) != 0) {
        snprintf(supersededCommandId, sizeof(supersededCommandId), "%s", sensor->pendingCommandId);
    }
    if (hasInheritDefaults) {
        sensor->hasSleepOverride = false;
        sensor->hasDevModeOverride = false;
        sensor->hasTxPowerOverride = false;
        sensor->hasCpuFrequencyOverride = false;
    } else if (hasSleep) {
        sensor->hasSleepOverride = true;
        sensor->sleepOverrideSeconds = (uint8_t)sleepSeconds;
    }
    if (!hasInheritDefaults && hasDevMode) {
        sensor->hasDevModeOverride = true;
        sensor->devModeOverride = config["defaultDevMode"].as<bool>();
    }
    if (!hasInheritDefaults && hasTxPower) {
        sensor->hasTxPowerOverride = true;
        sensor->txPowerOverride = (int8_t)txPower;
    }
    if (!hasInheritDefaults && hasCpuFrequency) {
        sensor->hasCpuFrequencyOverride = true;
        sensor->cpuFrequencyOverride = (uint8_t)cpuFrequency;
    }
    if (hasRadioProfile) {
        gatewayRadioProfile.requested = requestedProfile;
        gatewayRadioProfile.pending = true;
        gatewayRadioProfile.awaitingAck = false;
        gatewayRadioProfile.phase = 0;
        snprintf(gatewayRadioProfile.status, sizeof(gatewayRadioProfile.status), "%s", "queued");
        snprintf(gatewayRadioProfile.commandId, sizeof(gatewayRadioProfile.commandId), "%s", commandId);
    }
    sensor->configurationPending = true;
    sensor->pendingCpuChange = sensor->pendingCpuChange || hasCpuFrequency || inheritChangesCpu;
    sensor->pendingModeChange = sensor->pendingModeChange || hasDevMode || inheritChangesMode;
    snprintf(sensor->configurationOrigin, sizeof(sensor->configurationOrigin), "%s", "MQTT");
    snprintf(sensor->pendingCommandId, sizeof(sensor->pendingCommandId), "%s", commandId);
    const uint8_t previousProcessedSlot = nextProcessedCommandSlot;
    char previousProcessedId[sizeof(processedCommandIds[0])] = {};
    const time_t previousProcessedExpiry = processedCommandExpiry[previousProcessedSlot];
    snprintf(previousProcessedId, sizeof(previousProcessedId), "%s",
             processedCommandIds[previousProcessedSlot]);
    rememberCommand(commandId, expiresAtUtc);
    const bool persisted = saveGatewayConfig();
    if (!persisted && hasRadioProfile) {
        *sensor = previousSensor;
        gatewayRadioProfile = previousProfileState;
        snprintf(processedCommandIds[previousProcessedSlot], sizeof(processedCommandIds[previousProcessedSlot]),
                 "%s", previousProcessedId);
        nextProcessedCommandSlot = previousProcessedSlot;
        processedCommandExpiry[previousProcessedSlot] = previousProcessedExpiry;
        publishConfigResult(mac, commandId, "rejected");
        GW_LOG_WARN("Rejected MQTT config command %s: SD persistence failed", commandId);
        return;
    }
    if (!persisted) GW_LOG_WARN("MQTT config %s is queued in RAM only: SD persistence unavailable.", commandId);
    const uint8_t supersededWebUiCommands = gatewayWebUi.discardOlderSensorConfigs(mac, queuedCommandBoundary);
    if (supersededCommandId[0]) publishConfigResult(mac, supersededCommandId, "superseded");
    publishConfigResult(mac, commandId, "queued");
    if (supersededWebUiCommands) GW_LOG_INFO("MQTT config %s superseded %u waiting WebUI config(s) for %s.",
                                           commandId, supersededWebUiCommands, mac);
    const DesiredSensorConfig queuedDesired = desiredSensorConfig(sensor);
    GW_LOG_INFO("Queued MQTT config %s for %s: sleep=%u, devMode=%u, txPower=%ddBm, cpu=%uMHz; awaiting next telemetry confirmation.",
                commandId, mac, queuedDesired.sleepInterval, queuedDesired.developmentMode,
                queuedDesired.txPower, queuedDesired.cpuFrequencyMhz);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    gatewayWebUi.appendMqttEvent("RX", topic, reinterpret_cast<const char*>(payload), length);
    String localTopic = String(MQTT_TOPIC_PREFIX) + "/gateway/" + gatewayId() + "/relay";
    if (localTopic == topic) handleMqttRelayCommand(payload, length);
    else handleMqttConfigCommand(topic, payload, length);
    GW_LOG_DEBUG("MQTT receive processed; loopTask minimum free stack: %u bytes.",
                 static_cast<unsigned int>(uxTaskGetStackHighWaterMark(nullptr)));
}

SensorPresence* updateSensorPresence(const char* mac, uint32_t sleepIntervalSeconds,
                                     uint32_t receivedAtMs, uint32_t bootNonce) {
    if (!isValidSensorMac(mac) || gatewayConfig.pairedSensorMac.length() != 17 ||
        strcasecmp(gatewayConfig.pairedSensorMac.c_str(), mac) != 0) {
        GW_LOG_WARN("Rejected telemetry from unpaired Sensor %s", mac ? mac : "(null)");
        return nullptr;
    }

    SensorPresence* selected = &sensorPresence[0];
    if (selected->mac[0] == '\0') snprintf(selected->mac, sizeof(selected->mac), "%s", mac);

    selected->lastPacketMs = receivedAtMs;
    selected->lastBootNonce = bootNonce;
    selected->bootNonceKnown = true;
    selected->sleepIntervalSeconds = sleepIntervalSeconds;
    if (!selected->online) selected->onlineSinceMs = receivedAtMs;
    selected->online = true;
    return selected;
}

bool publishSensorStatus(SensorPresence& presence) {
    const unsigned long onlineUptimeSeconds = presence.online ? (millis() - presence.onlineSinceMs) / 1000UL : 0;
    SensorStatus status{presence.online, onlineUptimeSeconds};
    static StaticJsonDocument<1536> statusDoc;
    jsonBuild(status, statusDoc);
    JsonObject radioStatus = statusDoc.createNestedObject("radio");
    radioStatus["available"] = presence.radioProfileKnown;
    if (presence.radioProfileKnown) {
        radioStatus["frequencyMhz"] = CONFIG_RADIO_FREQ;
        radioStatus["bandwidthKhz"] = RADIO_PROFILE_BANDWIDTHS_KHZ[presence.radioProfile.bandwidthIndex];
        radioStatus["spreadingFactor"] = presence.radioProfile.spreadingFactor;
        radioStatus["codingRate"] = presence.radioProfile.codingRate;
    } else {
        radioStatus["frequencyMhz"] = nullptr;
        radioStatus["bandwidthKhz"] = nullptr;
        radioStatus["spreadingFactor"] = nullptr;
        radioStatus["codingRate"] = nullptr;
    }
    JsonObject link = statusDoc.createNestedObject("link");
    if (presence.linkObserved) {
        link["lastSeenSeconds"] = static_cast<uint32_t>((esp_timer_get_time() - presence.lastObservedUs) / 1000000ULL);
    } else {
        link["lastSeenSeconds"] = nullptr;
    }
    if (presence.linkSignalValid) {
        link["rssiDbm"] = presence.uplinkRssi;
        link["snrDb"] = presence.uplinkSnr;
    } else {
        link["rssiDbm"] = nullptr;
        link["snrDb"] = nullptr;
    }
    populateSensorRadioSnapshot(statusDoc.createNestedObject("sensorRadio"), &presence, true);
    String mac = presence.mac;
    String statusTopic = mqttTopic(&mac, "sensorStatus");
    const bool published = !statusDoc.overflowed() && mqttPublish(statusTopic.c_str(), statusDoc, true);
    presence.statusPending = !published;
    if (published) presence.lastStatusPublishMs = millis();
    return published;
}

void addJsonMetadata(JsonDocument& doc, const String* mac) {
    // Always add hardware timestamp. If NTP fails, this makes the failure visible (e.g., 1970 dates)
    static unsigned long lastTimestamp = 0;  // Track when timestamp was last updated
    static char cachedTimeStr[24];
    
    // Cache timestamp for 1 second - fixed: compare elapsed time, not against constant
    if (lastTimestamp == 0 || (millis() - lastTimestamp) > 1000) {
        time_t sysTime = time(nullptr);
        struct tm localTimeInfo;
        localtime_r(&sysTime, &localTimeInfo);
        strftime(cachedTimeStr, sizeof(cachedTimeStr), "%Y-%m-%dT%H:%M:%S", &localTimeInfo);
        lastTimestamp = millis();  // Record when we updated the timestamp
        if (lastTimestamp == 0) lastTimestamp = 1; // Edge case: prevent re-triggering if millis() is exactly 0
    }
    doc["timestamp"] = cachedTimeStr;
    
    // Override mac array with formatted string if provided
    if (mac) {
        doc["mac"] = *mac;
    }
}
  
bool mqttPublish(const char* topic, JsonDocument& doc, bool retained) {
    if (!mqttClient.connected()) {
        static unsigned long lastMqttUnavailableLogMs = 0;
        if (millis() - lastMqttUnavailableLogMs >= 60000) {
            GW_LOG_WARN("MQTT not connected. Cannot publish to %s", topic);
            lastMqttUnavailableLogMs = millis();
        }
        return false;
    }

    const size_t payloadLength = measureJson(doc);
    if (payloadLength == 0) {
        GW_LOG_ERROR("JSON serialization failed for topic: %s", topic);
        return false;
    }

    // Keep the diagnostic serialization buffer out of loopTask's stack. MQTT
    // publication and the bounded WebUI event copy share this serialized payload.
    static char debugPayload[2048];
    const bool buffered = payloadLength < sizeof(debugPayload);
    if (buffered && serializeJson(doc, debugPayload, sizeof(debugPayload)) != payloadLength) {
        GW_LOG_ERROR("JSON serialization failed for topic: %s", topic);
        return false;
    }

    if (!mqttClient.beginPublish(topic, static_cast<unsigned int>(payloadLength), retained)) {
        GW_LOG_ERROR("MQTT publish failed to start for topic: %s", topic);
        return false;
    }

    const size_t bytesWritten = buffered
        ? mqttClient.write(reinterpret_cast<const uint8_t*>(debugPayload), payloadLength)
        : serializeJson(doc, mqttClient);
    const bool publishEnded = mqttClient.endPublish() == 1;
    if (bytesWritten != payloadLength) {
        GW_LOG_ERROR("MQTT payload write failed for topic: %s (%u/%u bytes)", topic,
                     static_cast<unsigned int>(bytesWritten), static_cast<unsigned int>(payloadLength));
        return false;
    }
    if (!publishEnded) {
        GW_LOG_ERROR("MQTT publish failed to finish for topic: %s", topic);
        return false;
    }

    GW_LOG_INFO("MQTT published to topic: %s", topic);
    GW_LOG_DEBUG("Payload streamed: %u bytes", static_cast<unsigned int>(payloadLength));
    if (buffered) {
        gatewayWebUi.appendMqttEvent("TX", topic, debugPayload, payloadLength);
    } else {
        gatewayWebUi.appendMqttEvent("TX", topic, "[payload exceeds diagnostic limit]");
    }
    return true;
}

void mqttConnect() {
    if (gatewayConfig.mqttHost.length() == 0 || gatewayConfig.mqttUser.length() == 0) return;
    if (millis() - lastMqttReconnectAttempt > mqttReconnectDelayMs) {
        GW_LOG_INFO("Attempting MQTT connection...");
        String clientId = "LoraHomeGW-" + String((uint32_t)ESP.getEfuseMac(), HEX);
        
        if (mqttClient.connect(clientId.c_str(), gatewayConfig.mqttUser.c_str(), gatewayConfig.mqttPassword.c_str())) {
            GW_LOG_INFO("MQTT connected!");
            mqttReconnectDelayMs = RECONNECT_DELAY_MS;
            if (!mqttClient.subscribe(MQTT_TOPIC_PREFIX "/sensor/+/config", 1)) {
                GW_LOG_ERROR("MQTT config subscription failed");
            } else {
                GW_LOG_INFO("MQTT config subscription active");
                gatewayWebUi.appendMqttEvent("SUB", MQTT_TOPIC_PREFIX "/sensor/+/config", "QoS 1 subscription active");
            }
            String relayCommandTopic = String(MQTT_TOPIC_PREFIX) + "/gateway/" + gatewayId() + "/relay";
            if (mqttClient.subscribe(relayCommandTopic.c_str(), 1)) {
                gatewayWebUi.appendMqttEvent("SUB", relayCommandTopic.c_str(), "QoS 1 subscription active");
            }
            gatewayRelayStatusPending = true;
        } else {
            GW_LOG_WARN("MQTT connect failed, rc=%d. Trying again later.", mqttClient.state());
            mqttReconnectDelayMs = min(mqttReconnectDelayMs * 2, MAX_MQTT_RECONNECT_DELAY_MS);
        }
        lastMqttReconnectAttempt = millis();
    }
}

void drawMain()
{
    if (disp) {
        disp->clearBuffer();
        disp->drawRFrame(0, 0, 128, 64, 5);
        disp->setFont(u8g2_font_pxplusibmvga8_mr);
        
        if (screenNum == 0) {
            disp->setCursor(5, 15);
            disp->print("MAC:");
            disp->setCursor(35, 15);
            disp->print(sensorMac);
            
            disp->setCursor(5, 30);
            disp->printf("Temp: %.1f C", sensorTemp);
            
            disp->setCursor(5, 45);
            disp->printf("Bat: %.2fV (%d%%)", sensorVcc, sensorBatt);
            
            disp->setCursor(5, 60);
            disp->printf("Msg Cnt: %u", msgCount);
            
        } else {
            disp->setCursor(5, 15);
            disp->print("-- GW Status --");
            
            disp->setCursor(5, 30);
            if (WiFi.isConnected()) {
                disp->print(WiFi.localIP().toString());
            } else {
                disp->print("WiFi: NC");
            }
            
            disp->setCursor(5, 45);
            time_t sysTime;
            time(&sysTime);
            if (isClockValid(sysTime)) {
                struct tm timeinfo;
                localtime_r(&sysTime, &timeinfo);
                char timeStr[20];
                strftime(timeStr, sizeof(timeStr), "%y-%m-%d %H:%M", &timeinfo);
                disp->print(timeStr);
            } else {
                disp->print("Time: Syncing...");
            }
            
            disp->setCursor(5, 60);
            disp->printf("S:%s R:%s", snr.c_str(), rssi.c_str());
        }
        
        disp->sendBuffer();
    }
}

bool saveGatewayConfig() {
    if (!gatewayLogSink.isSdReady() || gatewayConfigFromNvs) return false;

    DynamicJsonDocument document(4096);
    document["wifi"]["ssid"] = gatewayConfig.wifiSsid;
    document["wifi"]["password"] = gatewayConfig.wifiPassword;
    document["mqtt"]["host"] = gatewayConfig.mqttHost;
    document["mqtt"]["port"] = gatewayConfig.mqttPort;
    document["mqtt"]["user"] = gatewayConfig.mqttUser;
    document["mqtt"]["password"] = gatewayConfig.mqttPassword;
    document["sensor"]["defaultSleepSeconds"] = gatewayConfig.sensorSleepSeconds;
    document["sensor"]["defaultDevMode"] = gatewayConfig.sensorDevMode;
    document["sensor"]["defaultTxPower"] = gatewayConfig.sensorTxPower;
    document["sensor"]["defaultCpuFrequencyMhz"] = gatewayConfig.sensorCpuFrequencyMhz;
    document["sensor"]["pairedMac"] = gatewayConfig.pairedSensorMac;
    document["gateway"]["radioTxPower"] = gatewayConfig.radioTxPower;
    document["relay"]["requestedClosed"] = gatewayRelay.requestedClosed;
    document["relay"]["maxOnSeconds"] = gatewayRelay.maxOnSeconds;
    document["relay"]["deadline"] = gatewayRelay.deadline;
    document["control"]["commandSecret"] = gatewayConfig.controlCommandSecret;
    document["webui"]["adminPassword"] = gatewayConfig.webAdminPassword;
    JsonObject savedRadioProfile = document.createNestedObject("radioProfile");
    const RadioProfile profiles[] = {gatewayRadioProfile.active, gatewayRadioProfile.requested};
    const char* names[] = {"active", "requested"};
    for (size_t i = 0; i < 2; ++i) {
        JsonObject profile = savedRadioProfile.createNestedObject(names[i]);
        profile["spreadingFactor"] = profiles[i].spreadingFactor;
        profile["bandwidthKhz"] = RADIO_PROFILE_BANDWIDTHS_KHZ[profiles[i].bandwidthIndex];
        char coding[4] = {'4', '/', static_cast<char>('0' + profiles[i].codingRate), '\0'};
        profile["codingRate"] = String(coding);
    }
    savedRadioProfile["_help"] = "Fixed 868.300 MHz center; occupied edges must remain in 868.0-868.6 MHz. SF 7-12; bandwidth kHz: 7.8, 10.4, 15.6, 20.8, 31.25, 41.7, 62.5, 125, 250, 500; coding rate: 4/5, 4/6, 4/7, 4/8. Match Sensor and Gateway; radio and sleep limits apply.";
    savedRadioProfile["pending"] = gatewayRadioProfile.pending;
    savedRadioProfile["awaitingAck"] = gatewayRadioProfile.awaitingAck;
    savedRadioProfile["phase"] = gatewayRadioProfile.phase;
    savedRadioProfile["status"] = gatewayRadioProfile.status;
    savedRadioProfile["commandId"] = gatewayRadioProfile.commandId;
    JsonArray savedRequest = savedRadioProfile.createNestedArray("request");
    const uint8_t* rawRequest = reinterpret_cast<const uint8_t*>(&gatewayRadioProfile.request);
    for (size_t i = 0; i < sizeof(gatewayRadioProfile.request); ++i) savedRequest.add(rawRequest[i]);
    JsonObject overrides = document.createNestedObject("sensorOverrides");
    for (const SensorPresence& sensor : sensorPresence) {
        if (!sensor.mac[0] || (!sensor.hasSleepOverride && !sensor.hasDevModeOverride &&
            !sensor.hasTxPowerOverride && !sensor.hasCpuFrequencyOverride &&
            !sensor.configurationPending && !sensor.pendingCommandId[0] && !sensor.authenticatedProtocol)) continue;
        JsonObject saved = overrides.createNestedObject(sensor.mac);
        if (sensor.hasSleepOverride) saved["sleepSeconds"] = sensor.sleepOverrideSeconds;
        if (sensor.hasDevModeOverride) saved["developmentMode"] = sensor.devModeOverride;
        if (sensor.hasTxPowerOverride) saved["txPower"] = sensor.txPowerOverride;
        if (sensor.hasCpuFrequencyOverride) saved["cpuFrequencyMhz"] = sensor.cpuFrequencyOverride;
        saved["pending"] = sensor.configurationPending;
        if (sensor.authenticatedProtocol) {
            saved["authenticatedProtocol"] = true;
            saved["lastAuthenticatedBootNonce"] = sensor.lastBootNonce;
            saved["lastAuthenticatedMsgCount"] = sensor.lastAuthenticatedMsgCount;
        }
        if (sensor.pendingCpuChange) {
            saved["pendingCpuChange"] = true;
        }
        if (sensor.pendingModeChange) saved["pendingModeChange"] = true;
        if (sensor.configurationOrigin[0]) saved["origin"] = sensor.configurationOrigin;
        if (sensor.pendingCommandId[0]) saved["pendingCommandId"] = sensor.pendingCommandId;
    }
    JsonArray processedCommands = document.createNestedArray("processedCommandIds");
    for (size_t i = 0; i < MAX_PROCESSED_COMMANDS; ++i) {
        if (processedCommandIds[i][0] == '\0') continue;
        JsonObject savedCommand = processedCommands.createNestedObject();
        savedCommand["id"] = processedCommandIds[i];
        if (processedCommandExpiry[i] != 0) savedCommand["expiresAt"] = processedCommandExpiry[i];
    }

    const char* temporaryPath = "/config.tmp";
    const char* backupPath = "/config.bak";
    if (SD.exists(temporaryPath)) SD.remove(temporaryPath);
    File temporary = SD.open(temporaryPath, FILE_WRITE);
    if (!temporary || serializeJsonPretty(document, temporary) == 0) {
        if (temporary) temporary.close();
        SD.remove(temporaryPath);
        return false;
    }
    temporary.flush();
    temporary.close();

    if (SD.exists(backupPath)) SD.remove(backupPath);
    if (SD.exists("/config.json") && !SD.rename("/config.json", backupPath)) {
        SD.remove(temporaryPath);
        return false;
    }
    if (!SD.rename(temporaryPath, "/config.json")) {
        if (SD.exists(backupPath)) SD.rename(backupPath, "/config.json");
        return false;
    }
    DynamicJsonDocument backupDoc(3072);
    backupDoc["wifi"] = document["wifi"];
    backupDoc["mqtt"] = document["mqtt"];
    backupDoc["sensor"] = document["sensor"];
    backupDoc["gateway"] = document["gateway"];
    backupDoc["relay"] = document["relay"];
    backupDoc["radioProfile"] = document["radioProfile"];
    backupDoc["control"]["commandSecret"] = document["control"]["commandSecret"];
    backupDoc["webui"]["adminPassword"] = document["webui"]["adminPassword"];
    String backupJson;
    Preferences backup;
    if (backupDoc.overflowed() || !serializeJson(backupDoc, backupJson) || !backup.begin("gateway-backup", false)) {
        GW_LOG_ERROR("Unable to prepare Gateway NVS configuration backup.");
    } else {
        if (backup.getString("config", "") != backupJson && backup.putString("config", backupJson) != backupJson.length())
            GW_LOG_ERROR("Unable to save Gateway NVS configuration backup.");
        backup.end();
    }
    return true;
}

void handleWebUiCommand(const GatewayWebCommand& command) {
        if (command.hasGatewayTxPower && !command.hasWifiSsid && !command.hasWifiPassword &&
            !command.hasMqttHost && !command.hasMqttPort && !command.hasMqttUser &&
            !command.hasMqttPassword && !command.hasDefaultSleep &&
            !command.hasDefaultDevelopmentMode && !command.hasDefaultTxPower &&
            !command.hasDefaultCpuFrequency && !command.hasPairedSensorMac) {
            const int8_t previousTxPower = gatewayConfig.radioTxPower;
            if (!gatewayRadioAvailable || radio.setOutputPower(command.gatewayTxPower) != RADIOLIB_ERR_NONE) {
                GW_LOG_WARN("Rejected WebUI Gateway TX power; radio did not apply the value.");
                gatewayWebUi.updateOperationResult(command.operationId, "rejected", "Gateway radio TX power could not be applied.");
                return;
            }
            gatewayConfig.radioTxPower = command.gatewayTxPower;
            gatewayRadioOutputPowerApplied = true;
            gatewayAppliedRadioTxPower = command.gatewayTxPower;
            if (!saveGatewayConfig()) {
                gatewayConfig.radioTxPower = previousTxPower;
                radio.setOutputPower(previousTxPower);
                gatewayAppliedRadioTxPower = previousTxPower;
                GW_LOG_WARN("Rejected WebUI Gateway TX power; SD persistence failed.");
                gatewayWebUi.updateOperationResult(command.operationId, "rejected", "Gateway radio TX power could not be saved.");
                return;
            }
            GW_LOG_INFO("Applied independent WebUI Gateway TX power: %ddBm.", command.gatewayTxPower);
            gatewayWebUi.updateOperationResult(command.operationId, "succeeded", "Gateway radio TX power saved and applied.");
            return;
        }
        if (command.type == GatewayWebCommandType::RelayConfig) {
            GatewayRelayState previous = gatewayRelay;
            if (command.hasRelayMaxOn) gatewayRelay.maxOnSeconds = command.relayMaxOnSeconds;
            if (command.hasRelayClosed) {
                gatewayRelay.requestedClosed = command.relayClosed;
                gatewayRelay.deadline = command.relayClosed ? static_cast<uint32_t>(time(nullptr)) + gatewayRelay.maxOnSeconds : 0;
            }
            gatewayRelay.durable = saveGatewayConfig();
            if (!gatewayRelay.durable) {
                gatewayRelay = previous;
                GW_LOG_WARN("Rejected WebUI relay command: SD persistence failed.");
                gatewayWebUi.updateOperationResult(command.operationId, "rejected", "Relay settings could not be saved.");
                gatewayRelayStatusPending = true;
            } else if (command.hasRelayClosed && !applyGatewayRelay(gatewayRelay.requestedClosed)) {
                gatewayRelay.failed = true;
                if (!gatewayRelay.requestedClosed) {
                    gatewayRelay.requestedClosed = true;
                    gatewayRelay.deadline = time(nullptr) + gatewayRelay.maxOnSeconds;
                    gatewayRelay.durable = saveGatewayConfig();
                    if (!gatewayRelay.durable) gatewayRelay.recoveryRequired = true;
                    GW_LOG_ERROR("Relay OFF failed; countdown restarted for %lu seconds.",
                                 (unsigned long)gatewayRelay.maxOnSeconds);
                }
                GW_LOG_ERROR("WebUI relay actuation failed; feedback did not reach the requested state.");
                gatewayWebUi.updateOperationResult(command.operationId, "rejected", "Relay did not reach the requested state.");
                gatewayRelayStatusPending = true;
            } else {
                gatewayWebUi.updateOperationResult(command.operationId, "succeeded", "Relay settings saved.");
                GW_LOG_INFO("Applied WebUI relay policy: requested=%u maxOnSeconds=%lu remainingSeconds=%lu durable=%u.",
                            (unsigned)gatewayRelay.requestedClosed,
                            (unsigned long)gatewayRelay.maxOnSeconds,
                            (unsigned long)(gatewayRelay.requestedClosed && gatewayRelay.deadline > time(nullptr)
                                                ? gatewayRelay.deadline - time(nullptr)
                                                : 0),
                            (unsigned)gatewayRelay.durable);
                lastRelayCountdownLog = gatewayRelay.requestedClosed ? time(nullptr) : 0;
                gatewayRelayStatusPending = true;
            }
        } else if (command.type == GatewayWebCommandType::SensorConfig) {
            SensorPresence* sensor = findSensorPresence(command.sensorMac);
            if (!isValidSensorMac(command.sensorMac) || !sensor) {
                GW_LOG_WARN("Rejected WebUI sensor config: unknown sensor %s", command.sensorMac);
            } else {
                const bool resultingDevMode = command.inheritDefaults
                                                  ? gatewayConfig.sensorDevMode
                                                  : command.hasDevelopmentMode
                                                  ? command.developmentMode
                                                  : (sensor->hasDevModeOverride ? sensor->devModeOverride : gatewayConfig.sensorDevMode);
                const uint32_t resultingSleep = command.inheritDefaults
                                                    ? gatewayConfig.sensorSleepSeconds
                                                    : command.hasSleep
                                                    ? command.sleepSeconds
                                                    : (sensor->hasSleepOverride ? sensor->sleepOverrideSeconds : gatewayConfig.sensorSleepSeconds);
                const uint32_t minimumSleep = sensorMinimumTotalIntervalSeconds(resultingDevMode);
                uint32_t profileCoreUs = 0, profileDevelopmentUs = 0, profileRequestUs = 0, profileAckUs = 0;
                const bool profileIsValid = !command.hasRadioProfile ||
                    (radioProfileAirtimes(radio, command.radioProfile, profileCoreUs, profileDevelopmentUs,
                                          profileRequestUs, profileAckUs) &&
                     sensor->online && sensor->authenticatedProtocol && sensor->developmentMode &&
                     resultingDevMode && !gatewayRadioProfile.pending &&
                     gatewayConfig.pairedSensorMac.length() == 17 &&
                     strcasecmp(sensor->mac, gatewayConfig.pairedSensorMac.c_str()) == 0 &&
                     resultingSleep >= max(
                         LoRaDutyCycle::minimumSleepSeconds(
                             resultingDevMode ? profileDevelopmentUs : profileCoreUs),
                         LoRaDutyCycle::minimumSleepSeconds(profileAckUs)));
                const bool devEligible = sensor->online && sensor->authenticatedProtocol && sensor->developmentMode &&
                    sensor->linkObserved && millis() - sensor->lastPacketMs <=
                        sensor->sleepIntervalSeconds * 1000UL + SENSOR_STATUS_GRACE_MS;
                const bool inheritChangesMode = command.inheritDefaults &&
                    desiredSensorConfig(sensor).developmentMode != gatewayConfig.sensorDevMode;
                const bool inheritChangesCpu = command.inheritDefaults && sensor->hasCpuFrequencyOverride &&
                    sensor->cpuFrequencyOverride != gatewayConfig.sensorCpuFrequencyMhz;
                if ((command.hasDevelopmentMode && (command.hasCpuFrequency || command.hasRadioProfile)) ||
                    (inheritChangesCpu && desiredSensorConfig(sensor).developmentMode != gatewayConfig.sensorDevMode) ||
                    ((command.hasCpuFrequency || inheritChangesCpu) && !devEligible)) {
                    gatewayWebUi.updateOperationResult(command.operationId, "rejected", "CPU/profile changes require separate Development Mode commands.");
                } else if (!profileIsValid) {
                    GW_LOG_WARN("Rejected WebUI radio profile for %s: paired Sensor must be online in Dev Mode, with a valid profile and no transition pending.", command.sensorMac);
                    gatewayWebUi.updateOperationResult(command.operationId, "rejected", "Radio profile requires paired Sensor Dev Mode and a valid, idle transition state.");
                } else if (resultingSleep < minimumSleep) {
                    GW_LOG_WARN("Rejected WebUI sensor config for %s: sleep=%lus minimum=%lus.",
                                command.sensorMac, (unsigned long)resultingSleep, (unsigned long)minimumSleep);
                } else if (command.hasCpuFrequency && command.cpuFrequencyMhz != 80 &&
                           command.cpuFrequencyMhz != 160 && command.cpuFrequencyMhz != 240) {
                    GW_LOG_WARN("Rejected WebUI sensor config for %s: invalid CPU frequency", command.sensorMac);
                } else {
                    SensorPresence previousSensor = *sensor;
                    const GatewayRadioProfileState previousProfileState = gatewayRadioProfile;
                    char supersededCommandId[sizeof(sensor->pendingCommandId)] = {};
                    if (sensor->pendingCommandId[0]) {
                        snprintf(supersededCommandId, sizeof(supersededCommandId), "%s", sensor->pendingCommandId);
                    }
                    if (command.inheritDefaults) {
                        sensor->hasSleepOverride = false;
                        sensor->hasDevModeOverride = false;
                        sensor->hasTxPowerOverride = false;
                        sensor->hasCpuFrequencyOverride = false;
                    } else {
                        if (command.hasSleep) { sensor->hasSleepOverride = true; sensor->sleepOverrideSeconds = command.sleepSeconds; }
                        if (command.hasDevelopmentMode) { sensor->hasDevModeOverride = true; sensor->devModeOverride = command.developmentMode; }
                        if (command.hasTxPower) { sensor->hasTxPowerOverride = true; sensor->txPowerOverride = command.txPower; }
                        if (command.hasCpuFrequency) { sensor->hasCpuFrequencyOverride = true; sensor->cpuFrequencyOverride = command.cpuFrequencyMhz; }
                    }
                    if (command.hasRadioProfile) {
                        gatewayRadioProfile.requested = command.radioProfile;
                        gatewayRadioProfile.pending = true;
                        gatewayRadioProfile.awaitingAck = false;
                        gatewayRadioProfile.phase = 0;
                        snprintf(gatewayRadioProfile.status, sizeof(gatewayRadioProfile.status), "%s", "queued");
                        gatewayRadioProfile.commandId[0] = '\0';
                    }
                    sensor->configurationPending = true;
                    sensor->pendingCpuChange = sensor->pendingCpuChange || command.hasCpuFrequency || inheritChangesCpu;
                    sensor->pendingModeChange = sensor->pendingModeChange || command.hasDevelopmentMode || inheritChangesMode;
                    snprintf(sensor->configurationOrigin, sizeof(sensor->configurationOrigin), "%s", command.inheritDefaults ? "Defaults" : "UI");
                    sensor->pendingCommandId[0] = '\0';
                    if (!saveGatewayConfig()) {
                        *sensor = previousSensor;
                        gatewayRadioProfile = previousProfileState;
                        GW_LOG_WARN("Rejected WebUI sensor config for %s: SD persistence failed.", command.sensorMac);
                        gatewayWebUi.updateOperationResult(command.operationId, "rejected", "Gateway could not persist the requested Sensor configuration.");
                    } else {
                        if (supersededCommandId[0]) publishConfigResult(command.sensorMac, supersededCommandId, "superseded");
                        const DesiredSensorConfig queuedDesired = desiredSensorConfig(sensor);
                        GW_LOG_INFO("Persisted desired WebUI config for %s: sleep=%u, devMode=%u, txPower=%ddBm, cpu=%uMHz; awaiting next telemetry confirmation.",
                                    command.sensorMac, queuedDesired.sleepInterval, queuedDesired.developmentMode,
                                    queuedDesired.txPower, queuedDesired.cpuFrequencyMhz);
                        if (command.hasRadioProfile) {
                            GW_LOG_INFO("Radio profile request queued for %s: SF%u BW%.1fkHz CR4/%u.", command.sensorMac,
                                        command.radioProfile.spreadingFactor,
                                        RADIO_PROFILE_BANDWIDTHS_KHZ[command.radioProfile.bandwidthIndex],
                                        command.radioProfile.codingRate);
                        }
                        gatewayWebUi.updateOperationResult(command.operationId, "succeeded",
                            command.hasRadioProfile ? "Radio profile queued; awaiting Sensor ACK and new-PHY telemetry." : "Sensor configuration queued.");
                    }
                }
            }
        } else if (command.type == GatewayWebCommandType::ChangePassword) {
            const String previousPassword = gatewayConfig.webAdminPassword;
            if (!gatewayWebUi.prepareAdminPassword(command.newPassword)) {
                GW_LOG_WARN("WebUI administrator password verifier could not be prepared.");
                gatewayWebUi.updateOperationResult(command.operationId, "rejected", "Administrator password verifier could not be prepared.");
            } else {
                gatewayConfig.webAdminPassword = command.newPassword;
                if (!saveGatewayConfig()) {
                    gatewayConfig.webAdminPassword = previousPassword;
                    gatewayWebUi.discardPreparedAdminPassword();
                    GW_LOG_WARN("WebUI administrator password update could not be persisted.");
                    gatewayWebUi.updateOperationResult(command.operationId, "rejected", "Administrator password could not be saved.");
                } else {
                    bool verifierPersisted = false;
                    if (!gatewayWebUi.commitPreparedAdminPassword(verifierPersisted)) {
                        gatewayConfig.webAdminPassword = previousPassword;
                        const bool restored = saveGatewayConfig();
                        GW_LOG_ERROR("Prepared WebUI password verifier was unavailable after SD commit; config rollback %s.",
                                     restored ? "succeeded" : "failed");
                        gatewayWebUi.updateOperationResult(command.operationId, "rejected", "Administrator password activation failed.");
                    } else if (!verifierPersisted) {
                        GW_LOG_WARN("WebUI verifier cache could not be saved; SD configuration remains authoritative.");
                        GW_LOG_INFO("WebUI administrator password updated in SD configuration.");
                        gatewayWebUi.updateOperationResult(command.operationId, "succeeded", "Administrator password updated.");
                    } else {
                        GW_LOG_INFO("WebUI administrator password updated in SD configuration.");
                        gatewayWebUi.updateOperationResult(command.operationId, "succeeded", "Administrator password updated.");
                    }
                }
            }
        } else if (command.type == GatewayWebCommandType::GatewayConfig) {
            const bool changesDefaultCpu = command.hasDefaultCpuFrequency && command.defaultCpuFrequencyMhz != gatewayConfig.sensorCpuFrequencyMhz;
            const bool changesDefaultMode = command.hasDefaultDevelopmentMode && command.defaultDevelopmentMode != gatewayConfig.sensorDevMode;
            const SensorPresence& paired = sensorPresence[0];
            if ((command.hasDefaultCpuFrequency && command.hasDefaultDevelopmentMode) ||
                (changesDefaultCpu && (changesDefaultMode || !paired.online || !paired.authenticatedProtocol ||
                !paired.developmentMode || !paired.linkObserved ||
                millis() - paired.lastPacketMs > paired.sleepIntervalSeconds * 1000UL + SENSOR_STATUS_GRACE_MS))) {
                gatewayWebUi.updateOperationResult(command.operationId, "rejected", "Change Sensor CPU only while online in Development Mode, separately from mode.");
                return;
            }
            GatewayConfig previous = gatewayConfig;
            SensorPresence previousSensorPresence[MAX_TRACKED_SENSORS];
            DesiredSensorConfig previousDesired[MAX_TRACKED_SENSORS];
            for (size_t i = 0; i < MAX_TRACKED_SENSORS; ++i) {
                previousSensorPresence[i] = sensorPresence[i];
                previousDesired[i] = desiredSensorConfig(&sensorPresence[i]);
            }
            if (command.hasWifiSsid) gatewayConfig.wifiSsid = command.wifiSsid;
            if (command.hasWifiPassword) gatewayConfig.wifiPassword = command.wifiPassword;
            if (command.hasMqttHost) gatewayConfig.mqttHost = command.mqttHost;
            if (command.hasMqttPort) gatewayConfig.mqttPort = command.mqttPort;
            if (command.hasMqttUser) gatewayConfig.mqttUser = command.mqttUser;
            if (command.hasMqttPassword) gatewayConfig.mqttPassword = command.mqttPassword;
            if (command.hasCommandSecret) gatewayConfig.controlCommandSecret = command.commandSecret;
            if (command.hasDefaultSleep) gatewayConfig.sensorSleepSeconds = command.defaultSleepSeconds;
            if (command.hasDefaultDevelopmentMode) gatewayConfig.sensorDevMode = command.defaultDevelopmentMode;
            if (command.hasDefaultTxPower) gatewayConfig.sensorTxPower = command.defaultTxPower;
            if (command.hasGatewayTxPower) {
                if (!gatewayRadioAvailable || radio.setOutputPower(command.gatewayTxPower) != RADIOLIB_ERR_NONE) {
                    gatewayConfig = previous;
                    GW_LOG_WARN("Rejected WebUI Gateway TX power; radio did not apply the value.");
                    gatewayWebUi.updateOperationResult(command.operationId, "rejected", "Gateway radio TX power could not be applied.");
                    return;
                }
                gatewayConfig.radioTxPower = command.gatewayTxPower;
                gatewayRadioOutputPowerApplied = true;
                gatewayAppliedRadioTxPower = command.gatewayTxPower;
            }
            if (command.hasDefaultCpuFrequency) gatewayConfig.sensorCpuFrequencyMhz = command.defaultCpuFrequencyMhz;
            if (command.hasPairedSensorMac) {
                if (command.pairedSensorMac[0] && !isValidSensorMac(command.pairedSensorMac)) {
                    gatewayConfig = previous;
                    GW_LOG_WARN("Rejected WebUI pairing: invalid Sensor MAC.");
                    gatewayWebUi.updateOperationResult(command.operationId, "rejected", "Invalid paired Sensor MAC.");
                    return;
                }
                gatewayConfig.pairedSensorMac = command.pairedSensorMac;
                if (sensorPresence[0].mac[0] && strcasecmp(sensorPresence[0].mac, command.pairedSensorMac) != 0) {
                    memset(&sensorPresence[0], 0, sizeof(sensorPresence[0]));
                }
            }
            for (size_t i = 0; i < MAX_TRACKED_SENSORS; ++i) {
                if (!sensorPresence[i].mac[0]) continue;
                const DesiredSensorConfig updatedDesired = desiredSensorConfig(&sensorPresence[i]);
                if (previousDesired[i].sleepInterval != updatedDesired.sleepInterval ||
                    previousDesired[i].developmentMode != updatedDesired.developmentMode ||
                    previousDesired[i].txPower != updatedDesired.txPower ||
                    previousDesired[i].cpuFrequencyMhz != updatedDesired.cpuFrequencyMhz) {
                    sensorPresence[i].configurationPending = true;
                    if (previousDesired[i].cpuFrequencyMhz != updatedDesired.cpuFrequencyMhz) {
                        sensorPresence[i].pendingCpuChange = true;
                    }
                    if (previousDesired[i].developmentMode != updatedDesired.developmentMode)
                        sensorPresence[i].pendingModeChange = true;
                }
            }
            const uint32_t minimumSleep = sensorMinimumTotalIntervalSeconds(gatewayConfig.sensorDevMode);
            bool belowMinimum = gatewayConfig.sensorSleepSeconds < minimumSleep;
            for (size_t i = 0; i < MAX_TRACKED_SENSORS && !belowMinimum; ++i) {
                if (!sensorPresence[i].mac[0]) continue;
                const DesiredSensorConfig desired = desiredSensorConfig(&sensorPresence[i]);
                belowMinimum = desired.sleepInterval < sensorMinimumTotalIntervalSeconds(desired.developmentMode);
            }
            if (belowMinimum || !saveGatewayConfig()) {
                gatewayConfig = previous;
                if (command.hasGatewayTxPower && gatewayRadioAvailable) {
                    radio.setOutputPower(previous.radioTxPower);
                    gatewayRadioOutputPowerApplied = true;
                    gatewayAppliedRadioTxPower = previous.radioTxPower;
                }
                for (size_t i = 0; i < MAX_TRACKED_SENSORS; ++i) sensorPresence[i] = previousSensorPresence[i];
                GW_LOG_WARN("Rejected WebUI gateway configuration; validation or persistent save failed.");
                gatewayWebUi.updateOperationResult(command.operationId, "rejected", belowMinimum ?
                    "Sensor sleep is below the required duty-cycle minimum." : "Gateway configuration could not be saved.");
            } else {
                GW_LOG_INFO("WebUI gateway configuration saved; network settings applied and changed Sensor defaults queued.");
                if (command.hasPairedSensorMac && discoveredSensorMac[0] &&
                    strcasecmp(discoveredSensorMac, gatewayConfig.pairedSensorMac.c_str()) == 0) {
                    discoveredSensorMac[0] = '\0';
                    discoveredSensorAuthenticated = false;
                }
                if (command.hasPairedSensorMac) {
                    GW_LOG_INFO("Sensor pairing: %s; MAC=%s.", gatewayConfig.pairedSensorMac.length() ? "paired" : "unpaired",
                                gatewayConfig.pairedSensorMac.length() ? gatewayConfig.pairedSensorMac.c_str() : "none");
                }
                applyNetworkConfiguration();
                gatewayWebUi.updateOperationResult(command.operationId, "succeeded", "Gateway configuration saved and applied.");
            }
        }
}

void provideWebUiSnapshots(GatewayWebSnapshot& snapshot, GatewayWebConfigSnapshot& config) {
    if (strcasecmp(discoveredSensorMac, gatewayConfig.pairedSensorMac.c_str()) != 0)
        snprintf(snapshot.discoveredSensorMac, sizeof(snapshot.discoveredSensorMac), "%s", discoveredSensorMac);
    snapshot.discoveredSensorAuthenticated = discoveredSensorAuthenticated;
    snapshot.uptimeSeconds = static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
    snapshot.freeHeap = ESP.getFreeHeap();
    snapshot.cpuFrequencyMhz = getCpuFrequencyMhz();
    snprintf(snapshot.wifiStatus, sizeof(snapshot.wifiStatus), "%s", getWifiStatusString(WiFi.status()));
    snprintf(snapshot.ip, sizeof(snapshot.ip), "%s", WiFi.localIP().toString().c_str());
    snapshot.wifiRssi = WiFi.RSSI();
    snapshot.mqttConnected = mqttClient.connected();
    snapshot.sdReady = gatewayLogSink.isSdReady() && !gatewayConfigFromNvs;
    snapshot.radioFrequencyMhz = CONFIG_RADIO_FREQ;
    snapshot.radioBandwidthKhz = RADIO_PROFILE_BANDWIDTHS_KHZ[gatewayRadioProfile.active.bandwidthIndex];
    snapshot.spreadingFactor = gatewayRadioProfile.active.spreadingFactor;
    snapshot.codingRate = gatewayRadioProfile.active.codingRate;
    snapshot.radioAvailable = gatewayRadioAvailable && gatewayRadioOutputPowerApplied;
    snapshot.gatewayTxPower = gatewayAppliedRadioTxPower;
    snapshot.radioProfilePending = gatewayRadioProfile.pending;
    snprintf(snapshot.radioProfileStatus, sizeof(snapshot.radioProfileStatus), "%s", gatewayRadioProfile.status);
    snapshot.lastRadioRssi = (int16_t)radio.getRSSI();
    snapshot.lastRadioSnr = radio.getSNR();
    snapshot.countedBurstUs = rtcGatewayAirtime.countedBurstUs;
    snapshot.gatewayCooldownRemainingMs = (int32_t)(gatewayNextTxMs - millis()) > 0 ? gatewayNextTxMs - millis() : 0;
    snapshot.operationMinimumSleepSeconds = sensorMinimumTotalIntervalSeconds(false);
    snapshot.totalBurstCount = rtcGatewayAirtime.totalBurstCount;
    snapshot.nonCompliantBurstCount = rtcGatewayAirtime.nonCompliantBurstCount;
    snapshot.nonCompliantBurstRatePercent = snapshot.totalBurstCount == 0 ? 0.0f :
        (static_cast<float>(snapshot.nonCompliantBurstCount) * 100.0f / snapshot.totalBurstCount);
    snapshot.driftFromOnePercent = snapshot.totalBurstCount == 0 ? 0.0f : snapshot.nonCompliantBurstRatePercent - 1.0f;
    gatewayRelayFeedback();
    snapshot.relayRequestedClosed = gatewayRelay.requestedClosed;
    snapshot.relayFeedbackClosed = gatewayRelay.feedbackClosed;
    snapshot.relayFailed = gatewayRelay.failed;
    snapshot.relayAutoOff = gatewayRelay.autoOff;
    snapshot.relayDurable = gatewayRelay.durable;
    snapshot.relayMaxOnSeconds = gatewayRelay.maxOnSeconds;
    const time_t relayNow = time(nullptr);
    snapshot.relayRemainingSeconds = gatewayRelay.requestedClosed && gatewayRelay.deadline > relayNow
                                         ? static_cast<uint32_t>(gatewayRelay.deadline - relayNow)
                                         : 0;
    for (size_t presenceIndex = 0; presenceIndex < MAX_TRACKED_SENSORS; ++presenceIndex) {
        const SensorPresence& presence = sensorPresence[presenceIndex];
        if (!presence.mac[0] || snapshot.sensorCount >= WEBUI_MAX_SENSORS) continue;
        GatewayWebSensorSnapshot& sensor = snapshot.sensors[snapshot.sensorCount++];
        snprintf(sensor.mac, sizeof(sensor.mac), "%s", presence.mac);
        sensor.online = presence.online;
        sensor.lastSeenSeconds = (millis() - presence.lastPacketMs) / 1000UL;
        sensor.onlineSeconds = presence.online ? (millis() - presence.onlineSinceMs) / 1000UL : 0;
        sensor.sleepSeconds = presence.sleepIntervalSeconds;
        sensor.messageCount = presence.messageCount;
        sensor.temperatureC = presence.temperatureC;
        sensor.batteryVoltage = presence.batteryVoltage;
        sensor.batteryPercent = presence.batteryPercent;
        sensor.developmentMode = presence.developmentMode;
        sensor.needsTimeSync = presence.needsTimeSync;
        sensor.txPower = presence.reportedTxPower;
        sensor.cpuFrequencyMhz = presence.reportedCpuFrequencyMhz;
        sensor.lastRssi = presence.lastRssi;
        sensor.lastSnr = presence.lastSnr;
        sensor.debugTelemetryAvailable = presence.debugTelemetryAvailable;
        sensor.debugCurrentMa = presence.debugCurrentMa;
        sensor.debugPowerMw = presence.debugPowerMw;
        sensor.debugCpuTemp = presence.debugCpuTemp;
        sensor.debugFreeRam = presence.debugFreeRam;
        sensor.debugLastSnr = presence.debugLastSnr;
        sensor.debugLastRssi = presence.debugLastRssi;
        sensor.radioProfileAvailable = presence.radioProfileKnown;
        sensor.radioProfilePending = gatewayRadioProfile.pending;
        snprintf(sensor.radioProfileStatus, sizeof(sensor.radioProfileStatus), "%s", gatewayRadioProfile.status);
        if (presence.radioProfileKnown) {
            sensor.radioProfileSf = presence.radioProfile.spreadingFactor;
            sensor.radioProfileBandwidthKhz = RADIO_PROFILE_BANDWIDTHS_KHZ[presence.radioProfile.bandwidthIndex];
            sensor.radioProfileCodingRate = presence.radioProfile.codingRate;
        }
        const DesiredSensorConfig desired = desiredSensorConfig(&presence);
        snprintf(sensor.configurationOrigin, sizeof(sensor.configurationOrigin), "%s", presence.configurationOrigin);
        sensor.mqttResultPending = presence.pendingCommandId[0] && !presence.configurationPending;
        sensor.desiredSleepSeconds = desired.sleepInterval;
        sensor.desiredDevelopmentMode = desired.developmentMode;
        sensor.desiredTxPower = desired.txPower;
        sensor.desiredCpuFrequencyMhz = desired.cpuFrequencyMhz;
        sensor.configurationConfirmed = presence.lastPacketMs != 0 && !presence.configurationPending &&
            presence.sleepIntervalSeconds == desired.sleepInterval &&
            presence.developmentMode == desired.developmentMode &&
            presence.reportedTxPower == desired.txPower &&
            (!presence.pendingCpuChange || (presence.developmentMode && presence.reportedCpuFrequencyMhz == desired.cpuFrequencyMhz));
        // The UI pending flag represents an actual persisted Gateway command,
        // not a derived display mismatch.
        sensor.configurationPending = presence.configurationPending;
    }
    snprintf(config.wifiSsid, sizeof(config.wifiSsid), "%s", gatewayConfig.wifiSsid.c_str());
    config.wifiPasswordConfigured = gatewayConfig.wifiPassword.length() > 0;
    snprintf(config.mqttHost, sizeof(config.mqttHost), "%s", gatewayConfig.mqttHost.c_str());
    config.mqttPort = gatewayConfig.mqttPort;
    snprintf(config.mqttUser, sizeof(config.mqttUser), "%s", gatewayConfig.mqttUser.c_str());
    config.mqttPasswordConfigured = gatewayConfig.mqttPassword.length() > 0;
    snprintf(config.pairedSensorMac, sizeof(config.pairedSensorMac), "%s", gatewayConfig.pairedSensorMac.c_str());
    config.defaultSleepSeconds = gatewayConfig.sensorSleepSeconds;
    config.defaultDevMode = gatewayConfig.sensorDevMode;
    config.defaultTxPower = gatewayConfig.sensorTxPower;
    config.gatewayTxPower = gatewayConfig.radioTxPower;
    config.defaultCpuFrequencyMhz = gatewayConfig.sensorCpuFrequencyMhz;
}
