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

// Duels (connect4 / tic-tac-toe / dots-and-boxes) share one match + challenge
// system, parameterized by the active game's kind. Only one game is active at a
// time, so all live matches are the active kind.
#define DUEL_MAX_CELLS 42 // c4 = 7x6; ttt = 3x3
#define DUEL_MAX_MATCHES 6
#define DUEL_MAX_CHALLENGES 16
#define DOTS_W 5 // boxes across
#define DOTS_H 5 // boxes down
#define DOTS_HEDGES ((DOTS_H + 1) * DOTS_W) // horizontal edges
#define DOTS_VEDGES (DOTS_H * (DOTS_W + 1)) // vertical edges
#define DOTS_BOXES (DOTS_W * DOTS_H)

#define TRIVIA_MAX_TOPICS 6
#define TRIVIA_MAX_QS 20
#define TRIVIA_QDUR 20 // seconds per question (safety timer)
#define TRIVIA_COUNTDOWN 5 // seconds after all-ready before the first question
#define TRIVIA_REVEAL_MS 4000 // pause on the reveal before the next question

#define DRAW_SECS 70 // per drawing round
#define DRAW_REVEAL_MS 4000 // reveal pause before the next round
#define PONG_MAX 4 // concurrent pong matches
#define PONG_WIN 5 // points to win
#define PONG_TICK_MS 33 // ~30 Hz

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

// Trivia content, streamed from the Flipper at session start (the packs become
// the votable topics), then owned by the ESP which orchestrates the whole game.
struct TriviaQ {
    String q;
    String o[4];
    uint8_t correct;
};
struct TriviaTopic {
    String name;
    TriviaQ qs[TRIVIA_MAX_QS];
    uint8_t qcount;
};

struct Trivia {
    uint8_t phase; // 0 lobby, 1 countdown, 2 question, 3 reveal, 4 final
    bool ready[HA_MAX_PLAYERS + 1];
    int8_t vote[HA_MAX_PLAYERS + 1]; // topic index, -1 = none
    uint32_t countdownEnd;
    int lastSec; // last countdown second broadcast
    uint8_t topic; // chosen topic index
    int qi; // current question index
    int8_t answer[HA_MAX_PLAYERS + 1];
    uint32_t answerMs[HA_MAX_PLAYERS + 1];
    int gained[HA_MAX_PLAYERS + 1]; // points earned on the current question
    int counts[4];
    uint32_t deadline; // question end
    uint32_t revealUntil;
};

struct DuelMatch {
    bool used;
    uint8_t kind; // HA_GAME_CONNECT4 / TICTACTOE / DOTS
    uint8_t a, b; // pids; a plays mark 1, b plays mark 2
    bool aIn, bIn; // still attached (not returned to lobby)
    uint8_t turn; // pid to move
    uint8_t phase; // 1 playing, 2 over
    uint8_t winner; // pid, or 0 for draw
    uint8_t first; // who moved first (rematch alternates it)
    uint8_t board[DUEL_MAX_CELLS]; // grid games (c4/ttt), row-major; 0/1/2
    uint8_t hedges[DOTS_HEDGES]; // dots: horizontal edges drawn (0/1)
    uint8_t vedges[DOTS_VEDGES]; // dots: vertical edges drawn (0/1)
    uint8_t boxes[DOTS_BOXES]; // dots: box owner (0/1/2)
    uint8_t sA, sB; // dots: box counts for a / b
};

struct DuelChallenge {
    bool used;
    uint8_t from, to;
};

struct DrawState {
    uint8_t phase; // 0 idle, 1 draw, 2 reveal
    uint8_t drawer; // pid currently drawing
    uint8_t drawerSeq; // rotates the drawer
    uint16_t wordSeq; // rotates the word
    char word[24];
    int round;
    uint32_t deadline; // millis (draw end)
    uint32_t revealUntil; // millis (reveal end)
    uint8_t winner; // pid who guessed it, or 0
};

struct PongMatch {
    bool used;
    uint8_t a, b; // a = left paddle, b = right paddle
    bool aIn, bIn;
    uint8_t phase; // 1 playing, 2 over
    float bx, by, vx, vy; // ball position (0..1) + velocity per tick
    float p1, p2; // paddle centers (0..1)
    int8_t d1, d2; // paddle move dir (-1/0/1)
    uint8_t s1, s2; // scores
    uint8_t winner; // pid
};

class Engine {
public:
    void reset() {
        for(int i = 0; i <= HA_MAX_PLAYERS; i++) _p[i] = Player{};
        _active = HA_GAME_NONE;
        triviaClear();
        duelClear();
        drawClear();
        pongClear();
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
        anyOnLeave(pid); // forfeit any active match
        _p[pid] = Player{};
        haUartLeave(pid);
        triviaOnRosterChange();
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
        triviaOnRosterChange();
        pushAll();
    }

    // ---- host (Flipper) driven ----
    void selectGame(uint8_t id) {
        _active = id;
        triviaClear();
        duelClear();
        drawClear();
        pongClear();
        pushAll();
    }

