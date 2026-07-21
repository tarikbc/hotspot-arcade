// Drives the WASM engine and routes its outbox: ws/all -> phone panels,
// uart -> the Flipper panel. Every entry point drains, so nothing is buffered
// across a frame boundary.
import createEngine from "./engine.js";
import { makeSocket, deliver, broadcast, getSocket, MAX_PHONES } from "./phones.js";

const M = await createEngine();
const uartSubscribers = [];
let nextPhoneId = 1;

const call = (name, argTypes, args) => M.ccall(name, null, argTypes, args);
const drainRaw = () => JSON.parse(M.ccall("ha_drain", "string", [], []));

function route(items) {
  for (const it of items) {
    if (it.to === "ws") deliver(it.id, JSON.stringify(it.msg));
    else if (it.to === "all") broadcast(JSON.stringify(it.msg));
    else if (it.to === "uart") uartSubscribers.forEach((fn) => fn(it));
  }
}

const drain = () => route(drainRaw());

export const engine = {
  reset: () => { call("ha_reset", [], []); drain(); },
  input: (wsId, json) => { call("ha_input", ["number", "string"], [wsId, json]); drain(); },
  disconnect: (wsId) => { call("ha_disconnect", ["number"], [wsId]); drain(); },
  selectGame: (id) => { call("ha_select_game", ["number"], [id]); drain(); },
  roundEnd: () => { call("ha_round_end", [], []); drain(); },
  resetScores: () => { call("ha_reset_scores", [], []); drain(); },
  triviaClear: () => { call("ha_trivia_clear", [], []); drain(); },
  triviaAddTopic: (n) => { call("ha_trivia_add_topic", ["string"], [n]); drain(); },
  triviaAddQ: (j) => { call("ha_trivia_add_q", ["string"], [j]); drain(); },
};

export function subscribeUart(fn) { uartSubscribers.push(fn); }

// The client asks the parent for its socket (see harnessSocket() in web/core/app.js).
// The ?harness= value IS the wsId, so a panel and its engine-side player stay paired
// across reloads and removals — the harness never has to guess which socket is whose.
window.HA_HARNESS = {
  connect(id) {
    const wsId = Number(id);
    // The teardown callback is what lets close() (whichever path reaches it first, see
    // phones.js) tell the engine a player left, same as removePhone() below.
    return makeSocket(wsId, (sid, data) => engine.input(sid, data), (sid) => engine.disconnect(sid));
  },
};

export function addPhone() {
  if (document.querySelectorAll(".phone").length >= MAX_PHONES) return;
  const wsId = nextPhoneId++;
  const wrap = document.createElement("div");
  wrap.className = "phone";
  wrap.dataset.wsId = String(wsId);
  const frame = document.createElement("iframe");
  frame.src = "../../web/dist/index.html?harness=" + wsId;
  wrap.appendChild(frame);
  document.getElementById("phones").appendChild(wrap);
}

export function removePhone() {
  const all = document.querySelectorAll(".phone");
  if (!all.length) return;
  const wrap = all[all.length - 1];
  const wsId = Number(wrap.dataset.wsId);
  // Route teardown through the socket's own close(), exactly as if the client (or a real
  // AsyncWebServer disconnect) had gotten there first. That single path tells the engine
  // the player went away and drops the socket from phones.js's map, and close()'s own
  // guard means it's harmless if the client also calls close() around the same time.
  const sock = getSocket(wsId);
  if (sock) sock.close();
  wrap.remove();
}

// Engine clock. The firmware ticks from millis(); here rAF drives it.
const started = performance.now();

// A bad frame (e.g. ha_drain() handing drainRaw() a malformed/empty string, or a routed
// handler throwing) must not wedge the sim forever — requestAnimationFrame(loop) has to
// run no matter what happened above it. We still don't want an error that repeats every
// frame (~60/sec) to flood the console, so: log the first occurrence of each distinct
// error, then once it's clearly not a one-off, log a single "giving up on logging this"
// note and put up a one-time visible banner. A later, different error resets the count
// and gets its own log line.
let lastLoopErrorMsg = null;
let loopErrorRepeatCount = 0;
let loopErrorBannerShown = false;

function showLoopErrorBanner(message) {
  if (loopErrorBannerShown) return;
  loopErrorBannerShown = true;
  const banner = document.createElement("div");
  banner.textContent = "Simulator error (see console): " + message;
  banner.style.cssText =
    "position:fixed;top:0;left:0;right:0;z-index:9999;padding:6px 10px;" +
    "background:#5a1e1e;color:#ffd7d7;border-bottom:1px solid #a33;" +
    "font:12px ui-monospace,SFMono-Regular,Menlo,monospace;";
  document.body.prepend(banner);
}

function loop() {
  try {
    call("ha_tick", ["number"], [Math.floor(performance.now() - started)]);
    drain();
    lastLoopErrorMsg = null;
    loopErrorRepeatCount = 0;
  } catch (err) {
    const msg = err && err.message ? err.message : String(err);
    if (msg === lastLoopErrorMsg) {
      loopErrorRepeatCount++;
    } else {
      lastLoopErrorMsg = msg;
      loopErrorRepeatCount = 1;
      console.error("hotspot-arcade sim: tick/drain failed, dropping this frame:", err);
    }
    if (loopErrorRepeatCount === 3) {
      console.error(`hotspot-arcade sim: "${msg}" is repeating every frame; ` +
        "suppressing further console spam for it, see the on-page banner instead");
      showLoopErrorBanner(msg);
    }
  }
  // Always re-arm, even after a caught failure above — one bad frame must not stop the tick.
  requestAnimationFrame(loop);
}

engine.reset();
addPhone();
addPhone();
requestAnimationFrame(loop);
