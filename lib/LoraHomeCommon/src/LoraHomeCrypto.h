#pragma once

#ifndef LORAHOMECRYPTO_H
#define LORAHOMECRYPTO_H

#include <stddef.h>
#include <stdint.h>

struct RadioProfile;

namespace LoraHomeCrypto {

constexpr size_t AES128_KEY_SIZE = 16;
constexpr size_t SENSOR_MAC_SIZE = 6;
constexpr size_t SHA256_HEX_SIZE = 64;
constexpr size_t TELEMETRY_AUTH_KEY_SIZE = 32;
constexpr size_t TELEMETRY_AUTH_TAG_SIZE = 8;

bool isConfiguredAesKey(const uint8_t key[AES128_KEY_SIZE]);

// Encrypts or decrypts telemetry in place. The nonce layout is part of the
// Sensor/Gateway wire contract and must remain MAC || bootNonceLE || countLE || 0.
bool cryptPayload(uint8_t* data,
                  size_t length,
                  const uint8_t key[AES128_KEY_SIZE],
                  const uint8_t mac[SENSOR_MAC_SIZE],
                  uint32_t bootNonce,
                  uint16_t messageCount);

// Derives an independent telemetry-authentication key from the AES root key.
bool deriveTelemetryAuthKey(const uint8_t rootKey[AES128_KEY_SIZE],
                            uint8_t authKey[TELEMETRY_AUTH_KEY_SIZE]);

// Separates profile-transition authentication from telemetry authentication.
bool deriveRadioProfileAuthKey(const uint8_t rootKey[AES128_KEY_SIZE],
                               uint8_t authKey[TELEMETRY_AUTH_KEY_SIZE]);

// Computes and verifies the truncated HMAC tag over an encoded telemetry frame.
bool computeTelemetryTag(const uint8_t* message,
                         size_t messageLength,
                         const uint8_t authKey[TELEMETRY_AUTH_KEY_SIZE],
                         uint8_t tag[TELEMETRY_AUTH_TAG_SIZE]);
bool verifyTelemetryTag(const uint8_t* message,
                        size_t messageLength,
                        const uint8_t authKey[TELEMETRY_AUTH_KEY_SIZE],
                        const uint8_t tag[TELEMETRY_AUTH_TAG_SIZE]);

bool encodeAuthenticatedTelemetryFrame(const uint8_t* legacyPayload,
                                       size_t legacyLength,
                                       const uint8_t rootKey[AES128_KEY_SIZE],
                                       uint8_t* frame,
                                       size_t frameCapacity,
                                       size_t* frameLength);
bool decodeAuthenticatedTelemetryFrame(const uint8_t* frame,
                                       size_t frameLength,
                                       const uint8_t rootKey[AES128_KEY_SIZE],
                                       uint8_t* legacyPayload,
                                       size_t legacyCapacity,
                                       size_t* legacyLength);
bool encodeAuthenticatedProfileTelemetryFrame(const uint8_t* legacyPayload,
                                              size_t legacyLength,
                                              const RadioProfile& profile,
                                              const uint8_t rootKey[AES128_KEY_SIZE],
                                              uint8_t* frame,
                                              size_t frameCapacity,
                                              size_t* frameLength);
bool decodeAuthenticatedProfileTelemetryFrame(const uint8_t* frame,
                                              size_t frameLength,
                                              const uint8_t rootKey[AES128_KEY_SIZE],
                                              uint8_t* legacyPayload,
                                              size_t legacyCapacity,
                                              size_t* legacyLength,
                                              RadioProfile* profile);

// Verifies a lowercase hexadecimal HMAC-SHA256 without early-exit comparison.
bool verifyHmacSha256Hex(const uint8_t* key,
                         size_t keyLength,
                         const uint8_t* message,
                         size_t messageLength,
                         const char* expectedHex);

} // namespace LoraHomeCrypto

#endif // LORAHOMECRYPTO_H
