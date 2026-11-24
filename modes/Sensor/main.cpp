#include <RadioLib.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <INA226_WE.h>
#include <sys/time.h>
#include <time.h>
#include <esp_system.h>
#include <esp_crc.h>
#include <Preferences.h>
#include <Elog.h>
#include <LoRaHomeCommon.h>
#include <LoRaDutyCycle.h>
#include <RadioProfileSettings.h>
#include <LoraHomeCrypto.h>
#include <LoRaBoards.h>
#include <secrets.h>

// Pin definitions
#define DS18B20_PIN 4  // GPIO4 for DS18B20

/* GPIO12 is an ESP32 strapping pin: only use the active-low jumper documented here and
 never add an external pull-up that can drive it high during reset. */

#define DEV_MODE_PIN 12 // Pull low to force Development Mode

// Magic word to validate RTC memory integrity
#define RTC_MAGIC_WORD 0xA1B2C3D4
#define RTC_LINK_METRICS_MAGIC 0x4C4D4554

#define SENSOR_CONFIG_VERSION 4

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


// Sensor objects
OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);
INA226_WE ina226 = INA226_WE(0x40); // INA226 at default I2C address 0x40
static constexpr float INA226_SHUNT_OHMS = 0.1f;
static constexpr float INA226_MAX_CURRENT_AMPS = 1.0f;
// Change to -1.0f if the installed shunt direction should be inverted.
static constexpr float INA226_CURRENT_DIRECTION = 1.0f;


// save transmission state between loops
static int transmissionState = RADIOLIB_ERR_NONE;
// FreeRTOS semaphore to replace the busy-wait flag
static SemaphoreHandle_t radioSemaphore = NULL;
RTC_DATA_ATTR static uint16_t counter = 0;
RTC_DATA_ATTR static uint32_t bootNonce = 0;
RTC_DATA_ATTR static bool lowBatteryLockout = false;
RTC_DATA_ATTR static bool reportStateValid = false;
RTC_DATA_ATTR static int16_t lastReportedTemperature = 0;
RTC_DATA_ATTR static uint16_t lastReportedBatteryMv = 0;
RTC_DATA_ATTR static uint8_t unchangedWakeCount = 0;

struct LinkMetrics {
  uint32_t magicWord;
  float lastSNR;
  float lastRSSI;
};

RTC_NOINIT_ATTR LinkMetrics rtcLinkMetrics;
static String payload;
static uint32_t lastTxTime = 0;
static uint32_t lastRxTime = 0;
static uint32_t lastBeaconRxTime = 0;
static uint32_t beaconRxStartedAt = 0;
static bool beaconRxArmed = false;
static const uint32_t DEV_MODE_RELEASE_DEBOUNCE_MS = 100;
static const uint32_t RADIO_INIT_RETRY_INTERVAL_SECONDS = 300;
static const uint32_t LOW_BATTERY_SLEEP_INTERVAL_SECONDS = 3600;
static constexpr float LOW_BATTERY_CUTOFF_VOLTS = 3.2f;
static constexpr float LOW_BATTERY_RECOVERY_VOLTS = 3.4f;
static const uint32_t DEFAULT_CPU_FREQUENCY_MHZ = 80;
static bool i2cInitialized = false;
static bool ina226Initialized = false;
static const uint32_t DS18B20_CONVERSION_TIME_MS = 94;
static uint32_t temperatureConversionStartedAt = 0;
static bool temperatureConversionPending = false;
static const uint32_t ACTIVE_POWER_SAMPLE_INTERVAL_MS = 20;
static constexpr int16_t TEMPERATURE_REPORT_DELTA_CENTIDEGREES = 100;
static constexpr uint16_t BATTERY_REPORT_DELTA_MV = 20;
static constexpr uint8_t MAX_UNCHANGED_WAKES = 2;
static float activePowerSampleSumMw = 0.0f;
static uint16_t activePowerSampleCount = 0;
static float completedActivePowerMw = 0.0f;
static bool avgPowerCalculated = false;
// Transmission details
static String deviceId;
static int screenNum = -1;
static int msgOffset = 0;
// Dev mode flag
static bool devMode = false;
// Manual flag to enable serial output in Operation mode for testing. Requires reflash.
static bool serialEnabled = false; 
static bool sessionNonceValid = false;
static uint32_t lastTelemetryTxCompletedAtMs = 0;
static unsigned long bootTime = 0;
static bool bootTimeSet = false;
static constexpr uint8_t SENSOR_LOG = 0;
static constexpr uint16_t SENSOR_LOG_BUFFER_LINES = 20;

// Sensor Data structure
struct SensorData {
  float temperature;
  float batteryVoltage;
  float batteryCurrent;
  float batteryPower;
  uint8_t batteryPercent;
  float cpuTemp;
  uint32_t freeRam;
  float lastSNR;
  float lastRSSI;
  uint32_t timestamp;
} sensorData;

// Configuration structure
struct Config {
  uint32_t magicWord;
  uint8_t configVersion;
  uint32_t sleepInterval; // seconds
  uint8_t isDevMode; // Remote dev mode flag (0=false, 1=true)
  int8_t txPower; // Gateway-configured transmit power in dBm
  uint8_t cpuFrequencyMhz;
  uint32_t checksum;
} __attribute__((packed)) config = {RTC_MAGIC_WORD, SENSOR_CONFIG_VERSION, 20, 0, CONFIG_RADIO_OUTPUT_POWER,
                                    80, 0};

static constexpr uint32_t SENSOR_RADIO_PROFILE_MAGIC = 0x52505246;
struct __attribute__((packed)) SensorRadioProfileRecord {
    uint32_t magic;
    uint32_t generation;
    uint8_t version;
    uint8_t phase;
    RadioProfile active;
    RadioProfile staged;
    uint32_t sessionNonce;
    uint16_t uplinkCounter;
    uint32_t checksum;
};
static SensorRadioProfileRecord sensorRadioProfile = {
    SENSOR_RADIO_PROFILE_MAGIC, 0, 1, 0, RADIO_PROFILE_DEFAULT, RADIO_PROFILE_DEFAULT, 0, 0, 0
};

// RTC memory for config persistence
RTC_NOINIT_ATTR Config rtcConfig; // NOINIT ensures it survives SW_CPU_RESET (ESP.restart)

#define RTC_AIRTIME_MAGIC 0x4154524F
struct AirtimeState {
  uint32_t magicWord;
    RadioProfile radioProfile;
  uint32_t calculatedOperationUs;
  uint32_t calculatedDevelopmentUs;
  uint32_t measuredUs;
  uint32_t countedUs;
  uint32_t minimumSleepSeconds;
};
RTC_NOINIT_ATTR AirtimeState rtcAirtime;

// Function Prototypes
void readSensors();
bool transmitData();
void enterDeepSleep(uint32_t sleepSeconds = 0);

static bool SessionNonceInit() {
    const esp_reset_reason_t resetReason = esp_reset_reason();
    if (resetReason == ESP_RST_DEEPSLEEP && bootNonce != 0) return true;

    Preferences preferences;
    if (!preferences.begin("lorahome", false)) return false;
    const uint32_t previousSession = preferences.getUInt("session", 0);
    if (previousSession == UINT32_MAX) {
        preferences.end();
        return false;
    }
    const uint32_t nextSession = previousSession + 1;
    if (preferences.putUInt("session", nextSession) != sizeof(nextSession)) {
        preferences.end();
        return false;
    }
    preferences.end();
    bootNonce = nextSession == 0 ? 1 : nextSession;
    counter = 0;
    return true;
}
void wakeCycle();
void checkForUpdates();
bool waitForUpdateBeacon();
bool listenForConfig();
void drawMain();
void flushSerialOutput();
void resetPowerSamples();
void sampleActivePower();
void calculateAvgPwr();
bool waitForRadioEvent(uint32_t timeoutMs);
void convertTemperature();
bool batteryRequiresLockout(float voltage);
void displayOff();
bool handleReleasedDevModePin();
static bool saveSensorRadioProfile();

