#include "ha_session.h"
#include "ha_storage.h"
#include "../hotspot_arcade_i.h"
#include "../ha_json.h"

// Everything here runs on the GUI thread (RX drained from the global custom event
// handler), so app state is single-threaded: no locking needed.

enum { RXS_SYNC, RXS_TYPE, RXS_LEN0, RXS_LEN1, RXS_PAYLOAD, RXS_CRC };

// ---------------- console / event feed ----------------

static void console_add(HotspotArcadeApp* app, const char* line) {
    furi_string_cat_str(app->console, line);
    furi_string_cat_str(app->console, "\n");
    size_t sz = furi_string_size(app->console);
    if(sz > HA_CONSOLE_MAX) furi_string_right(app->console, sz - HA_CONSOLE_MAX / 2);
}

// ---------------- roster ----------------

int ha_player_count(HotspotArcadeApp* app) {
    int n = 0;
    for(int i = 0; i < HA_MAX_PLAYERS; i++)
        if(app->players[i].used) n++;
    return n;
}

static int player_find(HotspotArcadeApp* app, uint8_t pid) {
    for(int i = 0; i < HA_MAX_PLAYERS; i++)
        if(app->players[i].used && app->players[i].pid == pid) return i;
    return -1;
}

static void player_join(HotspotArcadeApp* app, uint8_t pid, const char* nick) {
    int idx = player_find(app, pid);
    if(idx < 0) {
        for(int i = 0; i < HA_MAX_PLAYERS; i++) {
            if(!app->players[i].used) {
                idx = i;
                break;
            }
        }
    }
    if(idx < 0) return;
    HaPlayer* p = &app->players[idx];
    p->used = true;
    p->pid = pid;
    strlcpy(p->nick, (nick && nick[0]) ? nick : "Player", HA_NICK_LEN);
    p->score = 0;
}

static void player_leave(HotspotArcadeApp* app, uint8_t pid) {
    int idx = player_find(app, pid);
    if(idx >= 0) app->players[idx].used = false;
}

static void player_score(HotspotArcadeApp* app, uint8_t pid, int delta) {
    int idx = player_find(app, pid);
    if(idx >= 0) app->players[idx].score += delta;
}

static void roster_clear(HotspotArcadeApp* app) {
    for(int i = 0; i < HA_MAX_PLAYERS; i++) app->players[i].used = false;
}

// ---------------- trivia pack parsing ----------------

