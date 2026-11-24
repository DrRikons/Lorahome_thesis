#pragma once

#include <stdint.h>

namespace LoRaDutyCycle {

constexpr uint32_t DUTY_CYCLE_DENOMINATOR = 100;

inline uint64_t requiredSilenceUs(uint64_t airtimeUs) {
    return airtimeUs * (DUTY_CYCLE_DENOMINATOR - 1U);
}

inline uint32_t minimumSleepSeconds(uint64_t airtimeUs) {
    const uint64_t silenceUs = requiredSilenceUs(airtimeUs);
    return (uint32_t)((silenceUs + 999999ULL) / 1000000ULL);
}

inline uint32_t requiredSilenceMs(uint64_t airtimeUs) {
    const uint64_t silenceUs = requiredSilenceUs(airtimeUs);
    return (uint32_t)((silenceUs + 999ULL) / 1000ULL);
}

} // namespace LoRaDutyCycle