static bool handleRadioProfileRequest(const RadioProfileRequest& request) {
    const bool effectiveDevMode = digitalRead(DEV_MODE_PIN) == LOW || config.isDevMode != 0;
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    uint8_t authKey[LoraHomeCrypto::TELEMETRY_AUTH_KEY_SIZE] = {};
    const bool authenticated = request.version == RADIO_PROFILE_FRAME_VERSION &&
        request.type == RADIO_PROFILE_REQUEST_TYPE && memcmp(request.sensorMac, mac, sizeof(mac)) == 0 &&
        request.sessionNonce == bootNonce && request.uplinkCounter == counter &&
        LoraHomeCrypto::deriveRadioProfileAuthKey(AES_NETWORK_KEY, authKey) &&
        LoraHomeCrypto::verifyTelemetryTag(reinterpret_cast<const uint8_t*>(&request),
                                           sizeof(request) - sizeof(request.tag), authKey, request.tag);
    memset(authKey, 0, sizeof(authKey));
    if (!effectiveDevMode || !authenticated || sensorRadioProfile.phase != 0 ||
        !radioProfileIsValid(radio, request.profile)) {
        if (serialEnabled) Logger.warning(SENSOR_LOG, "Rejected radio profile request: Dev Mode, authentication, replay, phase, or profile check failed.");
        return false;
    }

    uint32_t coreUs = 0, developmentUs = 0, requestUs = 0, ackUs = 0;
    if (!radioProfileAirtimes(radio, request.profile, coreUs, developmentUs, requestUs, ackUs)) {
        if (serialEnabled) Logger.warning(SENSOR_LOG, "Rejected profile exceeding radio or 255-second sleep limits.");
        return false;
    }

    const SensorRadioProfileRecord previousRecord = sensorRadioProfile;
    const RadioProfile previous = sensorRadioProfile.active;
    int state = radio.setBandwidth(RADIO_PROFILE_BANDWIDTHS_KHZ[request.profile.bandwidthIndex]);
    if (state == RADIOLIB_ERR_NONE) state = radio.setSpreadingFactor(request.profile.spreadingFactor);
    if (state == RADIOLIB_ERR_NONE) state = radio.setCodingRate(request.profile.codingRate);
    const int restoreBw = radio.setBandwidth(RADIO_PROFILE_BANDWIDTHS_KHZ[previous.bandwidthIndex]);
    const int restoreSf = radio.setSpreadingFactor(previous.spreadingFactor);
    const int restoreCr = radio.setCodingRate(previous.codingRate);
    if (state != RADIOLIB_ERR_NONE || restoreBw != RADIOLIB_ERR_NONE ||
        restoreSf != RADIOLIB_ERR_NONE || restoreCr != RADIOLIB_ERR_NONE) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "Radio profile validation or old-PHY restoration failed; ACK withheld.");
        return false;
    }

    sensorRadioProfile.staged = request.profile;
    sensorRadioProfile.phase = 1;
    sensorRadioProfile.sessionNonce = request.sessionNonce;
    sensorRadioProfile.uplinkCounter = request.uplinkCounter;
    if (!saveSensorRadioProfile()) {
        sensorRadioProfile = previousRecord;
        if (serialEnabled) Logger.error(SENSOR_LOG, "Unable to durably stage profile; ACK withheld.");
        return false;
    }
    if (serialEnabled) Logger.notice(SENSOR_LOG, "Radio profile staged: SF%u BW%.1fkHz CR4/%u; transmitting authenticated ACK on old PHY.",
                                     request.profile.spreadingFactor,
                                     RADIO_PROFILE_BANDWIDTHS_KHZ[request.profile.bandwidthIndex],
                                     request.profile.codingRate);

    RadioProfileAck ack{};
    ack.version = RADIO_PROFILE_FRAME_VERSION;
    ack.type = RADIO_PROFILE_ACK_TYPE;
    uint8_t ackInput[3 + sizeof(request) + 2] = {'A', 'C', 'K'};
    memcpy(ackInput + 3, &request, sizeof(request));
    ackInput[3 + sizeof(request)] = ack.version;
    ackInput[4 + sizeof(request)] = ack.type;
    uint8_t profileKey[LoraHomeCrypto::TELEMETRY_AUTH_KEY_SIZE] = {};
    const bool ackReady = LoraHomeCrypto::deriveRadioProfileAuthKey(AES_NETWORK_KEY, profileKey) &&
        LoraHomeCrypto::computeTelemetryTag(ackInput, sizeof(ackInput), profileKey, ack.tag);
    memset(profileKey, 0, sizeof(profileKey));
    memset(ackInput, 0, sizeof(ackInput));
    if (!ackReady) return false;

    const uint32_t requiredSilenceMs = LoRaDutyCycle::requiredSilenceMs(rtcAirtime.countedUs);
    const uint32_t elapsedMs = lastTelemetryTxCompletedAtMs ? millis() - lastTelemetryTxCompletedAtMs : 0;
    if (elapsedMs < requiredSilenceMs) delay(requiredSilenceMs - elapsedMs);
    radio.standby();
    xSemaphoreTake(radioSemaphore, 0);
    state = radio.startTransmit(reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
    if (state != RADIOLIB_ERR_NONE || !waitForRadioEvent(5000)) {
        radio.standby();
        if (serialEnabled) Logger.error(SENSOR_LOG, "Profile ACK TX did not complete; staged profile remains inactive.");
        return false;
    }

    sensorRadioProfile.active = request.profile;
    sensorRadioProfile.phase = 0;
    sensorRadioProfile.sessionNonce = 0;
    sensorRadioProfile.uplinkCounter = 0;
    if (!saveSensorRadioProfile()) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "ACK completed but active profile commit failed; manual recovery required.");
        return false;
    }
    rtcAirtime.measuredUs = 0;
    rtcAirtime.countedUs = 0;
    rtcAirtime.minimumSleepSeconds = 0;
    if (serialEnabled) Logger.notice(SENSOR_LOG, "ACK completed; committed profile SF%u BW%.1fkHz CR4/%u; restarting.",
                                     request.profile.spreadingFactor,
                                     RADIO_PROFILE_BANDWIDTHS_KHZ[request.profile.bandwidthIndex],
                                     request.profile.codingRate);
    ESP.restart();
    return true;
}

uint32_t configChecksum(const Config& candidate) {
    return esp_crc32_le(UINT32_MAX, reinterpret_cast<const uint8_t*>(&candidate), offsetof(Config, checksum));
}

static uint32_t sensorRadioProfileChecksum(const SensorRadioProfileRecord& record) {
    return esp_crc32_le(UINT32_MAX, reinterpret_cast<const uint8_t*>(&record),
                        offsetof(SensorRadioProfileRecord, checksum));
}

static bool sensorRadioProfileRecordValid(const SensorRadioProfileRecord& record) {
    return record.magic == SENSOR_RADIO_PROFILE_MAGIC && record.version == 1 && record.phase <= 1 &&
           radioProfileIsValid(radio, record.active) &&
           (record.phase == 0 || radioProfileIsValid(radio, record.staged)) &&
           record.checksum == sensorRadioProfileChecksum(record);
}

static bool saveSensorRadioProfile();

static bool loadRadioProfile() {
    Preferences preferences;
    if (!preferences.begin("radio-profile", false)) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "Unable to open radio-profile NVS namespace.");
        return false;
    }
    const bool hasSlot0 = preferences.isKey("slot0");
    const bool hasSlot1 = preferences.isKey("slot1");
    const size_t length0 = hasSlot0 ? preferences.getBytesLength("slot0") : 0;
    const size_t length1 = hasSlot1 ? preferences.getBytesLength("slot1") : 0;
    SensorRadioProfileRecord records[2] = {};
    const bool valid0 = length0 == sizeof(records[0]) &&
                        preferences.getBytes("slot0", &records[0], sizeof(records[0])) == sizeof(records[0]) &&
                        sensorRadioProfileRecordValid(records[0]);
    const bool valid1 = length1 == sizeof(records[1]) &&
                        preferences.getBytes("slot1", &records[1], sizeof(records[1])) == sizeof(records[1]) &&
                        sensorRadioProfileRecordValid(records[1]);
    preferences.end();
    if (valid0 || valid1) {
        sensorRadioProfile = !valid1 || (valid0 && records[0].generation > records[1].generation)
                                 ? records[0] : records[1];
        if (serialEnabled && ((hasSlot0 && !valid0) || (hasSlot1 && !valid1))) {
            Logger.warning(SENSOR_LOG, "One radio-profile NVS slot is invalid; recovered the newest valid slot.");
        }
        return true;
    }

    sensorRadioProfile = {SENSOR_RADIO_PROFILE_MAGIC, 0, 1, 0,
                          RADIO_PROFILE_DEFAULT, RADIO_PROFILE_DEFAULT, 0, 0, 0};
    if (!saveSensorRadioProfile()) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "No valid radio-profile slot and unable to restore build default.");
        return false;
    }
    if (serialEnabled && (hasSlot0 || hasSlot1)) {
        Logger.warning(SENSOR_LOG, "No valid radio-profile slot; restored build default.");
    }
    return true;
}

static bool saveSensorRadioProfile() {
    sensorRadioProfile.magic = SENSOR_RADIO_PROFILE_MAGIC;
    sensorRadioProfile.version = 1;
    ++sensorRadioProfile.generation;
    sensorRadioProfile.checksum = sensorRadioProfileChecksum(sensorRadioProfile);
    Preferences preferences;
    if (!preferences.begin("radio-profile", false)) return false;
    const char* slot = (sensorRadioProfile.generation & 1U) ? "slot1" : "slot0";
    const size_t written = preferences.putBytes(slot, &sensorRadioProfile, sizeof(sensorRadioProfile));
    SensorRadioProfileRecord verify = {};
    const bool committed = written == sizeof(sensorRadioProfile) &&
                           preferences.getBytes(slot, &verify, sizeof(verify)) == sizeof(verify) &&
                           memcmp(&verify, &sensorRadioProfile, sizeof(verify)) == 0 &&
                           sensorRadioProfileRecordValid(verify);
    preferences.end();
    return committed;
}

