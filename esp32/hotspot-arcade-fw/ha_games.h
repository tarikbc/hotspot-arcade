// Hotspot Arcade game engine (ESP side, real-time referee).
// Owns the player roster and the authoritative live state for the active game.
// Header-only, included exactly once by the .ino (single translation unit), so
// it may define freely. It talks to the outside world only through the sink
// functions below, which the .ino implements (WS send, UART report).
#pragma once
#include <Arduino.h>
#include "ha_json.h"
#include "ha_proto.h"

#define HA_MAX_PLAYERS 12
#define HA_NICK_LEN 20
#define C4_COLS 7
#define C4_ROWS 6
#define C4_CELLS (C4_COLS * C4_ROWS)
#define C4_MAX_MATCHES 6
#define C4_MAX_CHALLENGES 16

// ---- sinks implemented in the .ino ----
void haWsSendWs(uint32_t wsId, const String& msg); // to one socket (0 = no-op)
void haWsBroadcast(const String& msg); // to all connected sockets
void haUartJoin(uint8_t pid, const char* nick);
void haUartLeave(uint8_t pid);
void haUartScore(uint8_t pid, int delta, const char* reason);
void haUartEvent(const String& json);
void haUartRoundResult(const String& json);

struct Player {
    bool used;
    uint32_t wsId; // 0 = not connected
    char nick[HA_NICK_LEN];
    int32_t score;
};

struct Trivia {
    uint8_t phase; // 0 idle, 1 question, 2 reveal
    int qi;
    String q;
    String opts[4];
    int correct;
    int dur; // seconds
    uint32_t deadline; // millis
    int8_t answer[HA_MAX_PLAYERS + 1]; // per-pid choice, -1 none
    uint32_t answerMs[HA_MAX_PLAYERS + 1];
    int counts[4];
};

struct C4Match {
    bool used;
    uint8_t a, b; // pids; a plays disc 1, b plays disc 2
    bool aIn, bIn; // still attached (not returned to lobby)
    uint8_t board[C4_CELLS]; // row-major, index=row*7+col, row 0 top; 0/1/2
    uint8_t turn; // pid to move
    uint8_t phase; // 1 playing, 2 over
    uint8_t winner; // pid, or 0 for draw
};

struct C4Challenge {
    bool used;
    uint8_t from, to;
};

class Engine {
public:
    void reset() {
        for(int i = 0; i <= HA_MAX_PLAYERS; i++) _p[i] = Player{};
        _active = HA_GAME_NONE;
        triviaClear();
        c4Clear();
    }

