// Drives the WASM engine and routes its outbox: ws/all -> phone panels,
// uart -> the Flipper panel. Every entry point drains, so nothing is buffered
// across a frame boundary.
import createEngine from "./engine.js";
import { makeSocket, deliver, broadcast, dropSocket, MAX_PHONES } from "./phones.js";

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
    return makeSocket(wsId, (sid, data) => engine.input(sid, data));
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
  // Tell the engine the socket went away, exactly as AsyncWebServer would on a real
  // disconnect. Without this the player lingers in the roster and in any live match.
  engine.disconnect(wsId);
  dropSocket(wsId);
  wrap.remove();
}

// Engine clock. The firmware ticks from millis(); here rAF drives it.
const started = performance.now();
function loop() {
  call("ha_tick", ["number"], [Math.floor(performance.now() - started)]);
  drain();
  requestAnimationFrame(loop);
}

engine.reset();
addPhone();
addPhone();
requestAnimationFrame(loop);
