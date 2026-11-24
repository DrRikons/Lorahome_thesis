#pragma once

#include <stdint.h>

// Copy or rename this file to secrets.h and replace every placeholder before building.
// Define LORAHOME_PRODUCTION_BUILD=1 in the production build flags. Production
// firmware refuses to start Gateway services when these values are blank or
// still match the placeholders below; development builds keep the bootstrap
// path available for deliberate local provisioning.
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define WIFI_HOSTNAME "LoRaGateway"

// MQTT defaults used when the SD configuration has not supplied replacements.
#define MQTT_USER "YOUR_MQTT_USER"
#define MQTT_PASSWORD "YOUR_MQTT_PASSWORD"
#define MQTT_BROKER "YOUR_MQTT_BROKER_HOST"

// Sensitive Gateway values copied into /config.json when their keys are missing.
#define CONTROL_COMMAND_SECRET "YOUR_COMMAND_SIGNING_SECRET"
#define WEBUI_ADMIN_PASSWORD "YOUR_WEBUI_ADMIN_PASSWORD_12_CHARS_MINIMUM"

// Gateway and every Sensor must use the same values.
#define NETWORK_KEY 0x00000000

const uint8_t AES_NETWORK_KEY[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