bool validateRtcConfig(const Config& candidate) {
    return candidate.magicWord == RTC_MAGIC_WORD &&
           candidate.configVersion == SENSOR_CONFIG_VERSION &&
           candidate.sleepInterval >= 10 && candidate.sleepInterval <= UINT8_MAX &&
           candidate.isDevMode <= 1 &&
#if defined(T3_V1_6_SX1276)
           candidate.txPower >= 2 && candidate.txPower <= 15 &&
#endif
           (candidate.cpuFrequencyMhz == 80 || candidate.cpuFrequencyMhz == 160 || candidate.cpuFrequencyMhz == 240) &&
           candidate.checksum == configChecksum(candidate);
}

void updateRtcConfig() {
    config.checksum = configChecksum(config);
    rtcConfig = config;
}

// Check if current time reflects true NTP sync (not just boot epoch + drift)
bool isClockValid(time_t currentTime) {
    if (!bootTimeSet) {
        bootTime = millis();
        bootTimeSet = true;
    }

    unsigned long uptimeSeconds = (millis() - bootTime) / 1000;
    bool valid = isClockValidSince(currentTime, uptimeSeconds);
    if (serialEnabled) {
        Logger.debug(SENSOR_LOG, valid ? "Clock is valid" : "Clock is not valid - likely still at boot epoch");
    }
    return valid;
}

void flushSerialOutput() {
    if (!serialEnabled) {
        return;
    }

    delay(20);
    Serial.flush();
}