// Trim leading/trailing spaces and CR from a line slice into out.
static void copy_trim(const char* start, const char* end, FuriString* out) {
    while(start < end && (*start == ' ' || *start == '\t')) start++;
    while(end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
    furi_string_set(out, "");
    for(const char* p = start; p < end; p++) furi_string_push_back(out, *p);
}

// Load the idx-th question block from the loaded pack into cur_q / cur_opts /
// cur_correct. Returns true on a complete question (4 options + valid answer).
static bool trivia_load_question(HotspotArcadeApp* app, int idx) {
    const char* s = furi_string_get_cstr(app->trivia_pack);
    furi_string_set(app->cur_q, "");
    for(int k = 0; k < 4; k++) furi_string_set(app->cur_opts[k], "");
    app->cur_correct = 0;

    int block = -1;
    bool capturing = false;
    int got_opts = 0;
    bool got_answer = false;
    FuriString* tmp = furi_string_alloc();

    const char* line = s;
    while(*line) {
        const char* eol = line;
        while(*eol && *eol != '\n') eol++;
        // skip leading spaces for the tag check
        const char* t = line;
        while(t < eol && (*t == ' ' || *t == '\t')) t++;

        if(t[0] == 'Q' && t[1] == ':') {
            block++;
            if(capturing) break; // reached the next question
            if(block == idx) {
                capturing = true;
                copy_trim(t + 2, eol, app->cur_q);
                got_opts = 0;
                got_answer = false;
            }
        } else if(capturing) {
            if((t[0] == 'A' || t[0] == 'B' || t[0] == 'C' || t[0] == 'D') && t[1] == ':') {
                int k = t[0] - 'A';
                if(k >= 0 && k < 4) {
                    copy_trim(t + 2, eol, app->cur_opts[k]);
                    got_opts++;
                }
            } else if(strncmp(t, "Answer:", 7) == 0) {
                copy_trim(t + 7, eol, tmp);
                char c = furi_string_size(tmp) ? furi_string_get_char(tmp, 0) : 'A';
                if(c >= 'a') c = c - 'a' + 'A';
                if(c >= 'A' && c <= 'D') {
                    app->cur_correct = c - 'A';
                    got_answer = true;
                }
            } else if(t[0] == '-' && t[1] == '-') {
                break; // explicit block separator
            }
        }

        line = (*eol == '\n') ? eol + 1 : eol;
    }

    furi_string_free(tmp);
    return capturing && got_opts >= 4 && got_answer;
}

static void json_escape_cat(FuriString* out, const char* s) {
    for(const char* p = s; *p; p++) {
        char c = *p;
        if(c == '"' || c == '\\') {
            furi_string_push_back(out, '\\');
            furi_string_push_back(out, c);
        } else if(c == '\n') {
            furi_string_cat_str(out, "\\n");
        } else if((unsigned char)c >= 0x20) {
            furi_string_push_back(out, c);
        }
    }
}

static void trivia_send_current(HotspotArcadeApp* app) {
    FuriString* j = furi_string_alloc();
    furi_string_printf(j, "{\"i\":%d,\"q\":\"", app->trivia_idx);
    json_escape_cat(j, furi_string_get_cstr(app->cur_q));
    furi_string_cat_str(j, "\",\"o\":[");
    for(int k = 0; k < 4; k++) {
        if(k) furi_string_cat_str(j, ",");
        furi_string_cat_str(j, "\"");
        json_escape_cat(j, furi_string_get_cstr(app->cur_opts[k]));
        furi_string_cat_str(j, "\"");
    }
    furi_string_cat_printf(
        j, "],\"c\":%d,\"dur\":%lu}", app->cur_correct, (unsigned long)app->question_dur);
    ha_proto_send(
        app->uart, HA_MSG_QUESTION, (const uint8_t*)furi_string_get_cstr(j), furi_string_size(j));
    furi_string_free(j);
    app->trivia_phase = 1;
    app->answers_in = 0;
}

bool ha_trivia_begin(HotspotArcadeApp* app) {
    if(!ha_storage_load_trivia(app)) return false;
    app->trivia_idx = 0;
    if(!trivia_load_question(app, 0)) return false;
    trivia_send_current(app);
    return true;
}

void ha_trivia_reveal(HotspotArcadeApp* app) {
    if(app->trivia_phase != 1) return;
    ha_proto_send(app->uart, HA_MSG_REVEAL, NULL, 0);
    app->trivia_phase = 2;
}

bool ha_trivia_next(HotspotArcadeApp* app) {
    int next = app->trivia_idx + 1;
    if(next >= app->trivia_count || !trivia_load_question(app, next)) {
        ha_round_end(app);
        return false;
    }
    app->trivia_idx = next;
    trivia_send_current(app);
    return true;
}

void ha_round_end(HotspotArcadeApp* app) {
    ha_proto_send(app->uart, HA_MSG_ROUND_END, NULL, 0);
    app->trivia_phase = 0;
}

// ---------------- game selection ----------------

void ha_select_game(HotspotArcadeApp* app, uint8_t game) {
    app->active_game = game;
    app->trivia_phase = 0;
    app->trivia_idx = 0;
    uint8_t g = game;
    ha_proto_send(app->uart, HA_MSG_SELECT_GAME, &g, 1);
}

void ha_reset_scores(HotspotArcadeApp* app) {
    for(int i = 0; i < HA_MAX_PLAYERS; i++)
        if(app->players[i].used) app->players[i].score = 0;
    ha_proto_send(app->uart, HA_MSG_RESET_SCORES, NULL, 0);
}

// ---------------- handshake / file streaming ----------------

static void send_config(HotspotArcadeApp* app) {
    FuriString* j = furi_string_alloc();
    furi_string_printf(j, "{\"max\":%d}", HA_MAX_PLAYERS < 8 ? HA_MAX_PLAYERS : 8);
    ha_proto_send(
        app->uart, HA_MSG_CONFIG, (const uint8_t*)furi_string_get_cstr(j), furi_string_size(j));
    furi_string_free(j);
}

// Stream file asset[file_idx], or advance to SET_AP when all files are sent.
static void send_next_file(HotspotArcadeApp* app) {
    if(app->file_idx >= app->asset_count) {
        // All files streamed: name the AP, then start.
        ha_proto_send_str(app->uart, HA_MSG_SET_AP, furi_string_get_cstr(app->ssid));
        app->hs = HaHsSetAp;
        return;
    }
    HaAsset* a = &app->assets[app->file_idx];
    FuriString* path = furi_string_alloc();
    furi_string_printf(path, "%s/%s", HA_WEB_DIR, a->file);
    FuriString* content = furi_string_alloc();
    bool ok = ha_storage_read_file(furi_string_get_cstr(path), content, HA_FILE_MAX);
    furi_string_free(path);
    if(!ok) {
        furi_string_set(app->status, "asset read err");
        app->hs = HaHsErr;
        furi_string_free(content);
        return;
    }
    size_t total = furi_string_size(content);

    // FILE_BEGIN payload: flags(1) pathlen(1) path mimelen(1) mime total(4 LE)
    uint8_t hdr[HA_ASSET_PATH + HA_ASSET_MIME + 8];
    size_t i = 0;
    hdr[i++] = a->gzip ? 1 : 0;
    size_t pl = strlen(a->path);
    hdr[i++] = (uint8_t)pl;
    memcpy(hdr + i, a->path, pl);
    i += pl;
    size_t ml = strlen(a->mime);
    hdr[i++] = (uint8_t)ml;
    memcpy(hdr + i, a->mime, ml);
    i += ml;
    hdr[i++] = (uint8_t)(total & 0xFF);
    hdr[i++] = (uint8_t)((total >> 8) & 0xFF);
    hdr[i++] = (uint8_t)((total >> 16) & 0xFF);
    hdr[i++] = (uint8_t)((total >> 24) & 0xFF);
    ha_proto_send(app->uart, HA_MSG_FILE_BEGIN, hdr, i);

    // Then the raw file bytes (unframed), as the protocol's bulk escape.
    ha_uart_tx(app->uart, (const uint8_t*)furi_string_get_cstr(content), total);
    furi_string_free(content);
}

static void start_handshake(HotspotArcadeApp* app) {
    roster_clear(app);
    app->trivia_phase = 0;
    app->answers_in = 0;
    app->answers_total = 0;
    app->file_idx = 0;
    ha_storage_load_manifest(app);
    furi_string_set(app->status, "starting");
    // Discard stale bytes and reset the frame parser.
    app->rx_state = RXS_SYNC;
    uint8_t scratch[64];
    while(ha_uart_rx(app->uart, scratch, sizeof(scratch)) > 0) {
    }
    ha_proto_send(app->uart, HA_MSG_CLEAR_FILES, NULL, 0);
    app->hs = HaHsClear;
}

void ha_session_start(HotspotArcadeApp* app) {
    app->session_active = true;
    app->portal_running = false;
    app->link_lost = false;
    app->last_rx_tick = furi_get_tick();
    app->active_game = HA_GAME_NONE;
    furi_string_reset(app->console);
    start_handshake(app);
}

void ha_session_stop(HotspotArcadeApp* app) {
    ha_proto_send(app->uart, HA_MSG_STOP, NULL, 0);
    app->session_active = false;
    app->portal_running = false;
    app->hs = HaHsIdle;
    furi_string_set(app->status, "stopped");
}

// ---------------- STATUS handling (drives the handshake) ----------------

static void on_status(HotspotArcadeApp* app, const char* tok) {
    furi_string_set(app->status, tok);
    if(strncmp(tok, "cleared", 7) == 0) {
        if(app->hs == HaHsClear) {
            app->hs = HaHsFiles;
            app->file_idx = 0;
            send_next_file(app);
        }
    } else if(strncmp(tok, "fok", 3) == 0) {
        if(app->hs == HaHsFiles) {
            app->file_idx++;
            send_next_file(app);
        }
    } else if(strncmp(tok, "ap_set", 6) == 0) {
        if(app->hs == HaHsSetAp) {
            send_config(app);
            ha_proto_send(app->uart, HA_MSG_START, NULL, 0);
            app->hs = HaHsStart;
        }
    } else if(strncmp(tok, "up", 2) == 0) {
        app->portal_running = true;
        app->hs = HaHsUp;
        // Restore the selected game if the ESP had rebooted mid-session.
        if(app->active_game != HA_GAME_NONE) {
            uint8_t g = app->active_game;
            ha_proto_send(app->uart, HA_MSG_SELECT_GAME, &g, 1);
        }
    } else if(strncmp(tok, "stopped", 7) == 0) {
        app->portal_running = false;
    } else if(strncmp(tok, "boot", 4) == 0) {
        // ESP rebooted: it lost the AP + all clients. Redo the handshake.
        if(app->session_active) start_handshake(app);
    }
}

// ---------------- frame dispatch ----------------

static void dispatch_frame(HotspotArcadeApp* app) {
    uint8_t* p = app->rx_buf;
    uint16_t len = app->rx_len;
    p[len] = '\0'; // JSON/text payloads: safe (buf has +1)

    switch(app->rx_type) {
    case HA_MSG_STATUS:
        on_status(app, (const char*)p);
        break;
    case HA_MSG_JOIN:
        if(len >= 1) {
            char nick[HA_NICK_LEN];
            size_t nl = len - 1 < HA_NICK_LEN - 1 ? (size_t)(len - 1) : HA_NICK_LEN - 1;
            memcpy(nick, p + 1, nl);
            nick[nl] = '\0';
            player_join(app, p[0], nick);
            FuriString* c = furi_string_alloc();
            furi_string_printf(c, "JOIN %s", nick);
            console_add(app, furi_string_get_cstr(c));
            furi_string_free(c);
        }
        break;
    case HA_MSG_LEAVE:
        if(len >= 1) {
            player_leave(app, p[0]);
            console_add(app, "LEAVE");
        }
        break;
    case HA_MSG_SCORE:
        if(len >= 3) {
            int16_t d = (int16_t)((uint16_t)p[1] | ((uint16_t)p[2] << 8));
            player_score(app, p[0], d);
        }
        break;
    case HA_MSG_ROUND_RESULT:
        furi_string_set_str(app->last_event, (const char*)p);
        console_add(app, (const char*)p);
        break;
    case HA_MSG_EVENT: {
        int v;
        if(ha_json_int((const char*)p, "answers", &v)) {
            app->answers_in = v;
            if(ha_json_int((const char*)p, "total", &v)) app->answers_total = v;
        } else {
            char c4[64];
            if(ha_json_str((const char*)p, "c4", c4, sizeof(c4))) {
                furi_string_set_str(app->last_event, c4);
                console_add(app, c4);
            }
        }
        break;
    }
    case HA_MSG_PING:
        break; // liveness only
    default:
        break;
    }
}

static void rx_byte(HotspotArcadeApp* app, uint8_t c) {
    switch(app->rx_state) {
    case RXS_SYNC:
        if(c == HA_SYNC) app->rx_state = RXS_TYPE;
        break;
    case RXS_TYPE:
        app->rx_type = c;
        app->rx_crc = ha_crc8_upd(0, c);
        app->rx_state = RXS_LEN0;
        break;
    case RXS_LEN0:
        app->rx_len = c;
        app->rx_crc = ha_crc8_upd(app->rx_crc, c);
        app->rx_state = RXS_LEN1;
        break;
    case RXS_LEN1:
        app->rx_len |= ((uint16_t)c << 8);
        app->rx_crc = ha_crc8_upd(app->rx_crc, c);
        if(app->rx_len > HA_MAX_PAYLOAD) {
            app->rx_state = RXS_SYNC;
            break;
        }
        app->rx_idx = 0;
        app->rx_state = app->rx_len ? RXS_PAYLOAD : RXS_CRC;
        break;
    case RXS_PAYLOAD:
        app->rx_buf[app->rx_idx++] = c;
        app->rx_crc = ha_crc8_upd(app->rx_crc, c);
        if(app->rx_idx >= app->rx_len) app->rx_state = RXS_CRC;
        break;
    case RXS_CRC:
        if(c == app->rx_crc) dispatch_frame(app);
        app->rx_state = RXS_SYNC;
        break;
    default:
        app->rx_state = RXS_SYNC;
        break;
    }
}

// ---------------- public RX / liveness ----------------

void ha_session_rx(HotspotArcadeApp* app) {
    uint8_t buf[128];
    size_t n;
    bool got = false;
    while((n = ha_uart_rx(app->uart, buf, sizeof(buf))) > 0) {
        for(size_t i = 0; i < n; i++) rx_byte(app, buf[i]);
        got = true;
    }
    if(got) {
        app->last_rx_tick = furi_get_tick();
        app->link_lost = false;
    }
}

bool ha_board_present(HotspotArcadeApp* app, uint32_t wait_ms) {
    if(furi_get_tick() - app->last_rx_tick < 2500) return true;
    uint32_t deadline = furi_get_tick() + wait_ms;
    uint8_t buf[64];
    while(furi_get_tick() < deadline) {
        if(ha_uart_rx(app->uart, buf, sizeof(buf)) > 0) {
            app->last_rx_tick = furi_get_tick();
            return true;
        }
        furi_delay_ms(20);
    }
    return false;
}
