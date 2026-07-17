/* Hotspot Arcade client core. Shares state through the global `A` so the game
   modules (trivia.js, connect4.js) can plug in without a bundler. */
"use strict";
var A = {
  ws: null,
  pid: null,
  nick: "",
  view: "landing",     // landing | lobby | trivia | c4
  joined: false,       // true once the user committed a nick (Play) or is rejoining
  offset: 0,           // serverMillis - localNow, learned from first timed msg
  offsetSet: false,
  retry: 0,
  handlers: {},        // messageType -> fn(msg), filled by game modules
};

var $ = function (id) { return document.getElementById(id); };
function show(id) { $(id).classList.remove("hide"); }
function hide(id) { $(id).classList.add("hide"); }
function esc(s) {
  return String(s == null ? "" : s).replace(/[&<>"]/g, function (c) {
    return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c];
  });
}

/* Show exactly one top-level screen. */
function screen(name) {
  A.view = name;
  ["landing", "lobby", "trivia", "c4"].forEach(function (s) {
    $(s).classList.toggle("hide", s !== name);
  });
}

/* Auto-route driven by server state. Ignored until the user has joined so we
   never yank someone off the landing screen before they pick a nickname. */
function route(name) { if (A.joined) screen(name); }

/* Cosmetic clock aligned to the server. deadline - serverNow() = ms remaining. */
function serverNow() { return Date.now() + A.offset; }
function noteDeadline(deadline, dur) {
  // Estimate the offset once from the first question we see so the countdown
  // matches the server. dur is the full window length in ms.
  if (!A.offsetSet && deadline && dur) {
    A.offset = (deadline - dur) - Date.now();
    A.offsetSet = true;
  }
}

var toastTimer;
function toast(msg) {
  var el = $("toast");
  el.textContent = msg;
  el.classList.remove("hide");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(function () { el.classList.add("hide"); }, 2600);
}

function send(obj) {
  if (A.ws && A.ws.readyState === 1) A.ws.send(JSON.stringify(obj));
}

/* Header status dot: "" connected (orange), "warn" reconnecting, "bad" down. */
function setDot(cls) { $("dot").className = "dot" + (cls ? " " + cls : ""); }
function setNick() { $("hdr-nick").textContent = A.nick || ""; }

/* WebSocket URL derives from the host so a local mock server also works.
   On the ESP this resolves to ws://192.168.4.1/ws. */
function wsUrl() {
  var proto = location.protocol === "https:" ? "wss:" : "ws:";
  var host = location.host || "192.168.4.1";
  return proto + "//" + host + "/ws";
}

function connect() {
  var ws;
  try { ws = new WebSocket(wsUrl()); }
  catch (e) { scheduleReconnect(); return; }
  A.ws = ws;

  ws.onopen = function () {
    A.retry = 0;
    hide("netbar");
    setDot("");           // connected
    // Auto (re)join only if we already have a nickname. A first-time visitor
    // stays on the landing screen until they press Play.
    if (A.joined) send({ t: "hello", nick: A.nick });
  };

  ws.onmessage = function (ev) {
    var m;
    try { m = JSON.parse(ev.data); } catch (e) { return; }
    dispatch(m);
  };

  ws.onclose = function () { scheduleReconnect(); };
  ws.onerror = function () { try { ws.close(); } catch (e) {} };
}

function scheduleReconnect() {
  setDot(A.retry > 4 ? "bad" : "warn");   // down after repeated failures
  if (A.view !== "landing") {
    $("netbar").textContent = "Reconnecting...";
    show("netbar");
  }
  var wait = Math.min(1000 * Math.pow(1.6, A.retry), 8000);
  A.retry++;
  setTimeout(connect, wait);
}

/* Core message routing. Game-specific messages hand off to registered
   handlers so the modules stay self-contained. */
function dispatch(m) {
  switch (m.t) {
    case "welcome":
      A.pid = m.pid;
      if (m.nick) { A.nick = m.nick; setNick(); }
      break;
    case "lobby":
      onLobby(m);
      break;
    case "toast":
      toast(m.msg);
      break;
    case "pong":
      break;
    default:
      if (A.handlers[m.t]) A.handlers[m.t](m);
  }
}

/* Lobby: player list + which game the host picked. Routes into the active
   game view; a trivia/c4 message can also switch us in (see game modules). */
function onLobby(m) {
  if (m.me) A.pid = m.me;
  A.players = m.players || [];   // kept for the Connect 4 challenge list
  $("lobby-me").textContent = A.nick ? "You: " + A.nick : "";

  var list = $("players");
  list.innerHTML = "";
  (m.players || []).forEach(function (p) {
    var li = document.createElement("li");
    if (p.pid === A.pid) li.className = "self";
    li.innerHTML =
      '<span class="dot"></span>' +
      '<span class="pn">' + esc(p.nick) + "</span>" +
      '<span class="ps">' + (p.score || 0) + "</span>";
    list.appendChild(li);
  });

  var g = m.game || "none";
  var gs = $("lobby-game");
  if (g === "none") {
    gs.textContent = "Waiting for the host to pick a game.";
  } else {
    gs.textContent = (g === "trivia" ? "Trivia" : "Connect 4") + " starting...";
  }

  // If the host went back to the plain lobby, leave any game screen.
  if (g === "none" && A.view !== "landing") route("lobby");
  else if (g !== "none" && A.view === "lobby") {
    // Show the shell of the chosen game; the game message fills in details.
    route(g === "trivia" ? "trivia" : "c4");
  }
}

/* Landing flow */
function startPlay() {
  var n = $("nick").value.trim().slice(0, 12);
  if (!n) { $("nick").focus(); return; }
  A.nick = n;
  A.joined = true;
  setNick();
  try { localStorage.setItem("ha_nick", n); } catch (e) {}
  send({ t: "hello", nick: n });
  screen("lobby");
}

function initApp() {
  var saved = "";
  try { saved = localStorage.getItem("ha_nick") || ""; } catch (e) {}
  A.nick = saved;
  A.joined = !!saved;   // returning player: rejoin automatically on connect
  $("nick").value = saved;
  setNick();
  setDot("warn");       // not connected yet

  $("play").addEventListener("click", startPlay);
  $("nick").addEventListener("keydown", function (e) {
    if (e.key === "Enter") startPlay();
  });

  connect();
  // Keepalive; also nudges the server to resend state after a doze.
  setInterval(function () { send({ t: "ping" }); }, 20000);
}

document.addEventListener("DOMContentLoaded", initApp);