// Callback function for LoRa hardware interrupts.
// IMPORTANT: This function MUST be 'void' type and MUST NOT have any arguments!
#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void setFlag(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (radioSemaphore != NULL) {
        xSemaphoreGiveFromISR(radioSemaphore, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// Standard Arduino setup function. Initializes hardware, sensors, and radio.
// Used in: Both Operation and Dev modes
void setup()
{
    // Initialize the FreeRTOS semaphore
    radioSemaphore = xSemaphoreCreateBinary();

    if (rtcLinkMetrics.magicWord != RTC_LINK_METRICS_MAGIC) {
        rtcLinkMetrics.magicWord = RTC_LINK_METRICS_MAGIC;
        rtcLinkMetrics.lastSNR = 0;
        rtcLinkMetrics.lastRSSI = 0;
    }

    // Early evaluate devMode to enable serial immediately if needed
    pinMode(DEV_MODE_PIN, INPUT_PULLUP);
    const bool retainedConfig = validateRtcConfig(rtcConfig);
    if (digitalRead(DEV_MODE_PIN) == LOW ||
        (retainedConfig && rtcConfig.isDevMode != 0)) {
        serialEnabled = true;
    }

    if (serialEnabled) {
        Serial.begin(115200);
        Logger.configure(SENSOR_LOG_BUFFER_LINES, true);
        Logger.configureSerial(1);
        Logger.registerSerial(SENSOR_LOG, ELOG_LEVEL_DEBUG, "SENSOR", Serial, ELOG_FLAG_TIME_LONG);
    }
     
    // RTC memory preserves this across deep sleep; cold starts use a durable
    // monotonic session value so AES-CTR streams cannot repeat after power loss.
    sessionNonceValid = SessionNonceInit();
    if (!sessionNonceValid) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "Unable to persist telemetry session nonce; refusing transmission.");
        enterDeepSleep(RADIO_INIT_RETRY_INTERVAL_SECONDS);
        return;
    }
    if (NETWORK_KEY == 0 || !LoraHomeCrypto::isConfiguredAesKey(AES_NETWORK_KEY)) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "LoRa credentials are not provisioned; refusing telemetry.");
        enterDeepSleep(RADIO_INIT_RETRY_INTERVAL_SECONDS);
        return;
    }
    if (!loadRadioProfile()) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "Persisted radio profile is corrupt or unavailable; refusing radio startup.");
        enterDeepSleep(RADIO_INIT_RETRY_INTERVAL_SECONDS);
        return;
    }
    if (sensorRadioProfile.phase == 1 && serialEnabled) {
        Logger.warning(SENSOR_LOG, "A radio profile request was staged before interruption; the active profile is retained and local recovery may be required.");
    }

    // Load only a fully validated RTC record. The CRC detects partial writes and
    // corrupted retained state before it can select a mode.
    if (retainedConfig) config = rtcConfig;
    if (!retainedConfig) {
        config.magicWord = RTC_MAGIC_WORD;
        config.configVersion = SENSOR_CONFIG_VERSION;
        // TO DO - configure defauls centrally
        config.sleepInterval = 60;
        config.isDevMode = 0;
        config.txPower = CONFIG_RADIO_OUTPUT_POWER;
        config.cpuFrequencyMhz = DEFAULT_CPU_FREQUENCY_MHZ;
        updateRtcConfig(); // Initialize RTC memory with defaults on cold boot
        reportStateValid = false;
        if (serialEnabled) {
            Logger.warning(SENSOR_LOG, "invalid rtcConfig, reinitialising to defaults: %u sleep=%u, devMode=%d, txPower=%ddBm, cpu=%uMHz",
                           rtcConfig.magicWord, rtcConfig.sleepInterval, rtcConfig.isDevMode,
                           rtcConfig.txPower, rtcConfig.cpuFrequencyMhz);
        }
    } else{
            if (serialEnabled) {
                Logger.info(SENSOR_LOG, "Boot rtcConfig is valid : %u sleep=%u, devMode=%d, txPower=%ddBm, cpu=%uMHz",
                            rtcConfig.magicWord, rtcConfig.sleepInterval, rtcConfig.isDevMode,
                            rtcConfig.txPower, rtcConfig.cpuFrequencyMhz);
            }
    }



    // Check dev mode (hardware pin OR remote config)
    devMode = (digitalRead(DEV_MODE_PIN) == LOW) || (config.isDevMode != 0);

    if (serialEnabled) {
        const esp_reset_reason_t resetReason = esp_reset_reason();
        const char* resetReasonText = "UNKNOWN";
        switch (resetReason) {
            case ESP_RST_POWERON: resetReasonText = "POWERON"; break;
            case ESP_RST_EXT: resetReasonText = "EXT"; break;
            case ESP_RST_SW: resetReasonText = "SW"; break;
            case ESP_RST_PANIC: resetReasonText = "PANIC"; break;
            case ESP_RST_INT_WDT: resetReasonText = "INT_WDT"; break;
            case ESP_RST_TASK_WDT: resetReasonText = "TASK_WDT"; break;
            case ESP_RST_WDT: resetReasonText = "WDT"; break;
            case ESP_RST_DEEPSLEEP: resetReasonText = "DEEPSLEEP"; break;
            case ESP_RST_BROWNOUT: resetReasonText = "BROWNOUT"; break;
            case ESP_RST_SDIO: resetReasonText = "SDIO"; break;
            default: break;
        }
        Logger.info(SENSOR_LOG, "Startup reset=%s(%d) retainedConfig=%d devPinLow=%d effectiveDevMode=%d",
                    resetReasonText, static_cast<int>(resetReason), retainedConfig,
                    digitalRead(DEV_MODE_PIN) == LOW, devMode);
    }

    if (!setCpuFrequencyMhz(config.cpuFrequencyMhz) && serialEnabled) {
        Logger.error(SENSOR_LOG, "Failed to set CPU frequency to %u MHz.", config.cpuFrequencyMhz);
    }

    if (devMode) {
        i2cInitialized = setupBoards(false, true);
        if (!i2cInitialized && serialEnabled) {
            Logger.error(SENSOR_LOG, "I2C initialization failed; display and INA226 are unavailable.");
        }
        if (serialEnabled) Logger.info(SENSOR_LOG, "Dev/Debug mode");
    } else {
        if (serialEnabled) Logger.info(SENSOR_LOG, "Operation mode");
        // In OP mode, disable all non-essential peripherals for max power saving
        // Disable WiFi and Bluetooth
        WiFi.mode(WIFI_OFF);
        btStop();

        // Initialize I2C only for sensors
#ifdef I2C_SDA
        i2cInitialized = Wire.begin(I2C_SDA, I2C_SCL);
#endif
        if (i2cInitialized) {
            displayOff(); // Keep the OLED controller off without initializing U8g2
        } else if (serialEnabled) {
            Logger.error(SENSOR_LOG, "I2C initialization failed; display and INA226 are unavailable.");
        } else {
            Serial.println("I2C initialization failed; display and INA226 are unavailable.");
        }

        // Initialize SPI for radio
        SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);
        
        // Set radio pins
        pinMode(18, OUTPUT); // RADIO_CS_PIN
        digitalWrite(18, HIGH);
        pinMode(23, OUTPUT); // RADIO_RST_PIN
        digitalWrite(23, HIGH);
        pinMode(32, INPUT); // RADIO_DIO2_PIN
    }
    
    // Initialize LED
    pinMode(BOARD_LED, OUTPUT);
    digitalWrite(BOARD_LED, !LED_ON);

    // Initialize sensors
    sensors.begin();
    sensors.setResolution(9); // 0.5 C steps, displayed with one decimal
    sensors.setWaitForConversion(false);
    convertTemperature(); // Overlap conversion with INA226 and radio initialization
    ina226Initialized = i2cInitialized && ina226.init(); // Initialize INA226
    if (ina226Initialized) {
        ina226.setResistorRange(INA226_SHUNT_OHMS, INA226_MAX_CURRENT_AMPS);
    } else if (serialEnabled) {
        Logger.warning(SENSOR_LOG, "INA226 unavailable; power readings may be invalid.");
    }

    // Avoid paying the radio initialization cost while the battery is locked out.
    if (ina226Initialized) {
        delay(3); // Allow the initial 1.1 ms shunt + bus conversions to complete.
        float earlyBatteryVoltage = ina226.getBusVoltage_V();
        if (batteryRequiresLockout(earlyBatteryVoltage)) {
            if (serialEnabled) {
                Logger.warning(SENSOR_LOG, "Battery %.2fV below safe operating threshold; skipping radio initialization.",
                               earlyBatteryVoltage);
            }
            enterDeepSleep(LOW_BATTERY_SLEEP_INTERVAL_SECONDS);
            return;
        }
    }

    // Radio setup 
    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) {
        if (serialEnabled) {
            Logger.error(SENSOR_LOG, "Radio init failed: %d", state);
        }
        enterDeepSleep(RADIO_INIT_RETRY_INTERVAL_SECONDS);
        return;
    }
    // Set radio parameters from the active profile
    state = radio.setFrequency(CONFIG_RADIO_FREQ);
    if (state != RADIOLIB_ERR_NONE) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "Radio frequency setup failed: %d", state);
        enterDeepSleep(RADIO_INIT_RETRY_INTERVAL_SECONDS);
        return;
    }
    state = radio.setBandwidth(RADIO_PROFILE_BANDWIDTHS_KHZ[sensorRadioProfile.active.bandwidthIndex]);
    if (state != RADIOLIB_ERR_NONE) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "Radio bandwidth setup failed: %d", state);
        enterDeepSleep(RADIO_INIT_RETRY_INTERVAL_SECONDS);
        return;
    }
    state = radio.setSpreadingFactor(sensorRadioProfile.active.spreadingFactor);
    if (state != RADIOLIB_ERR_NONE) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "Radio spreading-factor setup failed: %d", state);
        enterDeepSleep(RADIO_INIT_RETRY_INTERVAL_SECONDS);
        return;
    }
    state = radio.setCodingRate(sensorRadioProfile.active.codingRate);
    if (state != RADIOLIB_ERR_NONE) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "Radio coding-rate setup failed: %d", state);
        enterDeepSleep(RADIO_INIT_RETRY_INTERVAL_SECONDS);
        return;
    }
    state = radio.setSyncWord(0xAB);
    if (state != RADIOLIB_ERR_NONE) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "Radio sync-word setup failed: %d", state);
        enterDeepSleep(RADIO_INIT_RETRY_INTERVAL_SECONDS);
        return;
    }
    state = radio.setOutputPower(config.txPower);
    if (state != RADIOLIB_ERR_NONE) {
        if (serialEnabled) {
            Logger.warning(SENSOR_LOG, "Configured TX power %ddBm rejected (state %d); using firmware default %ddBm",
                           config.txPower, state, CONFIG_RADIO_OUTPUT_POWER);
        }
        config.txPower = CONFIG_RADIO_OUTPUT_POWER;
        updateRtcConfig();
        state = radio.setOutputPower(config.txPower);
        if (state != RADIOLIB_ERR_NONE) {
            if (serialEnabled) Logger.error(SENSOR_LOG, "Radio default TX power setup failed: %d", state);
            enterDeepSleep(RADIO_INIT_RETRY_INTERVAL_SECONDS);
            return;
        }
    }
    state = radio.setCRC(true);           // Enable CRC for data integrity
    if (state != RADIOLIB_ERR_NONE) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "Radio CRC setup failed: %d", state);
        enterDeepSleep(RADIO_INIT_RETRY_INTERVAL_SECONDS);
        return;
    }
    if (serialEnabled) Logger.notice(SENSOR_LOG, "Boot radio configuration: %.2fMHz SF%u BW%.2fkHz CR4/%u TX%ddBm sync=0xAB CRC=on.",
                                     (double)CONFIG_RADIO_FREQ,
                                     sensorRadioProfile.active.spreadingFactor,
                                     RADIO_PROFILE_BANDWIDTHS_KHZ[sensorRadioProfile.active.bandwidthIndex],
                                     sensorRadioProfile.active.codingRate, config.txPower);

    uint32_t operationAirtimeUs = 0, developmentAirtimeUs = 0, profileRequestUs = 0, profileAckUs = 0;
    if (!radioProfileAirtimes(radio, sensorRadioProfile.active, operationAirtimeUs,
                              developmentAirtimeUs, profileRequestUs, profileAckUs) ||
        operationAirtimeUs != radio.getTimeOnAir(RADIO_PROFILE_OPERATION_SIZE) ||
        developmentAirtimeUs != radio.getTimeOnAir(RADIO_PROFILE_TELEMETRY_FULL_SIZE) ||
        profileRequestUs != radio.getTimeOnAir(sizeof(RadioProfileRequest)) ||
        profileAckUs != radio.getTimeOnAir(sizeof(RadioProfileAck))) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "Stateless RadioLib profile airtime does not match the active radio; refusing transmission.");
        enterDeepSleep(RADIO_INIT_RETRY_INTERVAL_SECONDS);
        return;
    }
    if (rtcAirtime.magicWord != RTC_AIRTIME_MAGIC ||
        memcmp(&rtcAirtime.radioProfile, &sensorRadioProfile.active, sizeof(sensorRadioProfile.active)) != 0) {
        memset(&rtcAirtime, 0, sizeof(rtcAirtime));
        rtcAirtime.magicWord = RTC_AIRTIME_MAGIC;
        rtcAirtime.radioProfile = sensorRadioProfile.active;
    }
    rtcAirtime.radioProfile = sensorRadioProfile.active;
    rtcAirtime.calculatedOperationUs = operationAirtimeUs;
    rtcAirtime.calculatedDevelopmentUs = developmentAirtimeUs;
    const uint32_t profileAckAirtimeUs = radio.getTimeOnAir(sizeof(RadioProfileAck));
    const uint32_t activeCalculatedUs = devMode ? developmentAirtimeUs : operationAirtimeUs;
    rtcAirtime.countedUs = max(rtcAirtime.measuredUs, activeCalculatedUs);
    rtcAirtime.minimumSleepSeconds = max(LoRaDutyCycle::minimumSleepSeconds(rtcAirtime.countedUs),
                                         LoRaDutyCycle::minimumSleepSeconds(profileAckAirtimeUs));
    if (serialEnabled) {
        Logger.info(SENSOR_LOG, "Duty cycle: payload=%u calculated=%luus measured=%luus minimum sleep=%lus",
                    devMode ? RADIO_PROFILE_TELEMETRY_FULL_SIZE : RADIO_PROFILE_OPERATION_SIZE,
                    (unsigned long)activeCalculatedUs, (unsigned long)rtcAirtime.measuredUs,
                    (unsigned long)rtcAirtime.minimumSleepSeconds);
    }
    if (config.sleepInterval < rtcAirtime.minimumSleepSeconds) {
        if (serialEnabled) {
            Logger.warning(SENSOR_LOG, "Stored sleep %lus is below calculated minimum %lus; adjusted.",
                           (unsigned long)config.sleepInterval,
                           (unsigned long)rtcAirtime.minimumSleepSeconds);
        }
        config.sleepInterval = rtcAirtime.minimumSleepSeconds;
        updateRtcConfig();
    }

    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER && config.sleepInterval > 30) {
        const uint32_t rebootCooldownMs = LoRaDutyCycle::requiredSilenceMs(rtcAirtime.countedUs);
        if (serialEnabled) Logger.info(SENSOR_LOG, "Restoring reboot TX cooldown: %lums", (unsigned long)rebootCooldownMs);
        delay(rebootCooldownMs);
    } else if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER && serialEnabled) {
        Logger.notice(SENSOR_LOG, "Reboot cooldown bypassed for configured sleep <=30s.");
    }

    // Set hardware interrupt callbacks for both RX and TX
    radio.setPacketSentAction(setFlag);
    radio.setPacketReceivedAction(setFlag);

    // Display operation mode at startup screen
    #ifdef HAS_DISPLAY
    if (devMode && disp) {  //only in dev mode
        // Unique device identity (use MAC)
        uint64_t mac = ESP.getEfuseMac();
        char idBuf[17];
        sprintf(idBuf, "%08X%08X", (uint32_t)(mac >> 32), (uint32_t)mac);
        deviceId = String(idBuf);

        const char *modeText = "DEV MODE";
        disp->clearBuffer();
        disp->setFont(u8g2_font_pxplusibmvga9_mr);
        int16_t x = (disp->getDisplayWidth() - disp->getUTF8Width(modeText)) / 2;
        disp->drawStr(x, 30, modeText);
        disp->sendBuffer();
        delay(2000);
        disp->clearBuffer();
        disp->sendBuffer();
    }
    #endif

   
   
    wakeCycle();

    
    if (devMode) {
        // In Dev Mode, after the first cycle, continue in loop().
        drawMain(); 
    } else {
         // In Op Mode, after the cycle go to deep sleep
        enterDeepSleep();
    }
}

