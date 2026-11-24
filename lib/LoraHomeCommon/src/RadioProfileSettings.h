#pragma once

#include <RadioLib.h>
#include "LoraHomeCommon.h"
#include "LoRaDutyCycle.h"

// SX1276 bandwidth values use the same order as the profile wire format.
constexpr float RADIO_PROFILE_BANDWIDTHS_KHZ[] = {
    7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f
};
constexpr size_t RADIO_PROFILE_BANDWIDTH_COUNT =
    sizeof(RADIO_PROFILE_BANDWIDTHS_KHZ) / sizeof(RADIO_PROFILE_BANDWIDTHS_KHZ[0]);
constexpr float RADIO_PROFILE_EU868_LOWER_EDGE_MHZ = 868.0f;
constexpr float RADIO_PROFILE_EU868_UPPER_EDGE_MHZ = 868.6f;
constexpr float RADIO_PROFILE_EU868_CENTER_FREQUENCY_MHZ = 868.300f;
constexpr RadioProfile RADIO_PROFILE_FAST = {7, 7, 5};
constexpr RadioProfile RADIO_PROFILE_DEFAULT = {9, 7, 5};
constexpr RadioProfile RADIO_PROFILE_RANGE = {10, 7, 5};

constexpr bool radioProfileBandwidthFitsEu868SubBand(size_t bandwidthIndex) {
    return bandwidthIndex < RADIO_PROFILE_BANDWIDTH_COUNT &&
           RADIO_PROFILE_EU868_CENTER_FREQUENCY_MHZ - RADIO_PROFILE_BANDWIDTHS_KHZ[bandwidthIndex] / 2000.0f >= RADIO_PROFILE_EU868_LOWER_EDGE_MHZ &&
           RADIO_PROFILE_EU868_CENTER_FREQUENCY_MHZ + RADIO_PROFILE_BANDWIDTHS_KHZ[bandwidthIndex] / 2000.0f <= RADIO_PROFILE_EU868_UPPER_EDGE_MHZ;
}

inline bool radioProfileFitsEu868SubBand(const RadioProfile& profile) {
    return radioProfileBandwidthFitsEu868SubBand(profile.bandwidthIndex);
}

static_assert(radioProfileBandwidthFitsEu868SubBand(0) && radioProfileBandwidthFitsEu868SubBand(1) &&
              radioProfileBandwidthFitsEu868SubBand(2) && radioProfileBandwidthFitsEu868SubBand(3) &&
              radioProfileBandwidthFitsEu868SubBand(4) && radioProfileBandwidthFitsEu868SubBand(5) &&
              radioProfileBandwidthFitsEu868SubBand(6) && radioProfileBandwidthFitsEu868SubBand(7) &&
              radioProfileBandwidthFitsEu868SubBand(8) && radioProfileBandwidthFitsEu868SubBand(9),
              "Configured radio-profile bandwidth must remain within the EU868 sub-band.");

inline bool radioProfileIsValid(PhysicalLayer& radio, const RadioProfile& profile) {
    if (profile.spreadingFactor < 7 || profile.spreadingFactor > 12 ||
        profile.bandwidthIndex >= RADIO_PROFILE_BANDWIDTH_COUNT ||
        profile.codingRate < 5 || profile.codingRate > 8) return false;
#if defined(T3_V1_6_SX1276)
    if (!radioProfileFitsEu868SubBand(profile)) return false;
#endif
    DataRate_t rate = {};
    rate.lora.spreadingFactor = profile.spreadingFactor;
    rate.lora.bandwidth = RADIO_PROFILE_BANDWIDTHS_KHZ[profile.bandwidthIndex];
    rate.lora.codingRate = profile.codingRate;
    return radio.checkDataRate(rate, RADIOLIB_MODEM_LORA) == RADIOLIB_ERR_NONE;
}

// RadioLib calculates airtime without changing the currently active modem.
inline bool radioProfileAirtimes(PhysicalLayer& radio, const RadioProfile& profile,
                                 uint32_t& coreUs, uint32_t& developmentUs,
                                 uint32_t& requestUs, uint32_t& ackUs) {
    if (!radioProfileIsValid(radio, profile)) return false;
    DataRate_t rate = {};
    rate.lora.spreadingFactor = profile.spreadingFactor;
    rate.lora.bandwidth = RADIO_PROFILE_BANDWIDTHS_KHZ[profile.bandwidthIndex];
    rate.lora.codingRate = profile.codingRate;
    PacketConfig_t packet = {};
    packet.lora.preambleLength = 8;
    packet.lora.implicitHeader = false;
    packet.lora.crcEnabled = true;
    packet.lora.ldrOptimize = ((1UL << profile.spreadingFactor) / rate.lora.bandwidth) >= 16.0f;
    coreUs = radio.calculateTimeOnAir(RADIOLIB_MODEM_LORA, rate, packet, RADIO_PROFILE_OPERATION_SIZE);
    developmentUs = radio.calculateTimeOnAir(RADIOLIB_MODEM_LORA, rate, packet, RADIO_PROFILE_TELEMETRY_FULL_SIZE);
    requestUs = radio.calculateTimeOnAir(RADIOLIB_MODEM_LORA, rate, packet, sizeof(RadioProfileRequest));
    ackUs = radio.calculateTimeOnAir(RADIOLIB_MODEM_LORA, rate, packet, sizeof(RadioProfileAck));
    return coreUs != 0 && developmentUs != 0 && requestUs != 0 && ackUs != 0 &&
           LoRaDutyCycle::minimumSleepSeconds(coreUs) <= 255 &&
           LoRaDutyCycle::minimumSleepSeconds(developmentUs) <= 255 &&
           LoRaDutyCycle::minimumSleepSeconds(requestUs) <= 255 &&
           LoRaDutyCycle::minimumSleepSeconds(ackUs) <= 255;
}
