#pragma once
#include <stdint.h>

// Copy this file to secrets.h and fill in real values. Do NOT commit secrets.h.
#define SECRET_WIFI_SSID "your-wifi-name"
#define SECRET_WIFI_PASS "your-wifi-password"
// 16-byte AES-128 key for AES-GCM. Generate a random one; never reuse the example.
static const uint8_t AES_KEY[16] = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