// Standard Arduino loop function.
// Used ONLY in Dev mode (Simulates the device op lifecycle with serial logging and display updates).
void loop()
{
    if (devMode) {
       
        Logger.debug(SENSOR_LOG, "[DEV] --- Sleep ---");
        Logger.info(SENSOR_LOG, "Entering simulated deep sleep for %lu seconds", (unsigned long)config.sleepInterval);

        if (disp) {
            disp->clearBuffer();
            disp->sendBuffer();
        }
        convertTemperature(); // Complete during the simulated sleep interval
        uint32_t simulatedSleepStartedMs = millis();
        uint32_t simulatedSleepDurationMs = config.sleepInterval * 1000UL;
        while (millis() - simulatedSleepStartedMs < simulatedSleepDurationMs) {
            if (handleReleasedDevModePin()) return;
            uint32_t remainingMs = simulatedSleepDurationMs - (millis() - simulatedSleepStartedMs);
            delay(remainingMs > 50 ? 50 : remainingMs);
        }

        // run the operating cycle
        wakeCycle();

        if (handleReleasedDevModePin()) return;
        
        // display data from the cycle
        Logger.debug(SENSOR_LOG, "[DEV] --- Display ---");
        // Rotate through all 5 screens
        for (int i = 0; i < 5; i++) {
            drawMain();
            uint32_t screenStartedMs = millis();
            while (millis() - screenStartedMs < 2000) {
                if (handleReleasedDevModePin()) return;
                delay(50);
            }
        }
    } else {
        
    }
}

// A low hardware pin forces Development Mode but does not overwrite the remote
// preference retained in RTC memory. Once the pin has been released and the
// retained preference is Operation Mode, restart so all low-power peripherals
// are initialized through the normal Operation Mode setup path.
bool handleReleasedDevModePin() {
    static uint32_t releasedSinceMs = 0;
    static bool restartIssued = false;
    if (!devMode || config.isDevMode != 0 || digitalRead(DEV_MODE_PIN) == LOW) {
        releasedSinceMs = 0;
        restartIssued = false;
        return false;
    }

    if (restartIssued) return true;

    if (releasedSinceMs == 0) {
        releasedSinceMs = millis();
        return false;
    }
    if (millis() - releasedSinceMs < DEV_MODE_RELEASE_DEBOUNCE_MS) return false;

    if (serialEnabled) Logger.notice(SENSOR_LOG, "Development-mode pin released; restarting into Operation Mode.");
    restartIssued = true;
    displayOff();
    flushSerialOutput();
    ESP.restart();
    return true;
}

// Converts a given voltage to an estimated percentage (0-100%) for a typical 18650 Li-Ion battery #6
// Used in: Both Operation and Dev modes
uint8_t getBatteryPercentage(float voltage) {
    int voltage_mv = voltage * 1000;
    // Voltage lookup table for 0%, 10%, 20%, ..., 100%
    const static int table[11] = {
        3000, 3650, 3700, 3740, 3760, 3795,
        3840, 3910, 3980, 4070, 4150
    };
    if (voltage_mv < table[0]) return 0;
    for (int i = 1; i < 11; i++) {
        if (voltage_mv < table[i]) {
            return i * 10 - (10 * (table[i] - voltage_mv)) / (table[i] - table[i - 1]);
        }
    }
    return 100;
}

// Starts a development-mode measurement window for LoRa TX and active RX power.
void resetPowerSamples() {
    activePowerSampleSumMw = 0.0f;
    activePowerSampleCount = 0;
}

// Samples signed battery power. V * mA yields mW.
void sampleActivePower() {
    if (!devMode || !ina226Initialized) {
        return;
    }

    float voltage = ina226.getBusVoltage_V();
    float current = ina226.getCurrent_mA() * INA226_CURRENT_DIRECTION;
    activePowerSampleSumMw += voltage * current;
    activePowerSampleCount++;
}

// Retains the completed average for the next development telemetry packet.
void calculateAvgPwr() {
    if (devMode && activePowerSampleCount > 0) {
        completedActivePowerMw = activePowerSampleSumMw / activePowerSampleCount;
        avgPowerCalculated = true;
    }
}

// Waits for a radio interrupt while periodically sampling power in development mode.
bool waitForRadioEvent(uint32_t timeoutMs) {
    uint32_t startTime = millis();

    while (millis() - startTime < timeoutMs) {
        uint32_t elapsed = millis() - startTime;
        uint32_t remaining = timeoutMs - elapsed;
        uint32_t waitMs = (devMode && ina226Initialized && remaining > ACTIVE_POWER_SAMPLE_INTERVAL_MS)
                            ? ACTIVE_POWER_SAMPLE_INTERVAL_MS
                            : remaining;

        if (xSemaphoreTake(radioSemaphore, pdMS_TO_TICKS(waitMs)) == pdTRUE) {
            sampleActivePower();
            return true;
        }
        sampleActivePower();
    }

    return false;
}

void convertTemperature() {
    sensors.requestTemperatures();
    temperatureConversionStartedAt = millis();
    temperatureConversionPending = true;
}

bool batteryRequiresLockout(float voltage) {
    lowBatteryLockout = lowBatteryLockout
                            ? voltage < LOW_BATTERY_RECOVERY_VOLTS
                            : voltage <= LOW_BATTERY_CUTOFF_VOLTS;
    return lowBatteryLockout;
}

void displayOff() {
    if (!i2cInitialized) return;
    #ifdef HAS_DISPLAY
    if (disp) {
        disp->clearBuffer();
        disp->sendBuffer();
        disp->setPowerSave(1);
    } else {
        // Operation mode does not initialize U8g2. Send the SSD1306 display-off
        // command directly so the controller is not left active between wakes.
        Wire.beginTransmission(DISPLAY_ADDR);
        Wire.write((uint8_t)0x00); // Command stream control byte
        Wire.write((uint8_t)0xAE); // Display OFF (sleep mode)
        uint8_t displayState = Wire.endTransmission();
        if (displayState != 0 && serialEnabled) {
            Logger.warning(SENSOR_LOG, "OLED power-down command failed: %u", displayState);
        }
    }
    #endif
}

// single operational cycle: read, transmit, and check for updates.
// Used in: Both Operation and Dev modes
void wakeCycle() {
    if (serialEnabled) Logger.debug(SENSOR_LOG, "--- Wake, Read Sensors ---");
    readSensors();

    if (ina226Initialized && batteryRequiresLockout(sensorData.batteryVoltage)) {
            if (serialEnabled) {
                Logger.warning(SENSOR_LOG, "Battery %.2fV below safe operating threshold; suppressing radio and sleeping.",
                               sensorData.batteryVoltage);
            }
            enterDeepSleep(LOW_BATTERY_SLEEP_INTERVAL_SECONDS);
            return;
    }

    const int16_t temperature = static_cast<int16_t>(sensorData.temperature * 100);
    const uint16_t batteryMv = static_cast<uint16_t>(sensorData.batteryVoltage * 1000);
    const bool telemetryChanged = !reportStateValid ||
        abs(static_cast<int>(temperature) - lastReportedTemperature) >= TEMPERATURE_REPORT_DELTA_CENTIDEGREES ||
        abs(static_cast<int>(batteryMv) - lastReportedBatteryMv) >= BATTERY_REPORT_DELTA_MV;
    const bool heartbeatDue = unchangedWakeCount >= MAX_UNCHANGED_WAKES;
    const bool timeSyncNeeded = !isClockValid(static_cast<time_t>(sensorData.timestamp));
    const bool txNeeded = devMode || telemetryChanged || heartbeatDue || timeSyncNeeded;
    if (!txNeeded) {
        ++unchangedWakeCount;
        if (serialEnabled) Logger.info(SENSOR_LOG, "Telemetry unchanged; skipping TX/RX (%u/%u).",
                                       unchangedWakeCount, MAX_UNCHANGED_WAKES);
        return;
    }

    resetPowerSamples();
    if (serialEnabled) Logger.debug(SENSOR_LOG, "--- Transmit ---");
    if (transmitData()) {
        reportStateValid = true;
        lastReportedTemperature = temperature;
        lastReportedBatteryMv = batteryMv;
        unchangedWakeCount = 0;
    }
    checkForUpdates();
    calculateAvgPwr();
}   

