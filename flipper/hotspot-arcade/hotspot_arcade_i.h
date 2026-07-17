#pragma once

#include <furi.h>
#include <furi_hal_rtc.h>
#include <datetime/datetime.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/widget.h>
#include <gui/modules/variable_item_list.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include "ha_uart.h"
#include "ha_proto.h"
#include "ha_custom_event.h"
#include "scenes/ha_scene.h"

#define HA_SSID_MAX (33) // 32 + NUL
#define HA_MAX_PLAYERS (12)
#define HA_NICK_LEN (20)
#define HA_MAX_ASSETS (8)
#define HA_ASSET_PATH (40)
#define HA_ASSET_MIME (32)
#define HA_LINE_MAX (512)
#define HA_LINK_TIMEOUT_MS (5000)
#define HA_CONSOLE_MAX (3072)
#define HA_FILE_MAX (60000) // max single web asset streamed to the ESP
#define HA_DEFAULT_DUR (20) // trivia seconds per question

#define HA_DATA_DIR EXT_PATH("apps_data/hotspot_arcade")
#define HA_WEB_DIR HA_DATA_DIR "/web"
#define HA_TRIVIA_DIR HA_DATA_DIR "/trivia"
#define HA_LOGS_DIR HA_DATA_DIR "/logs"
#define HA_CONFIG_PATH HA_DATA_DIR "/config.txt"
#define HA_MANIFEST_PATH HA_WEB_DIR "/manifest.json"

typedef enum {
    HaViewSubmenu,
    HaViewTextInput,
    HaViewWidget,
    HaViewVarItemList,
} HaView;

// A web bundle file, parsed from manifest.json, streamed to the ESP at start.
typedef struct {
    char path[HA_ASSET_PATH]; // URL path the ESP serves at ("/")
    char file[HA_ASSET_PATH]; // filename on SD in HA_WEB_DIR
    char mime[HA_ASSET_MIME];
    bool gzip;
} HaAsset;

// A connected player, mirrored from the ESP (JOIN/LEAVE/SCORE).
typedef struct {
    bool used;
    uint8_t pid;
    char nick[HA_NICK_LEN];
    int32_t score;
} HaPlayer;

// Handshake sequence at session start (driven by ESP STATUS acks).
typedef enum {
    HaHsIdle,
    HaHsClear, // sent CLEAR_FILES, waiting "cleared"
    HaHsFiles, // streaming files, waiting "fok" per file
    HaHsSetAp, // sent SET_AP, waiting "ap_set"
    HaHsStart, // sent START, waiting "up"
    HaHsUp, // portal live
    HaHsErr,
} HaHandshake;

typedef struct HotspotArcadeApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    DialogsApp* dialogs;
    NotificationApp* notifications;

    Submenu* submenu;
    TextInput* text_input;
    Widget* widget;
    VariableItemList* var_item_list;

    HaUart* uart;

    // Config (persisted)
    FuriString* ssid;
    char ssid_buf[HA_SSID_MAX];
    FuriString* trivia_pack_path; // selected pack file on SD
    uint32_t question_dur; // seconds per trivia question
    bool sound_on;
    bool vibro_on;

    // Web bundle (from manifest.json)
    HaAsset assets[HA_MAX_ASSETS];
    uint8_t asset_count;

    // Live roster
    HaPlayer players[HA_MAX_PLAYERS];

    // Active game (HA_GAME_*)
    uint8_t active_game;

    // Trivia host state
    FuriString* trivia_pack; // loaded pack file text
    int trivia_count; // number of questions in the pack
    int trivia_idx; // current question index
    uint8_t trivia_phase; // 0 idle, 1 question, 2 reveal
    int answers_in; // answers received for current question (from EVENT)
    int answers_total; // players connected (from EVENT)
    FuriString* cur_q; // current question text
    FuriString* cur_opts[4];
    int cur_correct;

    // Event feed / console
    FuriString* console; // scrollable raw event log
    FuriString* last_event; // most recent host-facing event line

    // RX frame parser (ESP -> Flipper)
    uint8_t rx_state;
    uint8_t rx_type;
    uint16_t rx_len;
    uint16_t rx_idx;
    uint8_t rx_crc;
    uint8_t rx_buf[HA_MAX_PAYLOAD + 1];

    // Status / lifecycle
    FuriString* status;
    bool portal_running;
    bool session_active;
    bool menu_shows_active;
    HaHandshake hs;
    uint8_t file_idx; // during HaHsFiles

    // Board liveness
    uint32_t last_rx_tick;
    bool link_lost;
    bool awaiting_board;

    volatile bool closing;
} HotspotArcadeApp;
