/* Draw & Guess. Registers handlers for {t:"draw"} (round/role/phase),
   {t:"ink"} (relayed segments for guessers) and {t:"chat"} (wrong guesses).
   Coordinates on the wire are normalised 0..1 so any canvas size renders the
   same picture. The server rotates the drawer and runs the clock. */
(function () {
  var canvas, cx;               // 2D context
  var drawing = false;          // pointer is down (drawer only)
  var lastX = 0, lastY = 0;
  var lastSent = 0;             // throttle stroke sends
  var role = "";                // "drawer" | "guesser"
  var revealedRound = -1;       // reveal sound guard

  function ready() {
    canvas = $("draw-canvas");
    cx = canvas.getContext("2d");
  }

  // Match the backing store to the CSS box so lines stay crisp; then clear.
  function sizeCanvas() {
    var r = canvas.getBoundingClientRect();
    var w = Math.max(1, Math.round(r.width));
    var h = Math.max(1, Math.round(r.height));
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w; canvas.height = h;
    }
    clearCanvas();
  }

  function clearCanvas() {
    cx.fillStyle = "#EDEDE6";               // off-white "paper"
    cx.fillRect(0, 0, canvas.width, canvas.height);
  }

  function seg(x0, y0, x1, y1) {
    cx.strokeStyle = "#111";
    cx.lineWidth = Math.max(2, canvas.width * 0.008);
    cx.lineCap = "round"; cx.lineJoin = "round";
    cx.beginPath();
    cx.moveTo(x0 * canvas.width, y0 * canvas.height);
    cx.lineTo(x1 * canvas.width, y1 * canvas.height);
    cx.stroke();
  }

  function norm(e) {
    var r = canvas.getBoundingClientRect();
    var t = e.touches && e.touches[0] ? e.touches[0] : e;
    return {
      x: Math.min(1, Math.max(0, (t.clientX - r.left) / r.width)),
      y: Math.min(1, Math.max(0, (t.clientY - r.top) / r.height)),
    };
  }

  function down(e) {
    if (role !== "drawer") return;
    e.preventDefault();
    drawing = true;
    var p = norm(e); lastX = p.x; lastY = p.y;
  }
  function moveEvt(e) {
    if (role !== "drawer" || !drawing) return;
    e.preventDefault();
    var now = Date.now();
    if (now - lastSent < 20) return;        // ~50/s cap
    lastSent = now;
    var p = norm(e);
    seg(lastX, lastY, p.x, p.y);            // draw locally
    send({ t: "stroke", x0: lastX, y0: lastY, x1: p.x, y1: p.y });
    lastX = p.x; lastY = p.y;
  }
  function up() { drawing = false; }

  function blanks(len) {
    var s = "";
    for (var i = 0; i < len; i++) s += (i ? " " : "") + "_";
    return s;
  }

  function chatLine(nick, text, cls) {
    var log = $("draw-chat");
    var div = document.createElement("div");
    div.className = "cmsg" + (cls ? " " + cls : "");
    div.innerHTML = '<b>' + esc(nick) + "</b> " + esc(text);
    log.appendChild(div);
    log.scrollTop = log.scrollHeight;
  }

  A.handlers.draw = function (m) {
    route("draw");
    if (A.view !== "draw") return;
    if (!canvas) ready();

    var status = $("draw-status"), word = $("draw-word");
    var tools = $("draw-tools"), guess = $("draw-guess"), reveal = $("draw-reveal");

    if (m.phase === "reveal") {
      role = "";
      hide("draw-tools"); hide("draw-guess"); show("draw-reveal");
      var who = m.winner == null ? "Nobody guessed it" : (esc(nickOf(m.winner)) + " got it");
      reveal.innerHTML = "Word: <b>" + esc(m.word || "") + "</b><br>" + who;
      status.textContent = "Round over";
      if (revealedRound !== m.round) {
        revealedRound = (m.round == null ? -2 : m.round);
        if (m.winner === A.pid) { A.sfx("win"); A.vibe([40, 60, 40]); }
        else if (m.winner != null) A.sfx("score");
      }
      return;
    }

    if (m.phase === "idle") {
      role = "";
      hide("draw-tools"); hide("draw-guess"); show("draw-reveal");
      reveal.textContent = "Get ready...";
      status.textContent = "Waiting";
      word.textContent = "";
      return;
    }

    // phase === "draw"
    hide("draw-reveal");
    show("draw-word");
    role = m.role;
    revealedRound = -1;
    status.textContent = "Round " + (m.round || 1);

    if (role === "drawer") {
      word.className = "draw-word";
      word.textContent = m.word || "";
      show("draw-tools"); hide("draw-guess");
      sizeCanvas();
      canvas.classList.add("drawable");
    } else {
      word.className = "draw-word blanks";
      word.textContent = blanks(m.len || 0);
      hide("draw-tools"); show("draw-guess");
      $("draw-chat").innerHTML = "";
      var who = esc(m.drawer || "Someone");
      chatLine("—", who + " is drawing", "sys");
      sizeCanvas();
      canvas.classList.remove("drawable");
    }
  };

  A.handlers.ink = function (m) {
    if (A.view !== "draw" || role !== "guesser") return;
    if (m.clear) { clearCanvas(); return; }
    seg(m.x0, m.y0, m.x1, m.y1);
  };

  A.handlers.chat = function (m) {
    if (A.view !== "draw") return;
    chatLine(m.nick, m.text);
  };

  document.addEventListener("DOMContentLoaded", function () {
    ready();
    canvas.addEventListener("pointerdown", down);
    canvas.addEventListener("pointermove", moveEvt);
    window.addEventListener("pointerup", up);
    // Touch fallback for browsers without pointer events.
    canvas.addEventListener("touchstart", down, { passive: false });
    canvas.addEventListener("touchmove", moveEvt, { passive: false });
    window.addEventListener("touchend", up);

    $("draw-clear").addEventListener("click", function () {
      if (role !== "drawer") return;
      clearCanvas(); A.sfx("buzz"); send({ t: "clear" });
    });

    $("draw-form").addEventListener("submit", function (e) {
      e.preventDefault();
      var inp = $("draw-input");
      var text = inp.value.trim();
      if (!text) return;
      A.sfx("buzz"); A.vibe(15);
      chatLine(A.nick || "You", text, "me");
      send({ t: "guess", text: text });
      inp.value = "";
    });

    window.addEventListener("resize", function () {
      if (A.view === "draw" && canvas) {
        // Resizing wipes the picture; the drawer keeps drawing, guessers wait
        // for the next stroke. Acceptable for a casual party game.
        sizeCanvas();
      }
    });
  });
})();