// Reads data from connected sensors (DS18B20, INA226) and internal ESP32 metrics (CPU temp, RAM).
// Used in: Both Operation and Dev modes
void readSensors()
{   
    // Preserve the most recent valid config-downlink metrics across deep sleep and soft resets.
    sensorData.lastSNR = rtcLinkMetrics.lastSNR;
    sensorData.lastRSSI = rtcLinkMetrics.lastRSSI;
    
    // Read the asynchronous DS18B20 conversion started during initialization or Dev sleep.
    if (!temperatureConversionPending) {
        convertTemperature();
    }
    uint32_t conversionElapsed = millis() - temperatureConversionStartedAt;
    if (conversionElapsed < DS18B20_CONVERSION_TIME_MS) {
        delay(DS18B20_CONVERSION_TIME_MS - conversionElapsed);
    }
    sensorData.temperature = sensors.getTempCByIndex(0);
    temperatureConversionPending = false;

    // Read INA226 data. Power is calculated from signed current so charge and
    // discharge direction is preserved instead of using the unsigned power register.
    if (ina226Initialized) {
        sensorData.batteryVoltage = ina226.getBusVoltage_V();
        sensorData.batteryCurrent = ina226.getCurrent_mA() * INA226_CURRENT_DIRECTION;
        float sessionPowerMw = sensorData.batteryVoltage * sensorData.batteryCurrent;
        sensorData.batteryPower = (devMode && avgPowerCalculated)
                                  ? completedActivePowerMw
                                  : sessionPowerMw;
        avgPowerCalculated = false;
        sensorData.batteryPercent = getBatteryPercentage(sensorData.batteryVoltage);
    } else {
        sensorData.batteryVoltage = 0.0f;
        sensorData.batteryCurrent = 0.0f;
        sensorData.batteryPower = 0.0f;
        sensorData.batteryPercent = 0;
    }

    // Read ESP32 internals
    sensorData.cpuTemp = temperatureRead();
    sensorData.freeRam = ESP.getFreeHeap() / 1024;

    // Get current time (ESP32 RTC survives deep sleep, drift is fixed on config update)
    time_t now;
    time(&now);
    sensorData.timestamp = (uint32_t)now;
    if (serialEnabled) {
        char timeStr[20];
        time_t ts = sensorData.timestamp;
        if (isClockValid(ts)) {
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", gmtime(&ts));
        } else {
            strncpy(timeStr, "UNSYNCED", sizeof(timeStr));
            timeStr[sizeof(timeStr) - 1] = '\0';
        }

        Logger.info(SENSOR_LOG, "T:%.1fC V:%.2fV(%d%%) I:%.1fmA P:%.1fmW | CPU:%.1fC RAM:%uKB | SNR:%.1f RSSI:%.0f | TS:%s",
                    sensorData.temperature, sensorData.batteryVoltage, sensorData.batteryPercent,
                    sensorData.batteryCurrent, sensorData.batteryPower, sensorData.cpuTemp,
                    sensorData.freeRam, sensorData.lastSNR, sensorData.lastRSSI,
                    timeStr);
    }
}

// Constructs the telemetry payload string and transmits it via the LoRa radio.
// Used in: Both Operation and Dev modes
bool transmitData()
{
    beaconRxArmed = false;
    // Prepare binary payload
    TelemetryPayload txPayload;
    esp_efuse_mac_get_default(txPayload.mac);
    txPayload.bootNonce = bootNonce;
    if (counter == UINT16_MAX) {
        if (serialEnabled) Logger.warning(SENSOR_LOG, "Telemetry counter exhausted; restarting to allocate a new session.");
        displayOff();
        flushSerialOutput();
        ESP.restart();
        return false;
    }
    txPayload.msgCount = ++counter;
    txPayload.temperature = (int16_t)(sensorData.temperature * 100); 
    txPayload.battVoltage = (uint16_t)(sensorData.batteryVoltage * 1000);
    txPayload.battPercent = sensorData.batteryPercent;
    txPayload.isDevMode = (uint8_t)devMode;
    txPayload.needsTimeSync = isClockValid((time_t)sensorData.timestamp) ? 0 : 1;
    txPayload.sleepInterval = config.sleepInterval;
    txPayload.txPower = config.txPower;
    size_t legacyTxSize = TELEMETRY_OPERATION_SIZE; // Core payload size

    if (devMode) {
        txPayload.cpuFrequencyMhz = getCpuFrequencyMhz();
        txPayload.battCurrent = (int16_t)sensorData.batteryCurrent;
        txPayload.battPower = (int16_t)sensorData.batteryPower;
        txPayload.freeRam = (uint16_t)sensorData.freeRam;
        txPayload.cpuTemp = (int8_t)sensorData.cpuTemp;
        txPayload.lastSNR = (int8_t)sensorData.lastSNR;
        txPayload.lastRSSI = (int16_t)sensorData.lastRSSI;
        legacyTxSize = sizeof(TelemetryPayload); // 32 bytes full size
    }

    uint8_t legacyBytes[sizeof(TelemetryPayload)] = {};
    memcpy(legacyBytes, &txPayload, legacyTxSize);

    if (serialEnabled) {
        // generate the debug string before encryption so it can still be read on the OLED
        payload = "";
        uint8_t* ptr = legacyBytes;
        for(size_t i = 0; i < legacyTxSize; i++) {
            char buf[3];
            sprintf(buf, "%02X", ptr[i]);
            payload += buf;
        }
    }

    uint8_t txFrame[RADIO_PROFILE_TELEMETRY_FULL_SIZE] = {};
    size_t frameSize = 0;
    if (!LoraHomeCrypto::encodeAuthenticatedProfileTelemetryFrame(legacyBytes, legacyTxSize,
                                                                 sensorRadioProfile.active, AES_NETWORK_KEY,
                                                                 txFrame, sizeof(txFrame), &frameSize)) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "Telemetry encryption/authentication failed; skipping TX.");
        radio.standby();
        return false;
    }

    // Turn LED on during transmission
    digitalWrite(BOARD_LED, LED_ON);

    // Clear any pending semaphore state
    xSemaphoreTake(radioSemaphore, 0);

    // Transmit the raw binary struct directly
    uint32_t txStartedUs = micros();
    transmissionState = radio.startTransmit(txFrame, frameSize);

    if (transmissionState != RADIOLIB_ERR_NONE) {
        if (serialEnabled) Logger.error(SENSOR_LOG, "Failed to start TX (state %d)", transmissionState);
        digitalWrite(BOARD_LED, !LED_ON);
        radio.standby();
        return false;
    }
    sampleActivePower();

    if (serialEnabled) {
        Logger.debug(SENSOR_LOG, "Transmitting authenticated telemetry (%d bytes): %s", frameSize, payload.c_str());
    }

    // Wait for transmission to complete (with 5 second timeout) using FreeRTOS Block state
    bool txCompleted = waitForRadioEvent(5000);
    if (txCompleted) lastTelemetryTxCompletedAtMs = millis();
    if (!txCompleted) {
        //put the radio on standby to save a bit more power
        radio.standby();
        if (serialEnabled) Logger.warning(SENSOR_LOG, "TX timeout!");
    } else {
        // Arm beacon RX before timing/accounting/logging work so the Gateway's
        // post-uplink guard is measured against an active receiver.
        xSemaphoreTake(radioSemaphore, 0);
        const int rxState = radio.startReceive();
        if (rxState == RADIOLIB_ERR_NONE) {
            beaconRxStartedAt = millis();
            beaconRxArmed = true;
        } else if (serialEnabled) {
            Logger.error(SENSOR_LOG, "Failed to arm beacon RX after TX (state %d)", rxState);
        }
    }
    uint32_t calculatedTxUs = radio.getTimeOnAir(frameSize);
    uint32_t measuredTxUs = txCompleted ? micros() - txStartedUs : calculatedTxUs;
    lastTxTime = (measuredTxUs + 999UL) / 1000UL;
    rtcAirtime.measuredUs = measuredTxUs;
    rtcAirtime.countedUs = max(calculatedTxUs, measuredTxUs);
    rtcAirtime.minimumSleepSeconds = LoRaDutyCycle::minimumSleepSeconds(rtcAirtime.countedUs);
    if (config.sleepInterval < rtcAirtime.minimumSleepSeconds) {
        if (serialEnabled) Logger.warning(SENSOR_LOG, "Measured TX raised minimum sleep from %lus to %lus; adjusted.",
                                          (unsigned long)config.sleepInterval,
                                          (unsigned long)rtcAirtime.minimumSleepSeconds);
        config.sleepInterval = rtcAirtime.minimumSleepSeconds;
        updateRtcConfig();
    }


    // Turn LED off after transmission
    digitalWrite(BOARD_LED, !LED_ON);

    if (serialEnabled) {
        Logger.info(SENSOR_LOG, "TX complete: calculated=%luus measured=%luus counted=%luus minimum sleep=%lus",
                    (unsigned long)calculatedTxUs, (unsigned long)measuredTxUs,
                    (unsigned long)rtcAirtime.countedUs,
                    (unsigned long)rtcAirtime.minimumSleepSeconds);
    }
    return txCompleted;
}

// Check for a Gateway update beacon after every telemetry transmission.
// Used in: Both Operation and Dev modes
void checkForUpdates() {
    if (serialEnabled) Logger.info(SENSOR_LOG, "Checking for update beacon after telemetry TX.");
    if (waitForUpdateBeacon() && !listenForConfig()) {
        if (serialEnabled) Logger.warning(SENSOR_LOG, "Failed to receive config after beacon.");
    }
}

