# Hotspot Arcade — wire protocols

Two links, two protocols. This file is the source of truth; the Flipper app, the
ESP32 firmware, and the web client must all agree with it.

```
 Flipper Zero  <--- UART v2 (framed) --->  ESP32-S2  <--- WebSocket (JSON) --->  Phones
  host brain                                referee                               players
```

- **UART v2** carries only low-frequency session/meta traffic (asset upload, AP
  control, round orchestration, join/leave, score deltas). Never per-frame state.
- **WebSocket JSON** carries the real-time game traffic, local to the ESP.

---

## 1. UART v2 (Flipper <-> ESP32)

- **115200-safe wiring, run at 921600 8N1** on the Flipper GPIO USART (pins 13/14).
  Fallback 460800 if the trace is noisy. Baud is a build constant on both sides.
- Same expansion-service dance as flytrap: `expansion_disable()` **before**
  `furi_hal_serial_control_acquire()`, `expansion_enable()` after release.

### 1.1 Control frame

All control messages are framed so the link can resync after noise:

```
+------+------+---------+-----------------+------+
| SYNC | TYPE | LEN(2)  | PAYLOAD(LEN)    | CRC8 |
+------+------+---------+-----------------+------+
 0xA5    1B    LE u16     LEN bytes         1B
```

- `SYNC = 0xA5`.
- `TYPE` = one of the message types below.
- `LEN` = payload length, little-endian u16 (0..4096). Payloads are capped at
  **4096** bytes; larger data (asset files) uses the raw-bulk escape (1.3).
- `CRC8` = CRC-8/ATM (poly 0x07, init 0x00) over `TYPE || LEN || PAYLOAD`.
- On a bad CRC or unknown type, the receiver drops the frame and rescans for the
  next `0xA5`.

### 1.2 Message types

**Flipper -> ESP**

| Type | Name         | Payload |
|------|--------------|---------|
| 0x10 | CLEAR_FILES  | (none) — drop all stored assets, start of session |
| 0x11 | FILE_BEGIN   | `flags(1)` `pathlen(1)` `path` `mimelen(1)` `mime` `total(4 LE)` — then `total` **raw** bytes follow (see 1.3). `flags` bit0 = gzip. |
| 0x12 | SET_AP       | `ssid` (UTF-8, <=32B) |
| 0x13 | START        | (none) — bring up AP + DNS + HTTP + WS |
| 0x14 | STOP         | (none) |
| 0x15 | RESET        | (none) — ESP reboots |
| 0x16 | SELECT_GAME  | `gameid(1)` — 0 lobby, 1 trivia, 2 connect4 |
| 0x17 | QUESTION     | JSON: `{"i":<n>,"q":"..","o":["a","b","c","d"],"c":<0-3>,"dur":<sec>}` (trivia) |
| 0x18 | REVEAL       | (none) — close the current question, broadcast the correct answer |
| 0x19 | ROUND_END    | (none) — back to lobby for the active game |
| 0x1A | CONFIG       | JSON: `{"max":8}` optional tuning |
| 0x1B | RESET_SCORES | (none) — zero the ESP live score mirror |

**ESP -> Flipper**

| Type | Name         | Payload |
|------|--------------|---------|
| 0x80 | STATUS       | token: `boot` `files_ok` `ap_ok` `up ip=..` `stopped` `err ..` |
| 0x81 | JOIN         | `pid(1)` `nick` — a player joined |
| 0x82 | LEAVE        | `pid(1)` |
| 0x83 | SCORE        | `pid(1)` `delta(2 LE, signed)` `reason` — authoritative-persist on Flipper |
| 0x84 | ROUND_RESULT | JSON, game-specific (trivia: `{"correct":[pid..]}`, c4: `{"win":pid,"lose":pid}` or `{"draw":[a,b]}`) |
| 0x85 | EVENT        | JSON for host display, e.g. `{"answers":3,"total":5}` or `{"c4":"A vs B started"}` |
| 0x86 | PING         | (none) — ~every 2s liveness beacon |

### 1.3 Raw-bulk escape (asset upload)

`FILE_BEGIN` is a normal control frame; immediately after its CRC, the sender
writes exactly `total` **unframed** bytes (the file content, possibly gzipped).
The receiver switches to a raw-read state, counts down `total`, stores the bytes,
then returns to frame parsing. This mirrors flytrap's `sethtml <N>\n` + N bytes,
generalized to named files. Bulk bytes need no escaping because the length is known.

### 1.4 Handshake (session start)

```
Flipper                         ESP
  |-- CLEAR_FILES -------------->|
  |-- FILE_BEGIN + bytes (xN) -->|   (index.html.gz, app.js.gz, app.css.gz, ...)
  |-- SET_AP ------------------->|
  |-- START -------------------->|
  |<------------- STATUS ap_ok --|
  |<------------- STATUS up ip=..|   AP live, phones can join
```
Then live: JOIN/LEAVE/SCORE/EVENT/ROUND_RESULT flow up; SELECT_GAME/QUESTION/
REVEAL/ROUND_END flow down as the host drives rounds. PING beacons throughout.

---

## 2. WebSocket JSON (Phone <-> ESP32)

- Endpoint: `ws://192.168.4.1/ws`. One socket per phone.
- All messages are a single JSON object with a `t` (type) field. Small; one frame.
- The ESP is authoritative: clients render server state and send intents only.

### 2.1 Client -> Server

| `t`        | Fields | Meaning |
|------------|--------|---------|
| `hello`    | `nick` | Join / re-join with a nickname (from localStorage) |
| `answer`   | `c` (0-3) | Trivia: buzz an answer for the current question |
| `challenge`| `to` (pid) | Connect4: challenge a player in the lobby |
| `accept`   | `from` (pid) | Connect4: accept a pending challenge |
| `cancel`   | | Connect4: withdraw my challenge / decline |
| `move`     | `col` (0-6) | Connect4: drop a disc in a column |
| `leaveGame`| | Connect4: forfeit/exit the current match |
| `ping`     | | keepalive |

### 2.2 Server -> Client

| `t`      | Fields | Meaning |
|----------|--------|---------|
| `welcome`| `pid`, `nick` | Assigned player id after `hello` |
| `lobby`  | `game` ("none"/"trivia"/"connect4"), `players` (`[{pid,nick,score}]`), `me` (pid) | Lobby snapshot; sent on change |
| `trivia` | `phase` ("idle"/"question"/"reveal"), `i`, `q`, `o` (opts), `dur`, `deadline` (ms epoch-ish, server `millis`), `mine` (my choice or -1), `counts` ([n0..n3]), `correct` (reveal only), `scores` | Full trivia view for this client |
| `c4`     | `phase` ("lobby"/"playing"/"over"), lobby: `challenges` (`[{from,to}]`); playing: `mid`, `board` (42 ints: 0 empty/1/2), `turn` (pid), `me` (1 or 2), `opp` (nick), `you` (pid); over: `result` ("win"/"lose"/"draw") | Full connect4 view for this client |
| `toast`  | `msg` | Transient message to show |
| `pong`   | | keepalive reply |

Server `millis` is used for `deadline`; the client shows a countdown from
`deadline - now_estimate`, so exact clock sync is not required (the server is the
referee for scoring; the client bar is cosmetic).

### 2.3 Scoring split

The ESP scores the live session (speed+correctness for trivia, win/draw for c4)
and (a) keeps a **live mirror** it broadcasts to phones in `players[].score`, and
(b) reports each delta to the Flipper via UART `SCORE` for the host display and
persistent leaderboard. Both stay consistent because every delta is reported.
