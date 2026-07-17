/* Connect 4 module. Registers a handler for {t:"c4"}. Three phases:
   lobby (challenge / accept), playing (7x6 board), over (result).
   We only send intents; the server owns the board. */
(function () {
  var COLS = 7, ROWS = 6;
  var prev = null;      // previous board, to animate only the newly dropped disc

  function otherPlayers() {
    return (A.players || []).filter(function (p) { return p.pid !== A.pid; });
  }

  function renderLobby(m) {
    hide("c4-match"); hide("c4-over"); show("c4-lobby");
    hide("c4-leave");
    var challenges = m.challenges || [];

    // Incoming + outgoing challenges.
    var inc = $("c4-incoming");
    inc.innerHTML = "";
    challenges.forEach(function (c) {
      var row = document.createElement("div");
      row.className = "challenge";
      if (c.to === A.pid) {
        row.innerHTML = '<span class="pn">' + esc(nickOf(c.from)) + " challenged you</span>";
        add(row, "Accept", "btn sm", function () { send({ t: "accept", from: c.from }); });
        add(row, "Decline", "btn ghost sm", function () { send({ t: "cancel" }); });
      } else if (c.from === A.pid) {
        row.innerHTML = '<span class="pn">Waiting on ' + esc(nickOf(c.to)) + "...</span>";
        add(row, "Cancel", "btn ghost sm", function () { send({ t: "cancel" }); });
      } else { return; }
      inc.appendChild(row);
    });

    // Players you can challenge.
    var iChallenged = challenges.some(function (c) { return c.from === A.pid; });
    var list = $("c4-players");
    list.innerHTML = "";
    var others = otherPlayers();
    if (!others.length) {
      var li = document.createElement("li");
      li.innerHTML = '<span class="pn" style="color:var(--dim)">No other players yet.</span>';
      list.appendChild(li);
    }
    others.forEach(function (p) {
      var li = document.createElement("li");
      li.innerHTML = '<span class="dot"></span><span class="pn">' + esc(p.nick) + "</span>";
      var pending = challenges.some(function (c) {
        return (c.from === A.pid && c.to === p.pid) || (c.from === p.pid && c.to === A.pid);
      });
      var b = add(li, pending ? "Pending" : "Challenge", "btn sm", function () {
        send({ t: "challenge", to: p.pid });
      });
      if (pending || iChallenged) b.disabled = true;
      list.appendChild(li);
    });
  }

  function nickOf(pid) {
    var p = (A.players || []).find(function (x) { return x.pid === pid; });
    return p ? p.nick : "Player";
  }

  function add(parent, label, cls, fn) {
    var b = document.createElement("button");
    b.className = cls; b.textContent = label;
    b.addEventListener("click", fn);
    parent.appendChild(b);
    return b;
  }

  function renderPlaying(m) {
    hide("c4-lobby"); hide("c4-over"); show("c4-match"); show("c4-leave");
    var myTurn = m.turn === m.you;
    var turn = $("c4-turn");
    turn.textContent = myTurn ? "Your turn" : (esc(m.opp || "Opponent") + "'s turn");
    turn.className = "turn" + (myTurn ? " you" : "");

    var board = $("c4-board");
    board.className = "board" + (myTurn ? " mine" : "");
    var b = m.board || [];
    var changed = board.childElementCount !== COLS * ROWS;
    if (changed) board.innerHTML = "";

    for (var i = 0; i < COLS * ROWS; i++) {
      var cell = changed ? document.createElement("div") : board.children[i];
      var v = b[i] || 0;
      var isNew = prev && prev[i] === 0 && v !== 0;
      // Colour by ownership, not player number: yours is orange, opponent off-white.
      var own = v === 0 ? "" : (v === m.me ? " you" : " opp");
      cell.className = "cell" + own + (isNew ? " drop" : "");
      if (changed) board.appendChild(cell);
    }
    prev = b.slice();

    // Column tap target: a tap anywhere in a column drops a disc there. Rebind
    // each render so the closure sees the current turn state.
    board.onclick = function (e) {
      if (!myTurn) return;
      var idx = Array.prototype.indexOf.call(board.children, e.target);
      if (idx < 0) return;
      send({ t: "move", col: idx % COLS });
    };
  }

  function renderOver(m) {
    hide("c4-lobby"); hide("c4-match"); show("c4-over"); show("c4-leave");
    var r = $("c4-result");
    var map = { win: ["Win", "win"], lose: ["Lose", "lose"], draw: ["Draw", "draw"] };
    var res = map[m.result] || ["Over", ""];
    r.textContent = res[0];
    r.className = "result " + res[1];
    prev = null;
  }

  A.handlers.c4 = function (m) {
    route("c4");
    if (A.view !== "c4") return;   // still on landing; render when joined
    if (m.phase === "playing") renderPlaying(m);
    else if (m.phase === "over") renderOver(m);
    else renderLobby(m);
  };

  // Leave / back-to-lobby both forfeit or exit the match.
  document.addEventListener("DOMContentLoaded", function () {
    $("c4-leave").addEventListener("click", function () { send({ t: "leaveGame" }); });
    $("c4-back").addEventListener("click", function () { send({ t: "leaveGame" }); });
  });
})();