    // ---- roster ----
    uint8_t pidByWs(uint32_t wsId) {
        if(!wsId) return 0;
        for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++)
            if(_p[i].used && _p[i].wsId == wsId) return i;
        return 0;
    }

    void onWsDisconnect(uint32_t wsId) {
        uint8_t pid = pidByWs(wsId);
        if(!pid) return;
        c4OnLeave(pid); // forfeit any active match
        _p[pid] = Player{};
        haUartLeave(pid);
        pushAll();
    }

    void onHello(uint32_t wsId, const char* nick) {
        uint8_t pid = pidByWs(wsId);
        if(!pid) {
            pid = freePid();
            if(!pid) return; // full
            _p[pid].used = true;
            _p[pid].wsId = wsId;
            _p[pid].score = 0;
            strlcpy(_p[pid].nick, (nick && nick[0]) ? nick : "Player", HA_NICK_LEN);
            haUartJoin(pid, _p[pid].nick);
        } else {
            if(nick && nick[0]) strlcpy(_p[pid].nick, nick, HA_NICK_LEN);
        }
        String w = String("{\"t\":\"welcome\",\"pid\":") + pid + ",\"nick\":\"" +
                   ha_json_escape(_p[pid].nick) + "\"}";
        haWsSendWs(wsId, w);
        pushAll();
    }

    // ---- host (Flipper) driven ----
    void selectGame(uint8_t id) {
        _active = id;
        triviaClear();
        c4Clear();
        pushAll();
    }

    void resetScores() {
        for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++)
            if(_p[i].used) _p[i].score = 0;
        pushAll();
    }

    void onQuestion(const char* json) {
        if(_active != HA_GAME_TRIVIA) _active = HA_GAME_TRIVIA;
        Trivia& t = _t;
        int v;
        char buf[256];
        t.qi = ha_json_int(json, "i", &v) ? v : (t.qi + 1);
        if(ha_json_str(json, "q", buf, sizeof(buf))) t.q = buf;
        for(int k = 0; k < 4; k++) t.opts[k] = "";
        // options array "o":["a","b","c","d"] — pull each quoted item in order
        parseOptions(json, t.opts);
        t.correct = ha_json_int(json, "c", &v) ? v : 0;
        t.dur = ha_json_int(json, "dur", &v) ? v : 20;
        if(t.dur < 3) t.dur = 3;
        t.phase = 1;
        t.deadline = millis() + (uint32_t)t.dur * 1000;
        for(int i = 0; i <= HA_MAX_PLAYERS; i++) {
            t.answer[i] = -1;
            t.answerMs[i] = 0;
        }
        for(int k = 0; k < 4; k++) t.counts[k] = 0;
        pushAll();
        haUartEvent(String("{\"answers\":0,\"total\":") + connectedCount() + "}");
    }

    void reveal() {
        if(_active != HA_GAME_TRIVIA || _t.phase != 1) return;
        _t.phase = 2;
        String correctList = "[";
        bool first = true;
        for(uint8_t pid = 1; pid <= HA_MAX_PLAYERS; pid++) {
            if(!_p[pid].used || _t.answer[pid] < 0) continue;
            if(_t.answer[pid] == _t.correct) {
                int pts = triviaPoints(_t.answerMs[pid]);
                _p[pid].score += pts;
                haUartScore(pid, pts, "trivia");
                if(!first) correctList += ",";
                correctList += pid;
                first = false;
            }
        }
        correctList += "]";
        haUartRoundResult(String("{\"correct\":") + correctList + "}");
        pushAll();
    }

    void roundEnd() {
        if(_active == HA_GAME_TRIVIA) {
            triviaClear();
        } else if(_active == HA_GAME_CONNECT4) {
            c4Clear();
        }
        pushAll();
    }

    // ---- player input (parsed WS JSON) ----
    void onInput(uint32_t wsId, const char* json) {
        char type[20];
        if(!ha_json_str(json, "t", type, sizeof(type))) return;
        if(strcmp(type, "hello") == 0) {
            char nick[HA_NICK_LEN];
            ha_json_str(json, "nick", nick, sizeof(nick));
            onHello(wsId, nick);
            return;
        }
        if(strcmp(type, "ping") == 0) {
            haWsSendWs(wsId, "{\"t\":\"pong\"}");
            return;
        }
        uint8_t pid = pidByWs(wsId);
        if(!pid) return;
        int v;
        if(strcmp(type, "answer") == 0 && ha_json_int(json, "c", &v)) {
            triviaAnswer(pid, v);
        } else if(strcmp(type, "challenge") == 0 && ha_json_int(json, "to", &v)) {
            c4Challenge(pid, (uint8_t)v);
        } else if(strcmp(type, "accept") == 0 && ha_json_int(json, "from", &v)) {
            c4Accept(pid, (uint8_t)v);
        } else if(strcmp(type, "cancel") == 0) {
            c4Cancel(pid);
        } else if(strcmp(type, "move") == 0 && ha_json_int(json, "col", &v)) {
            c4Move(pid, v);
        } else if(strcmp(type, "leaveGame") == 0) {
            c4OnLeave(pid);
            pushAll();
        }
    }

