/* Frankendraw — the exquisite-corpse drawing game. Server-driven
   {t:"frankendraw", phase, ...}: lobby (ready-up) -> countdown -> draw (three
   rounds; the sheets rotate a seat each round so head/torso/legs come from three
   different hands) -> show (the gallery, one finished creature at a time) -> vote
   -> final. We send ready / stroke / clear / done / vote{sheet} / again.

   Strokes go up as {t:"stroke"} with normalised 0..1 coordinates over the WHOLE
   sheet, exactly like Draw & Guess — no bitmaps ever move. The server quantises
   them onto its own 0..FD_UNIT grid and hands ink back in those units, which is
   why every message carries `unit`/`band`/`over` instead of the client hardcoding
   them. The only ink a drawer is ever sent is the sliver of the panel above. */
(function () {
  var myready = false;
  var cv, cx, gv, gx;            // the drawing canvas and the gallery canvas
  var st = null;                 // last draw-phase message
  var mine = [];                 // my own strokes this panel, as 0..1 quads
  var slice = [];                // the sliver handed down from the panel above
  var drawing = false, lastX = 0, lastY = 0, lastSent = 0;
  var PANEL = ["fd.head", "fd.torso", "fd.legs"];
  var PAPER = "#EDEDE6", FOLD = "#c9c9be", AWAY = "#d7d7cd";

  function ready() {
    cv = $("fd-canvas"); cx = cv.getContext("2d");
    gv = $("fd-gallery"); gx = gv.getContext("2d");
  }

  function sub(name) {
    ["lobby", "count", "play", "show", "vote", "final"].forEach(function (id) {
      $("fd-" + id).classList.toggle("hide", id !== name);
    });
  }
  function stopBar() { A.timebarStop("fd-bar"); hide("fd-bar"); }

  /* ---- canvas helpers. The sheet is the full canvas; y 0..1 spans all three
     panels, so a panel's band is [top/unit, bot/unit]. ---- */
  function fit(c) {
    var r = c.getBoundingClientRect();
    var w = Math.max(1, Math.round(r.width)), h = Math.max(1, Math.round(r.height));
    if (c.width !== w || c.height !== h) { c.width = w; c.height = h; }
  }
  function paper(c, k) { k.fillStyle = PAPER; k.fillRect(0, 0, c.width, c.height); }
  function folds(c, k, bands, unit) {
    k.strokeStyle = FOLD; k.lineWidth = 1;
    for (var i = 1; i < 3; i++) {
      var y = Math.round(i * bands / unit * c.height) + 0.5;
      k.beginPath(); k.moveTo(0, y); k.lineTo(c.width, y); k.stroke();
    }
  }
  function pen(c, k) {
    k.strokeStyle = "#111"; k.lineWidth = Math.max(2, c.width * 0.01);
    k.lineCap = "round"; k.lineJoin = "round";
  }
  function seg(c, k, q) {
    k.beginPath();
    k.moveTo(q[0] * c.width, q[1] * c.height);
    k.lineTo(q[2] * c.width, q[3] * c.height);
    k.stroke();
  }
  // Flat [x0,y0,x1,y1,...] in sheet units -> 0..1 quads.
  function quads(flat, unit) {
    var o = [], i;
    flat = flat || [];
    for (i = 0; i + 3 < flat.length; i += 4)
      o.push([flat[i] / unit, flat[i + 1] / unit, flat[i + 2] / unit, flat[i + 3] / unit]);
    return o;
  }

  function paintPlay() {
    if (!st || st.panel < 0) return;
    fit(cv); paper(cv, cx);
    folds(cv, cx, st.band, st.unit);
    var top = st.top / st.unit, bot = st.bot / st.unit, over = st.over / st.unit;
    // Fold the rest of the sheet away: you draw your third and nothing else.
    cx.fillStyle = AWAY;
    if (top > over) cx.fillRect(0, 0, cv.width, (top - over) * cv.height);
    if (bot < 1) cx.fillRect(0, bot * cv.height, cv.width, (1 - bot) * cv.height);
    pen(cv, cx);
    var i;
    for (i = 0; i < slice.length; i++) seg(cv, cx, slice[i]);
    for (i = 0; i < mine.length; i++) seg(cv, cx, mine[i]);
    if (top > 0) {  // tint the sliver so it reads as "carry on from here"
      cx.fillStyle = "rgba(255,138,0,.10)";
      cx.fillRect(0, (top - over) * cv.height, cv.width, over * cv.height);
    }
  }

  function clamp01(v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
  function norm(e) {
    var r = cv.getBoundingClientRect();
    var p = e.touches && e.touches[0] ? e.touches[0] : e;
    return { x: clamp01((p.clientX - r.left) / r.width), y: clamp01((p.clientY - r.top) / r.height) };
  }
  // Keep the pen inside your own band. The server clamps too — this is only so the
  // line you see matches the line it stores.
  function band(y) {
    var top = st.top / st.unit, bot = st.bot / st.unit;
    return y < top ? top : y > bot ? bot : y;
  }
  function canDraw() { return st && st.panel >= 0 && !st.done; }

  function down(e) {
    if (!canDraw()) return;
    e.preventDefault();
    drawing = true;
    var p = norm(e); lastX = p.x; lastY = band(p.y);
  }
  function moveEvt(e) {
    if (!canDraw() || !drawing) return;
    e.preventDefault();
    var now = Date.now();
    if (now - lastSent < 40) return;
    var p = norm(e), y = band(p.y);
    // The server stores a bounded number of segments per panel, so only send a
    // segment once the pen has actually travelled — a doodle, not a sample stream.
    if (Math.abs(p.x - lastX) + Math.abs(y - lastY) < 0.02) return;
    lastSent = now;
    var q = [lastX, lastY, p.x, y];
    mine.push(q);
    pen(cv, cx); seg(cv, cx, q);
    send({ t: "stroke", x0: q[0], y0: q[1], x1: q[2], y1: q[3] });
    lastX = p.x; lastY = y;
  }
  function up() { drawing = false; }

  /* ---- screens ---- */
  function renderLobby(m) {
    sub("lobby"); stopBar();
    st = null;
    myready = A.readyLobby({ players: m.players, listId: "fd-players", readyId: "fd-ready", meId: "fd-me" });
    var short = m.need - (m.players || []).length;
    $("fd-need").textContent = short > 0 ? t("fd.need", { n: m.need }) : "";
  }

  function renderCount(m) {
    sub("count"); stopBar();
    A.countdown("fd-count-num", m.sec);
  }

  function renderPlay(m) {
    sub("play");
    var fresh = !st || st.round !== m.round;
    st = m;
    if (fresh) { mine = []; }
    slice = quads(m.ink, m.unit);
    $("fd-meta").textContent = t("common.round", { n: m.round, total: m.rounds });
    $("fd-role").textContent = m.panel >= 0 ? t(PANEL[m.panel]) : "";
    noteDeadline(m.deadline, m.dur);
    A.timebar("fd-bar", m.deadline, m.dur, false);

    if (m.panel < 0) {   // joined mid-game: no sheet this time round
      hide("fd-tools"); hide("fd-canvas");
      $("fd-note").textContent = t("fd.wait_next");
      return;
    }
    show("fd-canvas"); show("fd-tools");
    $("fd-done").disabled = !!m.done;
    $("fd-clear").disabled = !!m.done;
    cv.classList.toggle("drawable", !m.done);
    $("fd-note").textContent = m.done ? t("fd.waiting", { n: m.waiting })
      : m.panel === 0 ? t("fd.hint_head") : t("fd.hint_next");
    if (fresh) { A.sfx("start"); A.vibe(20); }
    paintPlay();
  }

  function names(who) {
    var o = [], i;
    for (i = 0; i < (who || []).length; i++) o.push(who[i] || t("fd.nobody"));
    return o.join(" · ");
  }

  function renderShow(m) {
    sub("show"); stopBar();
    $("fd-show-meta").textContent = t("fd.gallery", { n: m.n + 1, total: m.total });
    fit(gv); paper(gv, gx);
    folds(gv, gx, m.band, m.unit);
    pen(gv, gx);
    for (var p = 0; p < (m.ink || []).length; p++) {
      var qs = quads(m.ink[p], m.unit);
      for (var i = 0; i < qs.length; i++) seg(gv, gx, qs[i]);
    }
    $("fd-who").textContent = names(m.who);
    var fav = $("fd-fav");
    fav.textContent = m.mine === m.n ? t("fd.voted") : t("fd.vote_this");
    fav.classList.toggle("ghost", m.mine === m.n);
    fav.setAttribute("data-n", m.n);
  }

  function renderVote(m) {
    sub("vote"); stopBar();
    var box = $("fd-sheets");
    box.innerHTML = "";
    (m.sheets || []).forEach(function (s) {
      var b = document.createElement("button");
      b.className = "topic" + (m.mine === s.n ? " mine" : "");
      b.innerHTML = '<span class="txt">' + esc(names(s.who)) + "</span>" +
                    '<span class="votes">' + (s.votes || 0) + "</span>";
      b.addEventListener("click", function () {
        A.sfx("buzz"); A.vibe(12);
        send({ t: "vote", sheet: s.n });
      });
      box.appendChild(b);
    });
  }

  function renderFinal(m) {
    sub("final"); stopBar();
    st = null;
    $("fd-best").textContent = t("fd.best", { who: names(m.who), n: m.votes || 0 });
    var b = A.podium("fd-podium", m.board);
    if (b && b.length && b[0].pid === A.pid) { A.sfx("win"); A.vibe([30, 50, 30]); }
    else { A.sfx("start"); A.vibe(20); }
  }

  A.handlers.frankendraw = function (m) {
    route("fd");
    if (A.view !== "fd") return;
    if (!cv) ready();
    switch (m.phase) {
      case "lobby": renderLobby(m); break;
      case "countdown": renderCount(m); break;
      case "draw": renderPlay(m); break;
      case "show": renderShow(m); break;
      case "vote": renderVote(m); break;
      case "final": renderFinal(m); break;
    }
  };

  document.addEventListener("DOMContentLoaded", function () {
    ready();
    cv.addEventListener("pointerdown", down);
    cv.addEventListener("pointermove", moveEvt);
    window.addEventListener("pointerup", up);
    cv.addEventListener("touchstart", down, { passive: false });
    cv.addEventListener("touchmove", moveEvt, { passive: false });
    window.addEventListener("touchend", up);

    $("fd-ready").addEventListener("click", function () {
      A.sfx("buzz"); A.vibe(15);
      send({ t: "ready", ready: !myready });
    });
    $("fd-clear").addEventListener("click", function () {
      if (!canDraw()) return;
      A.sfx("buzz"); A.vibe(12);
      mine = []; send({ t: "clear" }); paintPlay();
    });
    $("fd-done").addEventListener("click", function () {
      if (!canDraw()) return;
      A.sfx("start"); A.vibe(20);
      send({ t: "done" });
      st.done = true;
      $("fd-done").disabled = true; $("fd-clear").disabled = true;
      cv.classList.remove("drawable");
    });
    $("fd-fav").addEventListener("click", function () {
      A.sfx("buzz"); A.vibe(15);
      send({ t: "vote", sheet: Number($("fd-fav").getAttribute("data-n")) });
    });
    $("fd-again").addEventListener("click", function () {
      A.sfx("start"); A.vibe(20);
      send({ t: "again" });
    });

    window.addEventListener("resize", function () {
      if (A.view === "fd") paintPlay();
    });
  });
})();