    void resetScores() {
        for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++)
            if(_p[i].used) _p[i].score = 0;
        pushAll();
    }

    // ---- trivia content streamed from the Flipper (packs -> votable topics) ----
    void triviaTopicsClear() {
        for(int i = 0; i < TRIVIA_MAX_TOPICS; i++) _topics[i] = TriviaTopic{};
        _topicCount = 0;
    }
    void triviaAddTopic(const char* name) {
        if(_topicCount >= TRIVIA_MAX_TOPICS) return;
        _topics[_topicCount] = TriviaTopic{};
        _topics[_topicCount].name = name;
        _topics[_topicCount].qcount = 0;
        _topicCount++;
    }
    void triviaAddQ(const char* json) {
        if(_topicCount == 0) return;
        TriviaTopic& tp = _topics[_topicCount - 1];
        if(tp.qcount >= TRIVIA_MAX_QS) return;
        TriviaQ& q = tp.qs[tp.qcount];
        char buf[200];
        q.q = ha_json_str(json, "q", buf, sizeof(buf)) ? buf : "";
        String opts[4];
        parseOptions(json, opts);
        for(int k = 0; k < 4; k++) q.o[k] = opts[k];
        int v;
        q.correct = ha_json_int(json, "c", &v) ? (uint8_t)v : 0;
        tp.qcount++;
    }

    void roundEnd() {
        if(_active == HA_GAME_TRIVIA)
            triviaClear();
        else if(isDuel(_active))
            duelClear();
        else if(_active == HA_GAME_DRAW)
            drawClear();
        else if(_active == HA_GAME_PONG)
            pongClear();
        pushAll();
    }

    // Time-based updates (trivia phases, drawing timers, pong physics). From loop().
    void tick(uint32_t now) {
        if(_active == HA_GAME_TRIVIA)
            triviaTick(now);
        else if(_active == HA_GAME_DRAW)
            drawTick(now);
        else if(_active == HA_GAME_PONG && (now - _lastPong) >= PONG_TICK_MS) {
            _lastPong = now;
            pongTick();
        }
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
        } else if(strcmp(type, "ready") == 0) {
            const char* rp = ha_json_find(json, "ready");
            triviaReady(pid, rp && strncmp(rp, "true", 4) == 0);
        } else if(strcmp(type, "vote") == 0 && ha_json_int(json, "topic", &v)) {
            triviaVote(pid, v);
        } else if(strcmp(type, "again") == 0) {
            triviaAgain(pid);
        } else if(strcmp(type, "challenge") == 0 && ha_json_int(json, "to", &v)) {
            matchChallenge(pid, (uint8_t)v);
        } else if(strcmp(type, "accept") == 0 && ha_json_int(json, "from", &v)) {
            matchAccept(pid, (uint8_t)v);
        } else if(strcmp(type, "cancel") == 0) {
            duelCancel(pid);
        } else if(strcmp(type, "move") == 0 && ha_json_int(json, "n", &v)) {
            duelMove(pid, v);
        } else if(strcmp(type, "rematch") == 0) {
            duelRematch(pid);
        } else if(strcmp(type, "paddle") == 0 && ha_json_int(json, "dir", &v)) {
            pongPaddle(pid, v);
        } else if(strcmp(type, "guess") == 0) {
            char g[64];
            if(ha_json_str(json, "text", g, sizeof(g))) drawGuess(pid, g);
        } else if(strcmp(type, "stroke") == 0) {
            drawStroke(pid, json);
        } else if(strcmp(type, "clear") == 0) {
            drawClearInk(pid);
        } else if(strcmp(type, "leaveGame") == 0) {
            anyOnLeave(pid);
            pushAll();
        }
    }