// Briefly checks if the gateway has a pending configuration update for this wake cycle.
// Used in: Both Operation and Dev modes
bool waitForUpdateBeacon()
{
    unsigned long startTime = beaconRxStartedAt;
    if (!beaconRxArmed) {
        xSemaphoreTake(radioSemaphore, 0); // Clear semaphore
        radio.startReceive(); // Start non-blocking background reception
        startTime = millis();
    }
    beaconRxArmed = false;
    sampleActivePower();
    // Allow the gateway enough scheduling headroom to switch from RX to TX.
    uint32_t beaconAirtimeMs = ((radio.getTimeOnAir(1) + 999) / 1000) + SENSOR_BEACON_RX_MARGIN_MS;
    while (millis() - startTime < beaconAirtimeMs) {
        uint32_t elapsed = millis() - startTime;
        uint32_t remaining = beaconAirtimeMs > elapsed ? beaconAirtimeMs - elapsed : 0;
        if (remaining == 0) break;

        if (waitForRadioEvent(remaining)) {
            int numBytes = radio.getPacketLength();
            uint8_t rxBuffer[256];
            int state = radio.readData(rxBuffer, numBytes);

            if (state == RADIOLIB_ERR_NONE) {
                if (numBytes == 1 && rxBuffer[0] == CONFIG_UPDATE_PENDING_FLAG) {
                    lastBeaconRxTime = millis() - startTime;
                    if (serialEnabled) {
                        Logger.info(SENSOR_LOG, "Update beacon received after %lu ms (window: %lu ms)", lastBeaconRxTime, beaconAirtimeMs);
                    }
                    return true;
                } else if (serialEnabled) {
                    Logger.warning(SENSOR_LOG, "Received %d bytes (expected 1). Retrying...", numBytes);
                }
            } else if (serialEnabled) {
                Logger.warning(SENSOR_LOG, "RX error: %d. Retrying...", state);
            }
            radio.startReceive(); 
            sampleActivePower();
        }
        if (serialEnabled) {
                    Logger.debug(SENSOR_LOG, "Timeout while trying to find beacon.");
                }
    }
    radio.standby();
    lastBeaconRxTime = millis() - startTime;
    if (serialEnabled) {
        Logger.info(SENSOR_LOG, "No update beacon found. Short RX window: %lu ms (was %lu ms)", lastBeaconRxTime, beaconAirtimeMs);
    }
    return false;
}

// Listens for incoming LoRa configuration packets from the Gateway for 5 seconds.
// Returns true if config was successfully received and applied, false otherwise.
// Used in: Both Operation and Dev modes
bool listenForConfig()
{
    xSemaphoreTake(radioSemaphore, 0); // Clear semaphore
    radio.startReceive(); // Start non-blocking background reception
    sampleActivePower();
    unsigned long startTime = millis();
    bool configReceived = false;
    bool needsRestart = false;

    while (millis() - startTime < 5000) { // Listen for 5 seconds
        uint32_t elapsed = millis() - startTime;
        uint32_t remaining = 5000 > elapsed ? 5000 - elapsed : 0;
        if (remaining == 0) break;

        if (waitForRadioEvent(remaining)) {
            if (radio.getPacketLength() == sizeof(RadioProfileRequest)) {
                RadioProfileRequest request{};
                const int state = radio.readData(reinterpret_cast<uint8_t*>(&request), sizeof(request));
                if (state == RADIOLIB_ERR_NONE && handleRadioProfileRequest(request)) return true;
                radio.startReceive();
                continue;
            }
            if (radio.getPacketLength() == sizeof(ConfigPayload)) {
                ConfigPayload rxConfig;
                int state = radio.readData((uint8_t*)&rxConfig, sizeof(rxConfig));

                if (state == RADIOLIB_ERR_NONE) {
                    float receivedSNR = radio.getSNR();
                    float receivedRSSI = radio.getRSSI();

                    if (serialEnabled) {
                        uint32_t packetToA = radio.getTimeOnAir(sizeof(ConfigPayload)) / 1000; // Returns microseconds, convert to ms
                        Logger.info(SENSOR_LOG, "Received config packet. Packet ToA: %lu ms", packetToA);
                    }

                    // Verify header to ensure it's actually our config packet
                    if (rxConfig.header[0] == 'C' && rxConfig.header[1] == 'F') {

                        if (rxConfig.configVersion != SENSOR_CONFIG_VERSION) {
                            if (serialEnabled) Logger.warning(SENSOR_LOG, "Unsupported config version %u.", rxConfig.configVersion);
                            break;
                        }

                        // Verify this config packet is meant for this specific device
                        uint8_t myMac[6];
                        esp_efuse_mac_get_default(myMac);

                        if (memcmp(rxConfig.targetMac, myMac, 6) != 0) {
                            if (serialEnabled) Logger.warning(SENSOR_LOG, "Config not for this node. Terminating RX.");
                            break; // Not meant for this node, terminate early
                        }

                        // Verify the network marker; ordinary configuration has no cryptographic authentication.
                        if (rxConfig.networkKey != NETWORK_KEY) {
                            if (serialEnabled) Logger.error(SENSOR_LOG, "Invalid network key. Terminating RX.");
                            break; // Invalid key, terminate early
                        }

                        sensorData.lastSNR = receivedSNR;
                        sensorData.lastRSSI = receivedRSSI;
                        rtcLinkMetrics.lastSNR = receivedSNR;
                        rtcLinkMetrics.lastRSSI = receivedRSSI;

#if defined(T3_V1_6_SX1276)
                        if (rxConfig.txPower < 2 || rxConfig.txPower > 15) {
                            if (serialEnabled) Logger.warning(SENSOR_LOG, "Rejected config TX power outside supported range.");
                            break;
                        }
#endif
                        const bool txPowerChanged = config.txPower != rxConfig.txPower;
                        int txPowerState = radio.setOutputPower(rxConfig.txPower);
                        if (txPowerState != RADIOLIB_ERR_NONE) {
                            if (serialEnabled) Logger.warning(SENSOR_LOG, "Rejected config TX power %ddBm (state %d).", rxConfig.txPower, txPowerState);
                            break;
                        }
                        if (txPowerChanged) {
                            config.txPower = rxConfig.txPower;
                            updateRtcConfig();
                            reportStateValid = false;
                        }

                        if (rxConfig.isDevMode > 1 || rxConfig.sleepInterval < 10) {
                            if (serialEnabled) Logger.warning(SENSOR_LOG, "Rejected config mode or sleep interval.");
                            break;
                        }
                        if (rxConfig.cpuFrequencyMhz != CONFIG_CPU_UNCHANGED && rxConfig.cpuFrequencyMhz != 80 && rxConfig.cpuFrequencyMhz != 160 && rxConfig.cpuFrequencyMhz != 240) {
                            if (serialEnabled) Logger.warning(SENSOR_LOG, "Rejected CPU frequency %u MHz.", rxConfig.cpuFrequencyMhz);
                            break;
                        }
                        const bool cpuFrequencyChanged = rxConfig.cpuFrequencyMhz != CONFIG_CPU_UNCHANGED &&
                                                         (config.cpuFrequencyMhz != rxConfig.cpuFrequencyMhz ||
                                                          getCpuFrequencyMhz() != rxConfig.cpuFrequencyMhz);
                        const bool effectiveDevMode = digitalRead(DEV_MODE_PIN) == LOW || config.isDevMode != 0;
                        if (cpuFrequencyChanged && (!effectiveDevMode || (rxConfig.isDevMode != config.isDevMode))) {
                            if (serialEnabled) Logger.warning(SENSOR_LOG, "Rejected CPU change outside Development Mode or combined with mode change.");
                            break;
                        }
                        bool requestedDevMode = (digitalRead(DEV_MODE_PIN) == LOW) || (rxConfig.isDevMode != 0);
                        uint32_t requestedAirtimeUs = requestedDevMode
                                                         ? rtcAirtime.calculatedDevelopmentUs
                                                         : rtcAirtime.calculatedOperationUs;
                        uint32_t requestedMinimumSeconds = LoRaDutyCycle::minimumSleepSeconds(requestedAirtimeUs);
                        if (rxConfig.sleepInterval < requestedMinimumSeconds) {
                            if (serialEnabled) {
                                Logger.warning(SENSOR_LOG, "Rejected sleep %us: minimum=%lus payload=%u airtime=%luus.",
                                               rxConfig.sleepInterval, (unsigned long)requestedMinimumSeconds,
                                               requestedDevMode ? TELEMETRY_AUTH_FULL_SIZE : TELEMETRY_AUTH_CORE_SIZE,
                                               (unsigned long)requestedAirtimeUs);
                            }
                            break;
                        }

                        if (cpuFrequencyChanged && !setCpuFrequencyMhz(rxConfig.cpuFrequencyMhz)) {
                            radio.setOutputPower(config.txPower);
                            if (serialEnabled) Logger.warning(SENSOR_LOG, "Failed to apply CPU frequency %u MHz.", rxConfig.cpuFrequencyMhz);
                            break;
                        }

                        config.magicWord = RTC_MAGIC_WORD;
                        config.configVersion = SENSOR_CONFIG_VERSION;
                        config.sleepInterval = rxConfig.sleepInterval;
                        config.isDevMode = (rxConfig.isDevMode > 0);
                        config.txPower = rxConfig.txPower;
                        if (rxConfig.cpuFrequencyMhz != CONFIG_CPU_UNCHANGED) config.cpuFrequencyMhz = rxConfig.cpuFrequencyMhz;
                        if (rxConfig.timeOffset > 0) {
                            uint32_t newTime = rxConfig.timeOffset + CUSTOM_EPOCH;
                            time_t now;
                            time(&now);

                            // Sync if the local RTC is unset or has drifted by more than 2 seconds.
                            if (!isClockValid(now) || hasClockDrift(now, (time_t)newTime, 2.0)) {
                                if (serialEnabled) {
                                    Logger.notice(SENSOR_LOG, "Time drift detected! Old: %lu, New: %lu. Syncing...", (unsigned long)now, (unsigned long)newTime);
                                }
                                // Sync the internal ESP32 RTC to fix time drift
                                struct timeval tv;
                                tv.tv_sec = newTime;
                                tv.tv_usec = 0;
                                settimeofday(&tv, NULL);
                            }
                        }

                        if (serialEnabled) {
                            Logger.info(SENSOR_LOG, "Received config: version=%u, sleep=%u, devMode=%u, txPower=%ddBm, cpu=%uMHz, timeOffset=%lu",
                                        rxConfig.configVersion, rxConfig.sleepInterval, rxConfig.isDevMode,
                                        rxConfig.txPower, rxConfig.cpuFrequencyMhz, (unsigned long)rxConfig.timeOffset);
                        }
                        updateRtcConfig(); // Save to RTC with integrity protection
                        reportStateValid = false; // Confirm every accepted config on the next uplink.
                        const bool devPinHeld = digitalRead(DEV_MODE_PIN) == LOW;
                        const bool cfgMode = devPinHeld || (config.isDevMode != 0);
                        if ((!devPinHeld || !devMode) && (devMode != cfgMode || txPowerChanged)) {
                            needsRestart = true;
                            if (serialEnabled) {
                                Logger.notice(SENSOR_LOG, "Soft restart needed to apply received configuration; effectiveDevMode=%d.", cfgMode);
                            }
                        }
                        if (serialEnabled) {
                            Logger.notice(SENSOR_LOG, "Config applied: sleep=%u, remoteDevMode=%u, devPinLow=%d, effectiveDevMode=%d, txPower=%ddBm, cpu=%uMHz",
                                          config.sleepInterval, config.isDevMode, devPinHeld, cfgMode, config.txPower,
                                          getCpuFrequencyMhz());
                        }
                        configReceived = true;
                        break; // Successfully received and applied config, exit 5s RX window early
                    } else {
                        if (serialEnabled) Logger.error(SENSOR_LOG, "Invalid config header. Terminating RX.");
                        break;
                    }
                } else {
                    if (serialEnabled) Logger.error(SENSOR_LOG, "Failed to read config packet (state %d). Terminating RX.", state);
                    break;
                }
            } else {
                if (serialEnabled) Logger.warning(SENSOR_LOG, "Received unexpected packet length (%d). Terminating RX.", sizeof(ConfigPayload));
                break;
            }
        }
    }
    radio.standby();

    lastRxTime = millis() - startTime; // Record the total time the radio was consuming power in RX mode
    if (serialEnabled) {
        Logger.info(SENSOR_LOG, "RX window closed. Total RX power-on time: %lu ms. Config %s",
                    lastRxTime, configReceived ? "received" : "NOT received");
    }
    

    if (needsRestart) {
        if (serialEnabled) {
            Logger.notice(SENSOR_LOG, "Restarting device to apply remote configuration.");
        }
        displayOff();
        flushSerialOutput();
        bootNonce = esp_random();
        if (bootNonce == 0) bootNonce = 1;
        ESP.restart(); // Soft reset
    }

    return configReceived;
}