private:
    Player _p[HA_MAX_PLAYERS + 1] = {};
    uint8_t _active = HA_GAME_NONE;
    Trivia _t = {};
    C4Match _m[C4_MAX_MATCHES] = {};
    C4Challenge _c[C4_MAX_CHALLENGES] = {};

    uint8_t freePid() {
        for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++)
            if(!_p[i].used) return i;
        return 0;
    }
    int connectedCount() {
        int n = 0;
        for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++)
            if(_p[i].used) n++;
        return n;
    }

    // ---------- broadcast ----------
    void pushAll() {
        String lob = lobbyJson();
        for(uint8_t pid = 1; pid <= HA_MAX_PLAYERS; pid++) {
            if(!_p[pid].used || !_p[pid].wsId) continue;
            haWsSendWs(_p[pid].wsId, lob);
            if(_active == HA_GAME_TRIVIA)
                haWsSendWs(_p[pid].wsId, triviaJson(pid));
            else if(_active == HA_GAME_CONNECT4)
                haWsSendWs(_p[pid].wsId, c4Json(pid));
        }
    }

    String playersJson() {
        String s = "[";
        bool first = true;
        for(uint8_t pid = 1; pid <= HA_MAX_PLAYERS; pid++) {
            if(!_p[pid].used) continue;
            if(!first) s += ",";
            s += "{\"pid\":";
            s += pid;
            s += ",\"nick\":\"";
            s += ha_json_escape(_p[pid].nick);
            s += "\",\"score\":";
            s += _p[pid].score;
            s += "}";
            first = false;
        }
        s += "]";
        return s;
    }

    String lobbyJson() {
        const char* g = _active == HA_GAME_TRIVIA ? "trivia" :
                        _active == HA_GAME_CONNECT4 ? "connect4" :
                                                      "none";
        return String("{\"t\":\"lobby\",\"game\":\"") + g + "\",\"players\":" + playersJson() + "}";
    }

    // ---------- trivia ----------
    void triviaClear() {
        _t.phase = 0;
        _t.q = "";
        for(int k = 0; k < 4; k++) {
            _t.opts[k] = "";
            _t.counts[k] = 0;
        }
        for(int i = 0; i <= HA_MAX_PLAYERS; i++) {
            _t.answer[i] = -1;
            _t.answerMs[i] = 0;
        }
    }

    // Pull the four strings of "o":[...] in order into opts[4].
    static void parseOptions(const char* json, String opts[4]) {
        const char* q = ha_json_find(json, "o");
        if(!q || *q != '[') return;
        q++;
        for(int k = 0; k < 4 && *q; k++) {
            while(*q == ' ' || *q == ',') q++;
            if(*q != '"') break;
            q++;
            String s;
            while(*q && *q != '"') {
                if(*q == '\\' && q[1]) {
                    q++;
                    s += *q;
                } else {
                    s += *q;
                }
                q++;
            }
            opts[k] = s;
            if(*q == '"') q++;
        }
    }

    int triviaPoints(uint32_t answeredAt) {
        uint32_t start = _t.deadline - (uint32_t)_t.dur * 1000;
        long elapsed = (long)answeredAt - (long)start;
        long total = (long)_t.dur * 1000;
        if(elapsed < 0) elapsed = 0;
        if(elapsed > total) elapsed = total;
        int bonus = (int)(500L * (total - elapsed) / (total ? total : 1));
        return 500 + bonus;
    }

    void triviaAnswer(uint8_t pid, int c) {
        if(_active != HA_GAME_TRIVIA || _t.phase != 1) return;
        if(c < 0 || c > 3) return;
        if(millis() > _t.deadline) return;
        if(_t.answer[pid] >= 0) return; // already answered
        _t.answer[pid] = (int8_t)c;
        _t.answerMs[pid] = millis();
        _t.counts[c]++;
        int answered = 0;
        for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++)
            if(_p[i].used && _t.answer[i] >= 0) answered++;
        haUartEvent(String("{\"answers\":") + answered + ",\"total\":" + connectedCount() + "}");
        pushAll();
    }

    String triviaJson(uint8_t pid) {
        const char* phase = _t.phase == 1 ? "question" : _t.phase == 2 ? "reveal" : "idle";
        String s = String("{\"t\":\"trivia\",\"phase\":\"") + phase + "\"";
        if(_t.phase != 0) {
            s += ",\"i\":";
            s += _t.qi;
            s += ",\"q\":\"";
            s += ha_json_escape(_t.q.c_str());
            s += "\",\"o\":[";
            for(int k = 0; k < 4; k++) {
                if(k) s += ",";
                s += "\"";
                s += ha_json_escape(_t.opts[k].c_str());
                s += "\"";
            }
            s += "],\"dur\":";
            s += _t.dur;
            s += ",\"deadline\":";
            s += _t.deadline;
            s += ",\"mine\":";
            s += _t.answer[pid];
            s += ",\"counts\":[";
            for(int k = 0; k < 4; k++) {
                if(k) s += ",";
                s += _t.counts[k];
            }
            s += "]";
            if(_t.phase == 2) {
                s += ",\"correct\":";
                s += _t.correct;
            }
        }
        s += ",\"scores\":" + playersJson() + "}";
        return s;
    }

    // ---------- connect4 ----------
    void c4Clear() {
        for(int i = 0; i < C4_MAX_MATCHES; i++) _m[i] = C4Match{};
        for(int i = 0; i < C4_MAX_CHALLENGES; i++) _c[i] = C4Challenge{};
    }

    C4Match* matchOf(uint8_t pid) {
        for(int i = 0; i < C4_MAX_MATCHES; i++) {
            if(!_m[i].used) continue;
            if(_m[i].a == pid && _m[i].aIn) return &_m[i];
            if(_m[i].b == pid && _m[i].bIn) return &_m[i];
        }
        return nullptr;
    }

    void c4RemoveChallengesInvolving(uint8_t pid) {
        for(int i = 0; i < C4_MAX_CHALLENGES; i++)
            if(_c[i].used && (_c[i].from == pid || _c[i].to == pid)) _c[i] = C4Challenge{};
    }

    void c4Challenge(uint8_t from, uint8_t to) {
        if(_active != HA_GAME_CONNECT4) return;
        if(to == from || to < 1 || to > HA_MAX_PLAYERS || !_p[to].used) return;
        if(matchOf(from) || matchOf(to)) return;
        // one outstanding challenge per challenger
        for(int i = 0; i < C4_MAX_CHALLENGES; i++)
            if(_c[i].used && _c[i].from == from) _c[i] = C4Challenge{};
        for(int i = 0; i < C4_MAX_CHALLENGES; i++) {
            if(!_c[i].used) {
                _c[i] = C4Challenge{true, from, to};
                break;
            }
        }
        if(_p[to].wsId)
            haWsSendWs(
                _p[to].wsId,
                String("{\"t\":\"toast\",\"msg\":\"") + ha_json_escape(_p[from].nick) +
                    " challenges you\"}");
        pushAll();
    }

    void c4Accept(uint8_t pid, uint8_t from) {
        if(_active != HA_GAME_CONNECT4) return;
        bool found = false;
        for(int i = 0; i < C4_MAX_CHALLENGES; i++)
            if(_c[i].used && _c[i].from == from && _c[i].to == pid) found = true;
        if(!found) return;
        if(matchOf(pid) || matchOf(from)) return;
        for(int i = 0; i < C4_MAX_MATCHES; i++) {
            if(!_m[i].used) {
                _m[i] = C4Match{};
                _m[i].used = true;
                _m[i].a = from;
                _m[i].b = pid;
                _m[i].aIn = _m[i].bIn = true;
                memset(_m[i].board, 0, sizeof(_m[i].board));
                _m[i].turn = from; // challenger moves first
                _m[i].phase = 1;
                _m[i].winner = 0;
                break;
            }
        }
        c4RemoveChallengesInvolving(pid);
        c4RemoveChallengesInvolving(from);
        haUartEvent(
            String("{\"c4\":\"") + ha_json_escape(_p[from].nick) + " vs " +
            ha_json_escape(_p[pid].nick) + "\"}");
        pushAll();
    }

    void c4Cancel(uint8_t pid) {
        c4RemoveChallengesInvolving(pid);
        pushAll();
    }

    void c4Move(uint8_t pid, int col) {
        C4Match* m = matchOf(pid);
        if(!m || m->phase != 1 || m->turn != pid) return;
        if(col < 0 || col >= C4_COLS) return;
        int row = -1;
        for(int r = C4_ROWS - 1; r >= 0; r--) {
            if(m->board[r * C4_COLS + col] == 0) {
                row = r;
                break;
            }
        }
        if(row < 0) return; // column full
        uint8_t disc = (pid == m->a) ? 1 : 2;
        m->board[row * C4_COLS + col] = disc;
        if(c4Wins(m->board, row, col, disc)) {
            c4Finish(m, pid);
        } else if(c4Full(m->board)) {
            c4Finish(m, 0);
        } else {
            m->turn = (pid == m->a) ? m->b : m->a;
        }
        pushAll();
    }

    void c4Finish(C4Match* m, uint8_t winnerPid) {
        if(m->phase != 1) return;
        m->phase = 2;
        m->winner = winnerPid;
        uint8_t loser = 0;
        if(winnerPid == m->a) loser = m->b;
        else if(winnerPid == m->b) loser = m->a;
        if(winnerPid) {
            _p[winnerPid].score += 300;
            haUartScore(winnerPid, 300, "c4win");
            haUartRoundResult(String("{\"win\":") + winnerPid + ",\"lose\":" + loser + "}");
        } else {
            haUartRoundResult(String("{\"draw\":[") + m->a + "," + m->b + "]}");
        }
    }

    // A player returns to lobby or disconnects. Forfeit a live match to opponent.
    void c4OnLeave(uint8_t pid) {
        C4Match* m = matchOf(pid);
        if(!m) return;
        uint8_t opp = (pid == m->a) ? m->b : m->a;
        if(m->phase == 1) c4Finish(m, opp); // forfeit
        if(pid == m->a) m->aIn = false;
        if(pid == m->b) m->bIn = false;
        if(!m->aIn && !m->bIn) *m = C4Match{}; // both gone: free the slot
    }

    static bool c4Full(const uint8_t* b) {
        for(int c = 0; c < C4_COLS; c++)
            if(b[c] == 0) return false; // top row cell empty -> not full
        return true;
    }

    static bool c4Wins(const uint8_t* b, int row, int col, uint8_t disc) {
        static const int dr[4] = {0, 1, 1, 1};
        static const int dc[4] = {1, 0, 1, -1};
        for(int d = 0; d < 4; d++) {
            int cnt = 1;
            for(int s = 1; s <= 3; s++) {
                int r = row + dr[d] * s, c = col + dc[d] * s;
                if(r < 0 || r >= C4_ROWS || c < 0 || c >= C4_COLS) break;
                if(b[r * C4_COLS + c] != disc) break;
                cnt++;
            }
            for(int s = 1; s <= 3; s++) {
                int r = row - dr[d] * s, c = col - dc[d] * s;
                if(r < 0 || r >= C4_ROWS || c < 0 || c >= C4_COLS) break;
                if(b[r * C4_COLS + c] != disc) break;
                cnt++;
            }
            if(cnt >= 4) return true;
        }
        return false;
    }

    String c4ChallengesJson() {
        String s = "[";
        bool first = true;
        for(int i = 0; i < C4_MAX_CHALLENGES; i++) {
            if(!_c[i].used) continue;
            if(!first) s += ",";
            s += "{\"from\":";
            s += _c[i].from;
            s += ",\"to\":";
            s += _c[i].to;
            s += "}";
            first = false;
        }
        s += "]";
        return s;
    }

    String c4Json(uint8_t pid) {
        C4Match* m = matchOf(pid);
        if(!m) {
            return String("{\"t\":\"c4\",\"phase\":\"lobby\",\"challenges\":") + c4ChallengesJson() +
                   "}";
        }
        uint8_t opp = (pid == m->a) ? m->b : m->a;
        uint8_t me = (pid == m->a) ? 1 : 2;
        String s = "{\"t\":\"c4\",\"phase\":\"";
        s += (m->phase == 2) ? "over" : "playing";
        s += "\",\"board\":[";
        for(int i = 0; i < C4_CELLS; i++) {
            if(i) s += ",";
            s += m->board[i];
        }
        s += "],\"turn\":";
        s += m->turn;
        s += ",\"me\":";
        s += me;
        s += ",\"you\":";
        s += pid;
        s += ",\"opp\":\"";
        s += ha_json_escape(_p[opp].nick);
        s += "\"";
        if(m->phase == 2) {
            const char* r = (m->winner == 0) ? "draw" : (m->winner == pid) ? "win" : "lose";
            s += ",\"result\":\"";
            s += r;
            s += "\"";
        }
        s += "}";
        return s;
    }
};