private:
    Player _p[HA_MAX_PLAYERS + 1] = {};
    uint8_t _active = HA_GAME_NONE;
    Trivia _t = {};
    TriviaTopic _topics[TRIVIA_MAX_TOPICS] = {};
    uint8_t _topicCount = 0;
    DuelMatch _m[DUEL_MAX_MATCHES] = {};
    DuelChallenge _c[DUEL_MAX_CHALLENGES] = {};
    DrawState _d = {};
    PongMatch _pm[PONG_MAX] = {};
    uint32_t _lastPong = 0;

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
            else if(isDuel(_active))
                haWsSendWs(_p[pid].wsId, duelJson(pid));
            else if(_active == HA_GAME_DRAW)
                haWsSendWs(_p[pid].wsId, drawJson(pid));
            else if(_active == HA_GAME_PONG)
                haWsSendWs(_p[pid].wsId, pongJson(pid));
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

    static const char* gameName(uint8_t g) {
        switch(g) {
        case HA_GAME_TRIVIA:
            return "trivia";
        case HA_GAME_CONNECT4:
            return "connect4";
        case HA_GAME_TICTACTOE:
            return "tictactoe";
        case HA_GAME_DOTS:
            return "dots";
        case HA_GAME_DRAW:
            return "draw";
        case HA_GAME_PONG:
            return "pong";
        default:
            return "none";
        }
    }

    String lobbyJson() {
        return String("{\"t\":\"lobby\",\"game\":\"") + gameName(_active) +
               "\",\"players\":" + playersJson() + "}";
    }

    static bool isDuel(uint8_t g) {
        return g == HA_GAME_CONNECT4 || g == HA_GAME_TICTACTOE || g == HA_GAME_DOTS;
    }

    // ---------- trivia (phone-driven, self-organizing) ----------
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

    void triviaClear() {
        _t.phase = 0; // lobby
        for(int i = 0; i <= HA_MAX_PLAYERS; i++) {
            _t.ready[i] = false;
            _t.vote[i] = -1;
            _t.answer[i] = -1;
            _t.answerMs[i] = 0;
            _t.gained[i] = 0;
        }
        for(int k = 0; k < 4; k++) _t.counts[k] = 0;
        _t.qi = 0;
        _t.topic = 0;
        _t.lastSec = -1;
        for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++)
            if(_p[i].used) _p[i].score = 0;
    }

    bool triviaAllReady() {
        int n = 0;
        for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++) {
            if(!_p[i].used) continue;
            n++;
            if(!_t.ready[i]) return false;
        }
        return n >= 1;
    }

    bool triviaAllAnswered() {
        int n = 0;
        for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++) {
            if(!_p[i].used) continue;
            n++;
            if(_t.answer[i] < 0) return false;
        }
        return n >= 1;
    }

    void triviaCheckStart() {
        if(_active != HA_GAME_TRIVIA) return;
        if(_t.phase == 0 && _topicCount > 0 && triviaAllReady()) {
            _t.phase = 1; // all ready -> countdown
            _t.countdownEnd = millis() + (uint32_t)TRIVIA_COUNTDOWN * 1000;
            _t.lastSec = -1;
        } else if(_t.phase == 1 && !triviaAllReady()) {
            _t.phase = 0; // someone unreadied / a new player joined -> cancel
        }
    }

    void triviaOnRosterChange() {
        if(_active != HA_GAME_TRIVIA) return;
        triviaCheckStart();
        if(_t.phase == 2 && triviaAllAnswered()) triviaDoReveal();
    }

    void triviaReady(uint8_t pid, bool r) {
        if(_active != HA_GAME_TRIVIA || (_t.phase != 0 && _t.phase != 1)) return;
        _t.ready[pid] = r;
        triviaCheckStart();
        pushAll();
    }

    void triviaVote(uint8_t pid, int topic) {
        if(_active != HA_GAME_TRIVIA || _t.phase != 0) return;
        if(topic < 0 || topic >= _topicCount) return;
        _t.vote[pid] = (int8_t)topic;
        pushAll();
    }

    int triviaWinningTopic() {
        int votes[TRIVIA_MAX_TOPICS] = {0};
        int total = 0;
        for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++)
            if(_p[i].used && _t.vote[i] >= 0 && _t.vote[i] < _topicCount) {
                votes[_t.vote[i]]++;
                total++;
            }
        if(total == 0) return (int)random(_topicCount);
        int best = 0;
        for(int i = 1; i < _topicCount; i++)
            if(votes[i] > votes[best]) best = i;
        int tie[TRIVIA_MAX_TOPICS], tn = 0;
        for(int i = 0; i < _topicCount; i++)
            if(votes[i] == votes[best]) tie[tn++] = i;
        return tie[(int)random(tn)];
    }

    void triviaStartQuestion() {
        _t.phase = 2;
        _t.deadline = millis() + (uint32_t)TRIVIA_QDUR * 1000;
        for(int i = 0; i <= HA_MAX_PLAYERS; i++) {
            _t.answer[i] = -1;
            _t.answerMs[i] = 0;
            _t.gained[i] = 0;
        }
        for(int k = 0; k < 4; k++) _t.counts[k] = 0;
        pushAll();
    }

    void triviaBeginGame() {
        _t.topic = (uint8_t)triviaWinningTopic();
        _t.qi = 0;
        triviaStartQuestion();
    }

    int triviaPoints(uint32_t answeredAt) {
        uint32_t start = _t.deadline - (uint32_t)TRIVIA_QDUR * 1000;
        long elapsed = (long)answeredAt - (long)start;
        long total = (long)TRIVIA_QDUR * 1000;
        if(elapsed < 0) elapsed = 0;
        if(elapsed > total) elapsed = total;
        int bonus = (int)(500L * (total - elapsed) / (total ? total : 1));
        return 500 + bonus;
    }

    void triviaAnswer(uint8_t pid, int c) {
        if(_active != HA_GAME_TRIVIA || _t.phase != 2) return;
        if(c < 0 || c > 3 || _t.answer[pid] >= 0 || millis() > _t.deadline) return;
        _t.answer[pid] = (int8_t)c;
        _t.answerMs[pid] = millis();
        _t.counts[c]++;
        if(triviaAllAnswered())
            triviaDoReveal();
        else
            pushAll();
    }

    void triviaDoReveal() {
        _t.phase = 3;
        uint8_t correct = _topics[_t.topic].qs[_t.qi].correct;
        for(uint8_t pid = 1; pid <= HA_MAX_PLAYERS; pid++) {
            if(!_p[pid].used || _t.answer[pid] < 0) continue;
            if(_t.answer[pid] == correct) {
                int pts = triviaPoints(_t.answerMs[pid]);
                _p[pid].score += pts;
                _t.gained[pid] = pts;
                haUartScore(pid, pts, "trivia");
            }
        }
        _t.revealUntil = millis() + TRIVIA_REVEAL_MS;
        pushAll();
    }

    void triviaNext() {
        _t.qi++;
        if(_t.qi >= _topics[_t.topic].qcount) {
            _t.phase = 4; // final
            haUartRoundResult("{\"trivia\":\"final\"}");
            pushAll();
        } else {
            triviaStartQuestion();
        }
    }

    void triviaAgain(uint8_t pid) {
        (void)pid;
        if(_active != HA_GAME_TRIVIA || _t.phase != 4) return;
        triviaClear(); // back to the lobby (scores reset)
        pushAll();
    }

    void triviaTick(uint32_t now) {
        if(_t.phase == 1) { // countdown
            if(now >= _t.countdownEnd) {
                triviaBeginGame();
                return;
            }
            int secs = (int)((_t.countdownEnd - now + 999) / 1000);
            if(secs != _t.lastSec) {
                _t.lastSec = secs;
                pushAll(); // client shows the new second + plays a tick
            }
        } else if(_t.phase == 2) { // question
            if(now > _t.deadline) triviaDoReveal();
        } else if(_t.phase == 3) { // reveal
            if(now > _t.revealUntil) triviaNext();
        }
    }

    // Leaderboard: connected players sorted by score desc.
    String triviaBoard() {
        uint8_t order[HA_MAX_PLAYERS];
        int n = 0;
        for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++)
            if(_p[i].used) order[n++] = i;
        for(int a = 0; a < n; a++)
            for(int b = a + 1; b < n; b++)
                if(_p[order[b]].score > _p[order[a]].score) {
                    uint8_t t = order[a];
                    order[a] = order[b];
                    order[b] = t;
                }
        String s = "[";
        for(int i = 0; i < n; i++) {
            if(i) s += ",";
            s += "{\"pid\":";
            s += order[i];
            s += ",\"nick\":\"";
            s += ha_json_escape(_p[order[i]].nick);
            s += "\",\"score\":";
            s += _p[order[i]].score;
            s += "}";
        }
        s += "]";
        return s;
    }

    String triviaJson(uint8_t pid) {
        if(_t.phase == 0) { // lobby: ready + topic vote
            String s = "{\"t\":\"trivia\",\"phase\":\"lobby\",\"players\":[";
            bool first = true;
            for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++) {
                if(!_p[i].used) continue;
                if(!first) s += ",";
                first = false;
                s += "{\"pid\":";
                s += i;
                s += ",\"nick\":\"";
                s += ha_json_escape(_p[i].nick);
                s += "\",\"ready\":";
                s += _t.ready[i] ? "true" : "false";
                s += "}";
            }
            s += "],\"topics\":[";
            int votes[TRIVIA_MAX_TOPICS] = {0};
            for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++)
                if(_p[i].used && _t.vote[i] >= 0 && _t.vote[i] < _topicCount) votes[_t.vote[i]]++;
            for(int i = 0; i < _topicCount; i++) {
                if(i) s += ",";
                s += "{\"name\":\"";
                s += ha_json_escape(_topics[i].name.c_str());
                s += "\",\"votes\":";
                s += votes[i];
                s += "}";
            }
            s += "],\"myvote\":";
            s += _t.vote[pid];
            s += ",\"myready\":";
            s += _t.ready[pid] ? "true" : "false";
            s += "}";
            return s;
        }
        if(_t.phase == 1) { // countdown
            uint32_t now = millis();
            int secs = (now >= _t.countdownEnd) ? 1 : (int)((_t.countdownEnd - now + 999) / 1000);
            if(secs < 1) secs = 1;
            return String("{\"t\":\"trivia\",\"phase\":\"countdown\",\"secs\":") + secs +
                   ",\"topic\":\"" + ha_json_escape(_topics[_t.topic].name.c_str()) + "\"}";
        }
        if(_t.phase == 4) { // final
            return String("{\"t\":\"trivia\",\"phase\":\"final\",\"board\":") + triviaBoard() + "}";
        }
        // question / reveal
        TriviaTopic& tp = _topics[_t.topic];
        TriviaQ& q = tp.qs[_t.qi];
        const char* phase = (_t.phase == 3) ? "reveal" : "question";
        String s = String("{\"t\":\"trivia\",\"phase\":\"") + phase + "\",\"i\":" + _t.qi +
                   ",\"n\":" + tp.qcount + ",\"q\":\"" + ha_json_escape(q.q.c_str()) + "\",\"o\":[";
        for(int k = 0; k < 4; k++) {
            if(k) s += ",";
            s += "\"";
            s += ha_json_escape(q.o[k].c_str());
            s += "\"";
        }
        s += "],\"mine\":";
        s += _t.answer[pid];
        s += ",\"topic\":\"";
        s += ha_json_escape(tp.name.c_str());
        s += "\",\"board\":" + triviaBoard();
        if(_t.phase == 2) {
            int answered = 0;
            for(uint8_t i = 1; i <= HA_MAX_PLAYERS; i++)
                if(_p[i].used && _t.answer[i] >= 0) answered++;
            s += ",\"dur\":";
            s += TRIVIA_QDUR;
            s += ",\"deadline\":";
            s += _t.deadline;
            s += ",\"answered\":";
            s += answered;
            s += ",\"total\":";
            s += connectedCount();
        } else { // reveal
            s += ",\"correct\":";
            s += q.correct;
            s += ",\"counts\":[";
            for(int k = 0; k < 4; k++) {
                if(k) s += ",";
                s += _t.counts[k];
            }
            s += "],\"gained\":";
            s += _t.gained[pid];
        }
        s += "}";
        return s;
    }

    // ---------- duels (connect4 / tic-tac-toe / dots) ----------
    void duelClear() {
        for(int i = 0; i < DUEL_MAX_MATCHES; i++) _m[i] = DuelMatch{};
        for(int i = 0; i < DUEL_MAX_CHALLENGES; i++) _c[i] = DuelChallenge{};
    }

    static const char* kindStr(uint8_t kind) {
        return kind == HA_GAME_TICTACTOE ? "ttt" : kind == HA_GAME_DOTS ? "dots" : "c4";
    }

    // Grid params for c4/ttt.
    static void gridParams(uint8_t kind, int& cols, int& rows, int& need, bool& gravity) {
        if(kind == HA_GAME_TICTACTOE) {
            cols = 3;
            rows = 3;
            need = 3;
            gravity = false;
        } else { // connect4
            cols = 7;
            rows = 6;
            need = 4;
            gravity = true;
        }
    }

    DuelMatch* matchOf(uint8_t pid) {
        for(int i = 0; i < DUEL_MAX_MATCHES; i++) {
            if(!_m[i].used) continue;
            if(_m[i].a == pid && _m[i].aIn) return &_m[i];
            if(_m[i].b == pid && _m[i].bIn) return &_m[i];
        }
        return nullptr;
    }

    void duelRemoveChallengesInvolving(uint8_t pid) {
        for(int i = 0; i < DUEL_MAX_CHALLENGES; i++)
            if(_c[i].used && (_c[i].from == pid || _c[i].to == pid)) _c[i] = DuelChallenge{};
    }

    // Challenge/accept are shared by all 1v1 games (duels + pong).
    bool isMatchGame() { return isDuel(_active) || _active == HA_GAME_PONG; }
    bool inAnyMatch(uint8_t pid) { return matchOf(pid) || pongMatchOf(pid); }

    void matchChallenge(uint8_t from, uint8_t to) {
        if(!isMatchGame()) return;
        if(to == from || to < 1 || to > HA_MAX_PLAYERS || !_p[to].used) return;
        if(inAnyMatch(from) || inAnyMatch(to)) return;
        // one outstanding challenge per challenger
        for(int i = 0; i < DUEL_MAX_CHALLENGES; i++)
            if(_c[i].used && _c[i].from == from) _c[i] = DuelChallenge{};
        for(int i = 0; i < DUEL_MAX_CHALLENGES; i++) {
            if(!_c[i].used) {
                _c[i] = DuelChallenge{true, from, to};
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

    // Set up a fresh match between a (mark 1) and b (mark 2); `first` moves first.
    void duelStart(DuelMatch* m, uint8_t a, uint8_t b, uint8_t first) {
        *m = DuelMatch{};
        m->used = true;
        m->kind = _active;
        m->a = a;
        m->b = b;
        m->aIn = m->bIn = true;
        m->turn = first;
        m->first = first;
        m->phase = 1;
        m->winner = 0;
    }

    void matchAccept(uint8_t pid, uint8_t from) {
        if(!isMatchGame()) return;
        bool found = false;
        for(int i = 0; i < DUEL_MAX_CHALLENGES; i++)
            if(_c[i].used && _c[i].from == from && _c[i].to == pid) found = true;
        if(!found) return;
        if(inAnyMatch(pid) || inAnyMatch(from)) return;
        if(_active == HA_GAME_PONG) {
            for(int i = 0; i < PONG_MAX; i++)
                if(!_pm[i].used) {
                    pongStart(&_pm[i], from, pid);
                    break;
                }
        } else {
            for(int i = 0; i < DUEL_MAX_MATCHES; i++)
                if(!_m[i].used) {
                    duelStart(&_m[i], from, pid, from); // challenger moves first
                    break;
                }
        }
        duelRemoveChallengesInvolving(pid);
        duelRemoveChallengesInvolving(from);
        const char* key = (_active == HA_GAME_PONG) ? "pong" : "duel";
        haUartEvent(
            String("{\"") + key + "\":\"" + ha_json_escape(_p[from].nick) + " vs " +
            ha_json_escape(_p[pid].nick) + "\"}");
        pushAll();
    }

    void anyOnLeave(uint8_t pid) {
        duelOnLeave(pid);
        pongOnLeave(pid);
    }

    void duelCancel(uint8_t pid) {
        duelRemoveChallengesInvolving(pid);
        pushAll();
    }

    // Rematch: in an over match, restart the same pairing with the first move
    // alternated. Only if the opponent is still attached.
    void duelRematch(uint8_t pid) {
        DuelMatch* m = matchOf(pid);
        if(!m || m->phase != 2) return;
        if(!m->aIn || !m->bIn) return; // opponent left
        uint8_t next = (m->first == m->a) ? m->b : m->a;
        duelStart(m, m->a, m->b, next);
        pushAll();
    }

    void duelMove(uint8_t pid, int n) {
        DuelMatch* m = matchOf(pid);
        if(!m || m->phase != 1 || m->turn != pid) return;
        uint8_t mark = (pid == m->a) ? 1 : 2;
        if(m->kind == HA_GAME_DOTS)
            dotsMove(m, pid, n, mark);
        else
            gridMove(m, pid, n, mark);
        pushAll();
    }

    void gridMove(DuelMatch* m, uint8_t pid, int n, uint8_t mark) {
        int cols, rows, need;
        bool gravity;
        gridParams(m->kind, cols, rows, need, gravity);
        int row, col;
        if(gravity) {
            col = n;
            if(col < 0 || col >= cols) return;
            row = -1;
            for(int r = rows - 1; r >= 0; r--)
                if(m->board[r * cols + col] == 0) {
                    row = r;
                    break;
                }
            if(row < 0) return; // column full
        } else {
            if(n < 0 || n >= cols * rows) return;
            if(m->board[n] != 0) return; // cell taken
            row = n / cols;
            col = n % cols;
        }
        m->board[row * cols + col] = mark;
        if(gridWins(m->board, cols, rows, need, row, col, mark))
            duelFinish(m, pid);
        else if(gridFull(m->board, cols, rows))
            duelFinish(m, 0);
        else
            m->turn = (pid == m->a) ? m->b : m->a;
    }

    void dotsMove(DuelMatch* m, uint8_t pid, int n, uint8_t mark) {
        if(n < 0 || n >= DOTS_HEDGES + DOTS_VEDGES) return;
        if(n < DOTS_HEDGES) {
            if(m->hedges[n]) return;
            m->hedges[n] = 1;
        } else {
            int vi = n - DOTS_HEDGES;
            if(m->vedges[vi]) return;
            m->vedges[vi] = 1;
        }
        bool claimed = false;
        for(int r = 0; r < DOTS_H; r++)
            for(int c = 0; c < DOTS_W; c++) {
                int bi = r * DOTS_W + c;
                if(m->boxes[bi]) continue;
                if(dotsBoxComplete(m, r, c)) {
                    m->boxes[bi] = mark;
                    if(mark == 1)
                        m->sA++;
                    else
                        m->sB++;
                    claimed = true;
                }
            }
        if(m->sA + m->sB >= DOTS_BOXES) {
            uint8_t w = (m->sA > m->sB) ? m->a : (m->sB > m->sA) ? m->b : 0;
            duelFinish(m, w);
        } else if(!claimed) {
            m->turn = (pid == m->a) ? m->b : m->a; // completing a box grants another turn
        }
    }

    static bool dotsBoxComplete(const DuelMatch* m, int r, int c) {
        return m->hedges[r * DOTS_W + c] && m->hedges[(r + 1) * DOTS_W + c] &&
               m->vedges[r * (DOTS_W + 1) + c] && m->vedges[r * (DOTS_W + 1) + c + 1];
    }

    void duelFinish(DuelMatch* m, uint8_t winnerPid) {
        if(m->phase != 1) return;
        m->phase = 2;
        m->winner = winnerPid;
        uint8_t loser = (winnerPid == m->a) ? m->b : (winnerPid == m->b) ? m->a : 0;
        if(winnerPid) {
            _p[winnerPid].score += 300;
            haUartScore(winnerPid, 300, "duelwin");
            haUartRoundResult(String("{\"win\":") + winnerPid + ",\"lose\":" + loser + "}");
        } else {
            haUartRoundResult(String("{\"draw\":[") + m->a + "," + m->b + "]}");
        }
    }

    // A player returns to lobby or disconnects. Forfeit a live match to opponent.
    void duelOnLeave(uint8_t pid) {
        DuelMatch* m = matchOf(pid);
        if(!m) return;
        uint8_t opp = (pid == m->a) ? m->b : m->a;
        if(m->phase == 1) duelFinish(m, opp); // forfeit
        if(pid == m->a) m->aIn = false;
        if(pid == m->b) m->bIn = false;
        if(!m->aIn && !m->bIn) *m = DuelMatch{}; // both gone: free the slot
    }

    static bool gridFull(const uint8_t* b, int cols, int rows) {
        for(int i = 0; i < cols * rows; i++)
            if(b[i] == 0) return false;
        return true;
    }

    static bool
        gridWins(const uint8_t* b, int cols, int rows, int need, int row, int col, uint8_t disc) {
        static const int dr[4] = {0, 1, 1, 1};
        static const int dc[4] = {1, 0, 1, -1};
        for(int d = 0; d < 4; d++) {
            int cnt = 1;
            for(int s = 1; s < need; s++) {
                int r = row + dr[d] * s, c = col + dc[d] * s;
                if(r < 0 || r >= rows || c < 0 || c >= cols) break;
                if(b[r * cols + c] != disc) break;
                cnt++;
            }
            for(int s = 1; s < need; s++) {
                int r = row - dr[d] * s, c = col - dc[d] * s;
                if(r < 0 || r >= rows || c < 0 || c >= cols) break;
                if(b[r * cols + c] != disc) break;
                cnt++;
            }
            if(cnt >= need) return true;
        }
        return false;
    }

    String duelChallengesJson() {
        String s = "[";
        bool first = true;
        for(int i = 0; i < DUEL_MAX_CHALLENGES; i++) {
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

    static String intArray(const uint8_t* a, int n) {
        String s = "[";
        for(int i = 0; i < n; i++) {
            if(i) s += ",";
            s += a[i];
        }
        s += "]";
        return s;
    }

    String duelJson(uint8_t pid) {
        DuelMatch* m = matchOf(pid);
        const char* kind = kindStr(_active);
        if(!m) {
            return String("{\"t\":\"duel\",\"kind\":\"") + kind +
                   "\",\"phase\":\"lobby\",\"challenges\":" + duelChallengesJson() + "}";
        }
        kind = kindStr(m->kind);
        uint8_t opp = (pid == m->a) ? m->b : m->a;
        uint8_t me = (pid == m->a) ? 1 : 2;
        const char* phase = (m->phase == 2) ? "over" : "playing";
        String s = String("{\"t\":\"duel\",\"kind\":\"") + kind + "\",\"phase\":\"" + phase +
                   "\",\"turn\":" + m->turn + ",\"me\":" + me + ",\"you\":" + pid + ",\"opp\":\"" +
                   ha_json_escape(_p[opp].nick) + "\"";
        if(m->kind == HA_GAME_DOTS) {
            s += ",\"w\":";
            s += DOTS_W;
            s += ",\"h\":";
            s += DOTS_H;
            s += ",\"hedges\":" + intArray(m->hedges, DOTS_HEDGES);
            s += ",\"vedges\":" + intArray(m->vedges, DOTS_VEDGES);
            s += ",\"boxes\":" + intArray(m->boxes, DOTS_BOXES);
            s += ",\"sme\":";
            s += (me == 1) ? m->sA : m->sB;
            s += ",\"sopp\":";
            s += (me == 1) ? m->sB : m->sA;
        } else {
            int cols, rows, need;
            bool gravity;
            gridParams(m->kind, cols, rows, need, gravity);
            s += ",\"cols\":";
            s += cols;
            s += ",\"rows\":";
            s += rows;
            s += ",\"need\":";
            s += need;
            s += ",\"gravity\":";
            s += gravity ? "true" : "false";
            s += ",\"board\":" + intArray(m->board, cols * rows);
        }
        if(m->phase == 2) {
            const char* r = (m->winner == 0) ? "draw" : (m->winner == pid) ? "win" : "lose";
            s += ",\"result\":\"";
            s += r;
            s += "\"";
        }
        s += "}";
        return s;
    }

    // ---------- drawing + guessing ----------
    static const char* drawWord(int i) {
        static const char* words[] = {
            "apple",  "house",  "car",   "tree",  "cat",     "dog",    "sun",   "star",
            "boat",   "fish",   "clock", "flower", "book",   "phone",  "chair", "heart",
            "cloud",  "key",    "shoe",  "smile", "pizza",   "robot",  "ghost", "snake",
            "crown",  "guitar", "rocket", "cake",  "moon",   "ladder"};
        const int n = sizeof(words) / sizeof(words[0]);
        return words[((i % n) + n) % n];
    }

    void drawClear() { _d = DrawState{}; }

    void drawStart(uint32_t now) {
        int used = connectedCount();
        if(used < 2) {
            _d.phase = 0;
            pushAll();
            return;
        }
        _d.drawerSeq++;
        int target = _d.drawerSeq % used, i = 0;
        uint8_t drawer = 0;
        for(uint8_t pid = 1; pid <= HA_MAX_PLAYERS; pid++)
            if(_p[pid].used) {
                if(i == target) {
                    drawer = pid;
                    break;
                }
                i++;
            }
        if(!drawer) {
            _d.phase = 0;
            return;
        }
        _d.drawer = drawer;
        strlcpy(_d.word, drawWord(_d.wordSeq++), sizeof(_d.word));
        _d.phase = 1;
        _d.round++;
        _d.winner = 0;
        _d.deadline = now + (uint32_t)DRAW_SECS * 1000;
        haWsBroadcast("{\"t\":\"ink\",\"clear\":true}");
        pushAll();
        haUartEvent(String("{\"draw\":\"") + ha_json_escape(_p[drawer].nick) + " drawing\"}");
    }

    void drawReveal(uint32_t now, uint8_t winner) {
        _d.phase = 2;
        _d.winner = winner;
        _d.revealUntil = now + DRAW_REVEAL_MS;
        pushAll();
    }

    void drawTick(uint32_t now) {
        if(_d.phase == 0) {
            if(connectedCount() >= 2) drawStart(now);
        } else if(_d.phase == 1) {
            if(!_p[_d.drawer].used || now > _d.deadline) drawReveal(now, 0);
        } else if(_d.phase == 2) {
            if(now > _d.revealUntil) drawStart(now);
        }
    }

    static bool wordMatch(const char* a, const char* b) {
        while(*a == ' ') a++;
        while(*b == ' ') b++;
        while(*a && *b) {
            char ca = *a, cb = *b;
            if(ca >= 'A' && ca <= 'Z') ca += 32;
            if(cb >= 'A' && cb <= 'Z') cb += 32;
            if(ca != cb) return false;
            a++;
            b++;
        }
        while(*a == ' ') a++;
        return *a == '\0' && *b == '\0';
    }

    void drawGuess(uint8_t pid, const char* text) {
        if(_active != HA_GAME_DRAW || _d.phase != 1 || pid == _d.drawer) return;
        if(wordMatch(text, _d.word)) {
            _p[pid].score += 200;
            haUartScore(pid, 200, "draw");
            if(_p[_d.drawer].used) {
                _p[_d.drawer].score += 100;
                haUartScore(_d.drawer, 100, "drawn");
            }
            haUartRoundResult(String("{\"draw\":\"") + ha_json_escape(_p[pid].nick) + " got it\"}");
            drawReveal(millis(), pid);
        } else {
            haWsBroadcast(
                String("{\"t\":\"chat\",\"nick\":\"") + ha_json_escape(_p[pid].nick) +
                "\",\"text\":\"" + ha_json_escape(text) + "\"}");
        }
    }

    static bool jsonNum(const char* s, const char* key, char* out, size_t n) {
        const char* q = ha_json_find(s, key);
        if(!q) return false;
        size_t i = 0;
        while(*q && (isdigit((unsigned char)*q) || *q == '.' || *q == '-' || *q == '+' ||
                     *q == 'e' || *q == 'E') &&
              i < n - 1)
            out[i++] = *q++;
        out[i] = '\0';
        return i > 0;
    }

    // Relay the drawer's stroke to every other client as an "ink" message.
    void drawStroke(uint8_t pid, const char* json) {
        if(_active != HA_GAME_DRAW || _d.phase != 1 || pid != _d.drawer) return;
        String ink = "{\"t\":\"ink\"";
        static const char* keys[4] = {"x0", "y0", "x1", "y1"};
        char num[16];
        for(int k = 0; k < 4; k++)
            if(jsonNum(json, keys[k], num, sizeof(num))) {
                ink += ",\"";
                ink += keys[k];
                ink += "\":";
                ink += num;
            }
        ink += "}";
        for(uint8_t p = 1; p <= HA_MAX_PLAYERS; p++)
            if(_p[p].used && _p[p].wsId && p != _d.drawer) haWsSendWs(_p[p].wsId, ink);
    }

    void drawClearInk(uint8_t pid) {
        if(_active != HA_GAME_DRAW || pid != _d.drawer) return;
        for(uint8_t p = 1; p <= HA_MAX_PLAYERS; p++)
            if(_p[p].used && _p[p].wsId && p != _d.drawer)
                haWsSendWs(_p[p].wsId, "{\"t\":\"ink\",\"clear\":true}");
    }

    String drawJson(uint8_t pid) {
        String s = "{\"t\":\"draw\",\"phase\":\"";
        s += _d.phase == 1 ? "draw" : _d.phase == 2 ? "reveal" : "idle";
        s += "\"";
        if(_d.phase != 0) {
            s += ",\"round\":";
            s += _d.round;
            if(_d.phase == 2) {
                s += ",\"word\":\"";
                s += ha_json_escape(_d.word);
                s += "\",\"winner\":";
                if(_d.winner)
                    s += _d.winner;
                else
                    s += "null";
            } else if(pid == _d.drawer) {
                s += ",\"role\":\"drawer\",\"word\":\"";
                s += ha_json_escape(_d.word);
                s += "\",\"drawer\":";
                s += _d.drawer;
            } else {
                s += ",\"role\":\"guesser\",\"len\":";
                s += (int)strlen(_d.word);
                s += ",\"drawer\":\"";
                s += ha_json_escape(_p[_d.drawer].nick);
                s += "\"";
            }
        }
        s += ",\"scores\":" + playersJson() + "}";
        return s;
    }

    // ---------- pong ----------
    void pongClear() {
        for(int i = 0; i < PONG_MAX; i++) _pm[i] = PongMatch{};
    }

    PongMatch* pongMatchOf(uint8_t pid) {
        for(int i = 0; i < PONG_MAX; i++) {
            if(!_pm[i].used) continue;
            if(_pm[i].a == pid && _pm[i].aIn) return &_pm[i];
            if(_pm[i].b == pid && _pm[i].bIn) return &_pm[i];
        }
        return nullptr;
    }

    void pongServe(PongMatch* m, int dir) {
        m->bx = 0.5f;
        m->by = 0.5f;
        m->p1 = 0.5f;
        m->p2 = 0.5f;
        m->vx = dir > 0 ? 0.018f : -0.018f;
        m->vy = 0.010f;
        m->d1 = 0;
        m->d2 = 0;
    }

    void pongStart(PongMatch* m, uint8_t a, uint8_t b) {
        *m = PongMatch{};
        m->used = true;
        m->a = a;
        m->b = b;
        m->aIn = m->bIn = true;
        m->phase = 1;
        pongServe(m, 1);
    }

    void pongPaddle(uint8_t pid, int dir) {
        PongMatch* m = pongMatchOf(pid);
        if(!m || m->phase != 1) return;
        if(dir < -1) dir = -1;
        if(dir > 1) dir = 1;
        if(pid == m->a)
            m->d1 = (int8_t)dir;
        else
            m->d2 = (int8_t)dir;
    }

    void pongFinish(PongMatch* m, uint8_t winner) {
        if(m->phase != 1) return;
        m->phase = 2;
        m->winner = winner;
        uint8_t loser = (winner == m->a) ? m->b : m->a;
        _p[winner].score += 300;
        haUartScore(winner, 300, "pongwin");
        haUartRoundResult(String("{\"win\":") + winner + ",\"lose\":" + loser + "}");
    }

    void pongOnLeave(uint8_t pid) {
        PongMatch* m = pongMatchOf(pid);
        if(!m) return;
        uint8_t opp = (pid == m->a) ? m->b : m->a;
        if(m->phase == 1) pongFinish(m, opp);
        if(pid == m->a) m->aIn = false;
        if(pid == m->b) m->bIn = false;
        if(!m->aIn && !m->bIn) *m = PongMatch{};
    }

    void pongTick() {
        const float PADHALF = 0.11f, PSPEED = 0.03f;
        for(int i = 0; i < PONG_MAX; i++) {
            PongMatch* m = &_pm[i];
            if(!m->used || m->phase != 1) continue;
            m->p1 += m->d1 * PSPEED;
            m->p2 += m->d2 * PSPEED;
            if(m->p1 < PADHALF) m->p1 = PADHALF;
            if(m->p1 > 1 - PADHALF) m->p1 = 1 - PADHALF;
            if(m->p2 < PADHALF) m->p2 = PADHALF;
            if(m->p2 > 1 - PADHALF) m->p2 = 1 - PADHALF;
            m->bx += m->vx;
            m->by += m->vy;
            if(m->by < 0) {
                m->by = 0;
                m->vy = -m->vy;
            }
            if(m->by > 1) {
                m->by = 1;
                m->vy = -m->vy;
            }
            if(m->bx <= 0.05f) {
                if(fabsf(m->by - m->p1) <= PADHALF) {
                    m->bx = 0.05f;
                    m->vx = -m->vx;
                    m->vy += (m->by - m->p1) * 0.05f;
                } else {
                    m->s2++;
                    if(m->s2 >= PONG_WIN)
                        pongFinish(m, m->b);
                    else
                        pongServe(m, 1);
                }
            }
            if(m->phase == 1 && m->bx >= 0.95f) {
                if(fabsf(m->by - m->p2) <= PADHALF) {
                    m->bx = 0.95f;
                    m->vx = -m->vx;
                    m->vy += (m->by - m->p2) * 0.05f;
                } else {
                    m->s1++;
                    if(m->s1 >= PONG_WIN)
                        pongFinish(m, m->a);
                    else
                        pongServe(m, -1);
                }
            }
            if(_p[m->a].wsId) haWsSendWs(_p[m->a].wsId, pongJson(m->a));
            if(_p[m->b].wsId) haWsSendWs(_p[m->b].wsId, pongJson(m->b));
        }
    }

    static String pongF(float v) {
        int iv = (int)(v * 1000.0f + 0.5f);
        if(iv < 0) iv = 0;
        if(iv > 1000) iv = 1000;
        char buf[8];
        snprintf(buf, sizeof(buf), "%d.%03d", iv / 1000, iv % 1000);
        return String(buf);
    }

    String pongJson(uint8_t pid) {
        PongMatch* m = pongMatchOf(pid);
        if(!m)
            return String("{\"t\":\"pong\",\"phase\":\"lobby\",\"challenges\":") +
                   duelChallengesJson() + "}";
        uint8_t opp = (pid == m->a) ? m->b : m->a;
        uint8_t me = (pid == m->a) ? 1 : 2;
        String s = "{\"t\":\"pong\",\"phase\":\"";
        s += (m->phase == 2) ? "over" : "playing";
        s += "\",\"you\":";
        s += pid;
        s += ",\"me\":";
        s += me;
        s += ",\"opp\":\"";
        s += ha_json_escape(_p[opp].nick);
        s += "\",\"ball\":{\"x\":" + pongF(m->bx) + ",\"y\":" + pongF(m->by) + "}";
        s += ",\"p1\":" + pongF(m->p1) + ",\"p2\":" + pongF(m->p2);
        s += ",\"s1\":";
        s += m->s1;
        s += ",\"s2\":";
        s += m->s2;
        if(m->phase == 2) {
            const char* r = (m->winner == pid) ? "win" : "lose";
            s += ",\"result\":\"";
            s += r;
            s += "\"";
        }
        s += "}";
        return s;
    }
};
