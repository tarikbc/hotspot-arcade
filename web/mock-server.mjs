// Tiny zero-dependency mock of the ESP server for eyeballing the UI in a real
// browser. Serves dist/index.html at "/" and speaks just enough of RFC 6455 at
// "/ws" to drive the client through lobby -> trivia -> connect4.
//
//   npm run build && npm run mock   ->   open http://localhost:8080
//
// This is a demo, not the referee. It scripts a plausible sequence so every
// screen renders; it does not implement real game rules.
import { createServer } from "node:http";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const ROOT = dirname(fileURLToPath(import.meta.url));
const PAGE = join(ROOT, "dist", "index.html");
const PORT = process.env.PORT || 8080;
const GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

const server = createServer((req, res) => {
  try {
    const body = readFileSync(PAGE);
    res.writeHead(200, { "Content-Type": "text/html" });
    res.end(body);
  } catch {
    res.writeHead(500).end("Run `npm run build` first.");
  }
});

server.on("upgrade", (req, socket) => {
  if (!req.url.startsWith("/ws")) { socket.destroy(); return; }
  const key = req.headers["sec-websocket-key"];
  const accept = createHash("sha1").update(key + GUID).digest("base64");
  socket.write(
    "HTTP/1.1 101 Switching Protocols\r\n" +
    "Upgrade: websocket\r\nConnection: Upgrade\r\n" +
    "Sec-WebSocket-Accept: " + accept + "\r\n\r\n"
  );
  handle(socket);
});

function send(socket, obj) {
  const data = Buffer.from(JSON.stringify(obj));
  const len = data.length;
  let head;
  if (len < 126) head = Buffer.from([0x81, len]);
  else if (len < 65536) head = Buffer.from([0x81, 126, len >> 8, len & 255]);
  else head = Buffer.from([0x81, 127, 0, 0, 0, 0, len >>> 24, (len >> 16) & 255, (len >> 8) & 255, len & 255]);
  socket.write(Buffer.concat([head, data]));
}

function handle(socket) {
  const me = 1, bot = 2, cathy = 3;
  let nick = "You";
  const scores = { 1: 0, 2: 0, 3: 0 };
  const nicks = { 1: "You", 2: "Botby", 3: "Cathy" };
  let timers = [];
  const at = (ms, fn) => timers.push(setTimeout(fn, ms));

  const players = () => [1, 2, 3].map((p) => ({ pid: p, nick: nicks[p], score: scores[p] }));
  const lobby = (game) => send(socket, { t: "lobby", game, players: players(), me });

  function runTrivia() {
    lobby("trivia");
    const q = {
      q: "Which planet is the largest in our solar system?",
      o: ["Mars", "Jupiter", "Saturn", "Neptune"], correct: 1,
    };
    at(400, () => send(socket, { t: "trivia", phase: "idle", scores: sc() }));
    at(1500, () => send(socket, {
      t: "trivia", phase: "question", i: 0, q: q.q, o: q.o,
      dur: 10000, deadline: Date.now() + 10000, mine: -1, counts: [0, 0, 0, 0], scores: sc(),
    }));
    at(11800, () => {
      scores[bot] += 100; scores[me] += 80;
      send(socket, {
        t: "trivia", phase: "reveal", i: 0, q: q.q, o: q.o, correct: q.correct,
        mine: 1, counts: [1, 2, 0, 0], scores: sc(),
      });
    });
    at(15000, runConnect4);
  }
  const sc = () => players().map((p) => ({ pid: p.pid, nick: p.nick, score: p.score }));

  let board = new Array(42).fill(0);
  function runConnect4() {
    board = new Array(42).fill(0);
    lobby("connect4");
    at(300, () => send(socket, {
      t: "c4", phase: "lobby", challenges: [{ from: cathy, to: me }],
    }));
  }

  function drop(col, who) {
    for (let r = 5; r >= 0; r--) {
      if (board[r * 7 + col] === 0) { board[r * 7 + col] = who; return true; }
    }
    return false;
  }
  function c4State(phase, extra) {
    send(socket, Object.assign({
      t: "c4", phase, mid: 1, board: board.slice(),
      turn: me, me: 1, opp: nicks[bot], you: me,
    }, extra || {}));
  }

  socket.on("data", (buf) => {
    const msg = decode(buf);
    if (!msg) return;
    let m; try { m = JSON.parse(msg); } catch { return; }
    if (m.t === "hello") {
      nick = m.nick || "You"; nicks[me] = nick;
      send(socket, { t: "welcome", pid: me, nick });
      lobby("none");
      at(1200, runTrivia);
    } else if (m.t === "ping") {
      send(socket, { t: "pong" });
    } else if (m.t === "answer") {
      send(socket, { t: "toast", msg: "Locked in " + "ABCD"[m.c] });
    } else if (m.t === "accept") {
      c4State("playing", { turn: me });
    } else if (m.t === "cancel") {
      send(socket, { t: "c4", phase: "lobby", challenges: [] });
    } else if (m.t === "move") {
      drop(m.col, me);
      c4State("playing", { turn: bot });
      at(600, () => {           // bot replies in a random open column
        let c; do { c = Math.floor(Math.random() * 7); } while (!drop(c, bot) && board.some((x) => x === 0));
        const full = board.every((x) => x !== 0);
        c4State(full ? "over" : "playing", full ? { turn: me, result: "draw" } : { turn: me });
      });
    } else if (m.t === "leaveGame") {
      timers.forEach(clearTimeout); timers = [];
      lobby("none");
      at(800, runTrivia);
    }
  });
  socket.on("close", () => timers.forEach(clearTimeout));
  socket.on("error", () => {});
}

// Minimal single-frame text decoder (client frames are always masked).
function decode(buf) {
  if (buf.length < 2) return null;
  const op = buf[0] & 0x0f;
  if (op === 0x8) return null; // close
  let len = buf[1] & 0x7f, off = 2;
  if (len === 126) { len = buf.readUInt16BE(2); off = 4; }
  else if (len === 127) { len = Number(buf.readBigUInt64BE(2)); off = 10; }
  const mask = buf.slice(off, off + 4); off += 4;
  const out = Buffer.alloc(len);
  for (let i = 0; i < len; i++) out[i] = buf[off + i] ^ mask[i & 3];
  return out.toString("utf8");
}

server.listen(PORT, () => console.log("Mock server: http://localhost:" + PORT));
