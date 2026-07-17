// Hotspot Arcade UART v2 wire protocol (ESP side).
// Must stay byte-for-byte identical to the Flipper side (flipper/.../ha_proto.h)
// and docs/PROTOCOL.md. Framed control messages + a raw-bulk escape for files.
#pragma once
#include <Arduino.h>

#define HA_UART_BAUD 921600
#define HA_SYNC 0xA5
#define HA_MAX_PAYLOAD 4096

// Flipper -> ESP
enum {
    HA_MSG_CLEAR_FILES = 0x10,
    HA_MSG_FILE_BEGIN = 0x11, // hdr frame, then `total` raw bytes follow
    HA_MSG_SET_AP = 0x12,
    HA_MSG_START = 0x13,
    HA_MSG_STOP = 0x14,
    HA_MSG_RESET = 0x15,
    HA_MSG_SELECT_GAME = 0x16,
    HA_MSG_QUESTION = 0x17,
    HA_MSG_REVEAL = 0x18,
    HA_MSG_ROUND_END = 0x19,
    HA_MSG_CONFIG = 0x1A,
    HA_MSG_RESET_SCORES = 0x1B,
};

// ESP -> Flipper
enum {
    HA_MSG_STATUS = 0x80,
    HA_MSG_JOIN = 0x81,
    HA_MSG_LEAVE = 0x82,
    HA_MSG_SCORE = 0x83,
    HA_MSG_ROUND_RESULT = 0x84,
    HA_MSG_EVENT = 0x85,
    HA_MSG_PING = 0x86,
};

// Game ids
enum {
    HA_GAME_NONE = 0,
    HA_GAME_TRIVIA = 1,
    HA_GAME_CONNECT4 = 2,
};

// CRC-8/ATM: poly 0x07, init 0x00, no reflect, no xorout. Identical both sides.
static inline uint8_t ha_crc8_upd(uint8_t crc, uint8_t b) {
    crc ^= b;
    for(uint8_t i = 0; i < 8; i++) {
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

static inline uint8_t ha_crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for(size_t i = 0; i < len; i++) crc = ha_crc8_upd(crc, data[i]);
    return crc;
}