// Handles sleep timing, conditionally triggering the configuration listener, and deep sleeping the ESP32.
// Used in: Both Operation and Dev modes (skips actual esp_deep_sleep_start in Dev mode)
void enterDeepSleep(uint32_t sleepSeconds)
{
    uint64_t requestedSleepUs;
    if (sleepSeconds > 0) {
        requestedSleepUs = (uint64_t)sleepSeconds * 1000000ULL;
    } else {
        requestedSleepUs = (uint64_t)config.sleepInterval * 1000000ULL;
        if (serialEnabled) Logger.info(SENSOR_LOG, "Entering configured deep sleep for %lu seconds",
                                       (unsigned long)config.sleepInterval);
    }

    if (serialEnabled && sleepSeconds > 0) Logger.info(SENSOR_LOG, "Entering fail-safe deep sleep for %u seconds", sleepSeconds);

    displayOff();

    // Attempt this even after an initialization failure so a partially responsive
    // external radio is not left in standby while the ESP32 sleeps.
    int16_t radioSleepState = radio.sleep();
    if (radioSleepState != RADIOLIB_ERR_NONE && serialEnabled) {
        Logger.warning(SENSOR_LOG, "Radio sleep failed: %d", radioSleepState);
    }

    if (ina226Initialized) {
        ina226.setMeasureMode(INA226_POWER_DOWN);
    }

    if (serialEnabled) {
        flushSerialOutput();
    }

    // Configure wake up timer
    esp_sleep_enable_timer_wakeup(requestedSleepUs);

    // Enter deep sleep
    esp_deep_sleep_start();
}

// Renders telemetry and device data to the OLED display. Cycles through 5 different informational screens.
// Used in: ONLY Dev mode
void drawMain()
{
    if (devMode) {
        Logger.debug(SENSOR_LOG, "drawMain called");
        if (disp) {
            Logger.debug(SENSOR_LOG, "disp found, drawing...");
        } else {
            Logger.warning(SENSOR_LOG, "disp not found, cannot draw");
        }
    }
    if (devMode && disp) {
        screenNum = (screenNum + 1) % 5; // Cycle through 5 screens
        disp->clearBuffer();
        disp->drawRFrame(0, 0, 128, 64, 5);
        disp->setFont(u8g2_font_pxplusibmvga8_mr);

        switch (screenNum) {
            case 0: // Primary Sensors
                disp->setCursor(5, 15);
                disp->printf("TSense: %.1fC", sensorData.temperature);
                disp->setCursor(5, 30);
                disp->printf("TCPU: %.1fC", sensorData.cpuTemp);
                disp->setCursor(5, 45);
                disp->printf("VCC: %.2fV", sensorData.batteryVoltage);
                disp->setCursor(5, 60);
                disp->printf("Batt: %d%%", sensorData.batteryPercent);
                break;

            case 1: // Power Details
                disp->setCursor(5, 15);
                disp->printf("I: %+.0fmA", sensorData.batteryCurrent);
                disp->setCursor(5, 30);
                disp->printf("P: %+.0fmW", sensorData.batteryPower);
                disp->setCursor(5, 45);
                disp->printf("RAM: %u KB", sensorData.freeRam);
                break;

            case 2: // LoRa & System
                disp->setCursor(5, 15);
                disp->printf("SNR:%.1f R:%.0f", sensorData.lastSNR, sensorData.lastRSSI);
                disp->setCursor(5, 30);
                disp->printf("TX Pwr: %ddBm", config.txPower);
                disp->setCursor(5, 45);
                disp->printf("TX Time: %lu ms", lastTxTime);
                disp->setCursor(5, 60);
                disp->printf("RX Time: %lu ms", lastRxTime);
                break;

            case 3: // IDs & Payload
                disp->setCursor(5, 15);
                disp->printf("ID: %.10s", deviceId.c_str());
                disp->setCursor(5, 30);
                disp->printf("Cnt:%u", counter);
                disp->setCursor(5, 45);
                disp->printf("Sleep: %d s", config.sleepInterval);
                
                // Scrolling payload preview
                if (payload.length() > 18) { // Approx 18 chars fit
                    msgOffset = (msgOffset + 1) % (payload.length() - 18);
                } else {
                    msgOffset = 0;
                }
                disp->setCursor(0, 60);
                disp->printf(">%.18s", payload.c_str() + msgOffset);
                break;

            case 4: // Active radio configuration
                disp->setCursor(5, 15);
                disp->printf("F: %.2f MHz", (double)CONFIG_RADIO_FREQ);
                disp->setCursor(5, 30);
                disp->printf("BW: %.2f kHz", RADIO_PROFILE_BANDWIDTHS_KHZ[sensorRadioProfile.active.bandwidthIndex]);
                disp->setCursor(5, 45);
                disp->printf("SF%u CR4/%u", sensorRadioProfile.active.spreadingFactor, sensorRadioProfile.active.codingRate);
                disp->setCursor(5, 60);
                disp->printf("TX:%ddBm CRC:on", config.txPower);
                break;
        }
        disp->sendBuffer();
    }
}
