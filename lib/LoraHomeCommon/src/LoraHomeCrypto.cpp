#include "LoraHomeCrypto.h"
#include "LoraHomeCommon.h"

#include <AES.h>
#include <CTR.h>
#include <Crypto.h>
#include <SHA256.h>
#include <string.h>

namespace LoraHomeCrypto {

namespace {
constexpr char TELEMETRY_AUTH_LABEL[] = "LoraHome telemetry auth v1";
constexpr char RADIO_PROFILE_AUTH_LABEL[] = "LoraHome radio profile v1";
}

bool isConfiguredAesKey(const uint8_t key[AES128_KEY_SIZE]) {
    if (!key) return false;
    for (size_t i = 0; i < AES128_KEY_SIZE; ++i) {
        if (key[i] != 0) return true;
    }
    return false;
}

bool cryptPayload(uint8_t* data,
                  size_t length,
                  const uint8_t key[AES128_KEY_SIZE],
                  const uint8_t mac[SENSOR_MAC_SIZE],
                  uint32_t bootNonce,
                  uint16_t messageCount) {
    if (!key || !mac || (!data && length != 0)) return false;

    uint8_t nonce[16] = {0};
    memcpy(nonce, mac, SENSOR_MAC_SIZE);
    nonce[6] = (uint8_t)bootNonce;
    nonce[7] = (uint8_t)(bootNonce >> 8);
    nonce[8] = (uint8_t)(bootNonce >> 16);
    nonce[9] = (uint8_t)(bootNonce >> 24);
    nonce[10] = (uint8_t)messageCount;
    nonce[11] = (uint8_t)(messageCount >> 8);

    CTR<AES128> cipher;
    const bool configured = cipher.setKey(key, AES128_KEY_SIZE) &&
                            cipher.setIV(nonce, sizeof(nonce));
    if (configured && length != 0) cipher.encrypt(data, data, length);

    cipher.clear();
    clean(nonce, sizeof(nonce));
    return configured;
}

bool deriveTelemetryAuthKey(const uint8_t rootKey[AES128_KEY_SIZE],
                            uint8_t authKey[TELEMETRY_AUTH_KEY_SIZE]) {
    if (!rootKey || !authKey) return false;
    hmac<SHA256>(authKey, TELEMETRY_AUTH_KEY_SIZE, rootKey, AES128_KEY_SIZE,
                 reinterpret_cast<const uint8_t*>(TELEMETRY_AUTH_LABEL),
                 sizeof(TELEMETRY_AUTH_LABEL) - 1);
    return true;
}

bool deriveRadioProfileAuthKey(const uint8_t rootKey[AES128_KEY_SIZE],
                               uint8_t authKey[TELEMETRY_AUTH_KEY_SIZE]) {
    if (!rootKey || !authKey) return false;
    hmac<SHA256>(authKey, TELEMETRY_AUTH_KEY_SIZE, rootKey, AES128_KEY_SIZE,
                 reinterpret_cast<const uint8_t*>(RADIO_PROFILE_AUTH_LABEL),
                 sizeof(RADIO_PROFILE_AUTH_LABEL) - 1);
    return true;
}

bool computeTelemetryTag(const uint8_t* message,
                         size_t messageLength,
                         const uint8_t authKey[TELEMETRY_AUTH_KEY_SIZE],
                         uint8_t tag[TELEMETRY_AUTH_TAG_SIZE]) {
    if ((!message && messageLength != 0) || !authKey || !tag) return false;
    uint8_t digest[SHA256::HASH_SIZE];
    hmac<SHA256>(digest, sizeof(digest), authKey, TELEMETRY_AUTH_KEY_SIZE,
                 message, messageLength);
    memcpy(tag, digest, TELEMETRY_AUTH_TAG_SIZE);
    clean(digest, sizeof(digest));
    return true;
}

bool verifyTelemetryTag(const uint8_t* message,
                        size_t messageLength,
                        const uint8_t authKey[TELEMETRY_AUTH_KEY_SIZE],
                        const uint8_t tag[TELEMETRY_AUTH_TAG_SIZE]) {
    if (!authKey || !tag) return false;
    uint8_t expected[TELEMETRY_AUTH_TAG_SIZE];
    if (!computeTelemetryTag(message, messageLength, authKey, expected)) return false;
    const bool matches = secure_compare(expected, tag, TELEMETRY_AUTH_TAG_SIZE);
    clean(expected, sizeof(expected));
    return matches;
}

bool encodeAuthenticatedTelemetryFrame(const uint8_t* legacyPayload,
                                       size_t legacyLength,
                                       const uint8_t rootKey[AES128_KEY_SIZE],
                                       uint8_t* frame,
                                       size_t frameCapacity,
                                       size_t* frameLength) {
    if (!legacyPayload || !rootKey || !frame || !frameLength ||
        (legacyLength != TELEMETRY_CORE_SIZE && legacyLength != sizeof(TelemetryPayload))) return false;
    const size_t requiredLength = TELEMETRY_FRAME_PREFIX_SIZE + (legacyLength - 12) + TELEMETRY_FRAME_TAG_SIZE;
    if (frameCapacity < requiredLength) return false;
    uint32_t bootNonce;
    uint16_t messageCount;
    memcpy(&bootNonce, legacyPayload + 6, sizeof(bootNonce));
    memcpy(&messageCount, legacyPayload + 10, sizeof(messageCount));
    frame[0] = TELEMETRY_FRAME_VERSION;
    frame[1] = TELEMETRY_FRAME_TYPE;
    memcpy(frame + 2, legacyPayload, 12);
    memcpy(frame + TELEMETRY_FRAME_PREFIX_SIZE, legacyPayload + 12, legacyLength - 12);
    uint8_t authKey[TELEMETRY_AUTH_KEY_SIZE] = {};
    const bool encrypted = cryptPayload(frame + TELEMETRY_FRAME_PREFIX_SIZE, legacyLength - 12,
                                        rootKey, legacyPayload, bootNonce, messageCount);
    const bool authenticated = encrypted && deriveTelemetryAuthKey(rootKey, authKey) &&
                               computeTelemetryTag(frame, requiredLength - TELEMETRY_FRAME_TAG_SIZE,
                                                   authKey, frame + requiredLength - TELEMETRY_FRAME_TAG_SIZE);
    clean(authKey, sizeof(authKey));
    if (!authenticated) return false;
    *frameLength = requiredLength;
    return true;
}

bool decodeAuthenticatedTelemetryFrame(const uint8_t* frame,
                                       size_t frameLength,
                                       const uint8_t rootKey[AES128_KEY_SIZE],
                                       uint8_t* legacyPayload,
                                       size_t legacyCapacity,
                                       size_t* legacyLength) {
    if (!frame || !rootKey || !legacyPayload || !legacyLength ||
        (frameLength != TELEMETRY_AUTH_CORE_SIZE && frameLength != TELEMETRY_AUTH_FULL_SIZE)) return false;
    const size_t decodedLength = frameLength == TELEMETRY_AUTH_CORE_SIZE ? TELEMETRY_CORE_SIZE : sizeof(TelemetryPayload);
    if (legacyCapacity < decodedLength || frame[0] != TELEMETRY_FRAME_VERSION || frame[1] != TELEMETRY_FRAME_TYPE) return false;
    uint8_t authKey[TELEMETRY_AUTH_KEY_SIZE] = {};
    const bool verified = deriveTelemetryAuthKey(rootKey, authKey) &&
                          verifyTelemetryTag(frame, frameLength - TELEMETRY_FRAME_TAG_SIZE, authKey,
                                              frame + frameLength - TELEMETRY_FRAME_TAG_SIZE);
    clean(authKey, sizeof(authKey));
    if (!verified) return false;
    memcpy(legacyPayload, frame + 2, 12);
    memcpy(legacyPayload + 12, frame + TELEMETRY_FRAME_PREFIX_SIZE, decodedLength - 12);
    *legacyLength = decodedLength;
    return true;
}

bool encodeAuthenticatedProfileTelemetryFrame(const uint8_t* legacyPayload,
                                              size_t legacyLength,
                                              const RadioProfile& profile,
                                              const uint8_t rootKey[AES128_KEY_SIZE],
                                              uint8_t* frame,
                                              size_t frameCapacity,
                                              size_t* frameLength) {
    if (!legacyPayload || !rootKey || !frame || !frameLength ||
        (legacyLength != TELEMETRY_OPERATION_SIZE && legacyLength != TELEMETRY_CORE_SIZE && legacyLength != sizeof(TelemetryPayload))) return false;
    const size_t payloadLength = legacyLength - 12;
    const size_t requiredLength = TELEMETRY_FRAME_PREFIX_SIZE + payloadLength + sizeof(profile) + TELEMETRY_FRAME_TAG_SIZE;
    if (frameCapacity < requiredLength) return false;
    frame[0] = legacyLength == TELEMETRY_OPERATION_SIZE ? RADIO_PROFILE_OPERATION_VERSION : RADIO_PROFILE_FRAME_VERSION;
    frame[1] = RADIO_PROFILE_TELEMETRY_TYPE;
    memcpy(frame + 2, legacyPayload, 12);
    memcpy(frame + TELEMETRY_FRAME_PREFIX_SIZE, legacyPayload + 12, payloadLength);
    uint32_t bootNonce;
    uint16_t messageCount;
    memcpy(&bootNonce, legacyPayload + 6, sizeof(bootNonce));
    memcpy(&messageCount, legacyPayload + 10, sizeof(messageCount));
    const bool encrypted = cryptPayload(frame + TELEMETRY_FRAME_PREFIX_SIZE, payloadLength,
                                        rootKey, legacyPayload, bootNonce, messageCount);
    const size_t profileOffset = TELEMETRY_FRAME_PREFIX_SIZE + payloadLength;
    memcpy(frame + profileOffset, &profile, sizeof(profile));
    uint8_t authKey[TELEMETRY_AUTH_KEY_SIZE] = {};
    const bool authenticated = encrypted && deriveTelemetryAuthKey(rootKey, authKey) &&
        computeTelemetryTag(frame, requiredLength - TELEMETRY_FRAME_TAG_SIZE,
                            authKey, frame + requiredLength - TELEMETRY_FRAME_TAG_SIZE);
    clean(authKey, sizeof(authKey));
    if (!authenticated) return false;
    *frameLength = requiredLength;
    return true;
}

bool decodeAuthenticatedProfileTelemetryFrame(const uint8_t* frame,
                                              size_t frameLength,
                                              const uint8_t rootKey[AES128_KEY_SIZE],
                                              uint8_t* legacyPayload,
                                              size_t legacyCapacity,
                                              size_t* legacyLength,
                                              RadioProfile* profile) {
    if (!frame || !rootKey || !legacyPayload || !legacyLength || !profile ||
        !((frameLength == RADIO_PROFILE_OPERATION_SIZE && frame[0] == RADIO_PROFILE_OPERATION_VERSION) ||
          ((frameLength == RADIO_PROFILE_TELEMETRY_CORE_SIZE || frameLength == RADIO_PROFILE_TELEMETRY_FULL_SIZE) &&
           frame[0] == RADIO_PROFILE_FRAME_VERSION)) || frame[1] != RADIO_PROFILE_TELEMETRY_TYPE) return false;
    const size_t decodedLength = frameLength == RADIO_PROFILE_OPERATION_SIZE ? TELEMETRY_OPERATION_SIZE :
                                 frameLength == RADIO_PROFILE_TELEMETRY_CORE_SIZE ? TELEMETRY_CORE_SIZE : sizeof(TelemetryPayload);
    if (legacyCapacity < decodedLength) return false;
    uint8_t authKey[TELEMETRY_AUTH_KEY_SIZE] = {};
    const bool verified = deriveTelemetryAuthKey(rootKey, authKey) &&
        verifyTelemetryTag(frame, frameLength - TELEMETRY_FRAME_TAG_SIZE, authKey,
                           frame + frameLength - TELEMETRY_FRAME_TAG_SIZE);
    clean(authKey, sizeof(authKey));
    if (!verified) return false;
    const size_t payloadLength = decodedLength - 12;
    memcpy(legacyPayload, frame + 2, 12);
    memcpy(legacyPayload + 12, frame + TELEMETRY_FRAME_PREFIX_SIZE, payloadLength);
    memcpy(profile, frame + TELEMETRY_FRAME_PREFIX_SIZE + payloadLength, sizeof(*profile));
    *legacyLength = decodedLength;
    return true;
}

bool verifyHmacSha256Hex(const uint8_t* key,
                         size_t keyLength,
                         const uint8_t* message,
                         size_t messageLength,
                         const char* expectedHex) {
    if (!key || keyLength == 0 || (!message && messageLength != 0) ||
        !expectedHex || strlen(expectedHex) != SHA256_HEX_SIZE) {
        return false;
    }

    uint8_t digest[SHA256::HASH_SIZE];
    hmac<SHA256>(digest, sizeof(digest), key, keyLength, message, messageLength);

    static const char HEX_DIGITS[] = "0123456789abcdef";
    char digestHex[SHA256_HEX_SIZE];
    for (size_t i = 0; i < sizeof(digest); ++i) {
        digestHex[i * 2] = HEX_DIGITS[digest[i] >> 4];
        digestHex[i * 2 + 1] = HEX_DIGITS[digest[i] & 0x0F];
    }

    const bool matches = secure_compare(digestHex, expectedHex, sizeof(digestHex));
    clean(digest, sizeof(digest));
    clean(digestHex, sizeof(digestHex));
    return matches;
}

} // namespace LoraHomeCrypto
