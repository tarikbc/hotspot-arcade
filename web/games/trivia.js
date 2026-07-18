/* Trivia module. The ESP is authoritative and self-organizing: it drives the
   whole flow through {t:"trivia", phase, ...} messages and we render server
   state, sending only intents (ready / vote / answer / again). Five sub-states
   live inside the one Trivia screen: lobby, countdown, question, reveal, final. */
(function () {
  var LABELS = ["A", "B", "C", "D"];
  var last = null;      // last trivia message (kept for reference)
  var barAnim = null;
  var ticks = [];       // scheduled countdown blips for the question bar
  var revealedFor = -1; // question index we already played the reveal sound for
  var myready = false;  // mirrors the lobby ready toggle for the ready intent
  var leadOpen = false; // collapsible leaderboard: collapsed by default
  var finaledFor = false; // played the final win/lose cue for this game once

  function noMotion() {
    return window.matchMedia && matchMedia("(prefers-reduced-motion:reduce)").matches;
  }

  // Server board -> ranked list (defensive local sort; the ESP already sorts).
  function ranked(m) {
    return (m.board || []).slice().sort(function (a, b) { return b.score - a.score; });
  }

  // Show exactly one trivia sub-state block.
  function sub(name) {
    ["lobby", "count", "play", "final"].forEach(function (id) {
      $("tv-" + id).classList.toggle("hide", id !== name);
    });
  }

  // ---- countdown bar (question timer), same transform trick as before -------
  function clearTicks() { ticks.forEach(clearTimeout); ticks = []; }

  function runBar(deadline, dur) {
    var bar = $("tv-bar"), fill = bar.firstElementChild;
    if (barAnim) { clearTimeout(barAnim); barAnim = null; }
    clearTicks();
    show("tv-bar");
    var remain = deadline - serverNow();
    bar.classList.remove("hot");
    if (remain <= 0 || !dur) { fill.style.transition = "none"; fill.style.transform = "scaleX(0)"; bar.classList.add("hot"); return; }
    var frac = Math.max(0, Math.min(1, remain / dur));
    fill.style.transition = "none";
    fill.style.transform = "scaleX(" + frac + ")";
    requestAnimationFrame(function () {
      fill.style.transition = "transform " + remain + "ms linear";
      fill.style.transform = "scaleX(0)";
    });
    if (remain <= 3000) bar.classList.add("hot");
    else barAnim = setTimeout(function () { bar.classList.add("hot"); }, remain - 3000);
    [3000, 2000, 1000].forEach(function (t) {
      if (remain > t) ticks.push(setTimeout(function () { A.sfx("tick"); }, remain - t));
    });
  }

  // ---- collapsible leaderboard ----------------------------------------------
  function renderLead(m, bump) {
    var b = ranked(m);
    if (!b.length) { hide("tv-lead"); return; }
    show("tv-lead");
    var top = b[0];
    $("tv-lead-sum").innerHTML = "<b>1.</b> " + esc(top.nick) + "  " + top.score;
    var ol = $("tv-lead-full");
    ol.innerHTML = "";
    b.forEach(function (p, i) {
      var li = document.createElement("li");
      if (p.pid === A.pid) li.className = "self";
      li.innerHTML =
        '<span class="r">' + (i + 1) + "</span>" +
        '<span class="pn">' + esc(p.nick) + "</span>" +
        '<span class="ps">' + (p.score || 0) + "</span>";
      ol.appendChild(li);
    });
    ol.classList.toggle("hide", !leadOpen);
    $("tv-lead-caret").innerHTML = leadOpen ? "&#9650;" : "&#9660;";
    $("tv-lead-bar").setAttribute("aria-expanded", leadOpen ? "true" : "false");
    if (bump && !noMotion()) {
      var el = $("tv-lead");
      el.classList.remove("bump"); void el.offsetWidth; el.classList.add("bump");
    }
  }

  // ---- lobby (ready + vote) -------------------------------------------------
  function renderLobby(m) {
    sub("lobby");
    hide("tv-lead");
    finaledFor = false;
    myready = !!m.myready;
    $("tv-lobby-me").textContent = A.nick ? "You: " + A.nick : "";

    var ul = $("tv-players");
    ul.innerHTML = "";
    (m.players || []).forEach(function (p) {
      var li = document.createElement("li");
      if (p.pid === A.pid) li.className = "self";
      li.innerHTML =
        '<span class="dot' + (p.ready ? "" : " warn") + '"></span>' +
        '<span class="pn">' + esc(p.nick) + "</span>" +
        '<span class="rdy' + (p.ready ? " on" : "") + '">' + (p.ready ? "Ready" : "...") + "</span>";
      ul.appendChild(li);
    });

    var rb = $("tv-ready");
    rb.textContent = myready ? "Ready. Tap to cancel" : "I'm ready";
    rb.classList.toggle("on", myready);

    var tl = $("tv-topics");
    tl.innerHTML = "";
    (m.topics || []).forEach(function (tp, i) {
      var b = document.createElement("button");
      b.className = "topic" + (m.myvote === i ? " mine" : "");
      b.innerHTML =
        '<span class="txt">' + esc(tp.name) + "</span>" +
        '<span class="votes">' + (tp.votes || 0) + "</span>";
      b.addEventListener("click", function () {
        A.sfx("buzz"); A.vibe(12);
        send({ t: "vote", topic: i });
      });
      tl.appendChild(b);
    });
  }

  // ---- countdown ------------------------------------------------------------
  function renderCount(m) {
    sub("count");
    hide("tv-lead");
    $("tv-count-topic").textContent = m.topic || "";
    var n = $("tv-count-num");
    n.textContent = m.secs;
    A.sfx("tick"); A.vibe(10);           // one tick per second
    if (!noMotion()) { n.classList.remove("pop"); void n.offsetWidth; n.classList.add("pop"); }
  }

  // ---- question / reveal shared option rendering ----------------------------
  function head(m) {
    $("tv-qmeta").textContent =
      (m.topic ? m.topic + " . " : "") + "Q" + ((m.i || 0) + 1) + " / " + (m.n || 0);
  }

  function renderOpts(m, reveal, mine) {
    var opts = $("tv-opts");
    var counts = m.counts || [];
    var picked = mine >= 0;
    opts.innerHTML = "";
    (m.o || []).forEach(function (text, i) {
      var b = document.createElement("button");
      b.className = "opt";
      var cls = [];
      if (reveal) {
        if (i === m.correct) cls.push("correct");
        else if (i === mine) cls.push("wrong");
      } else if (i === mine) {
        cls.push("mine");
      }
      if (cls.length) b.className += " " + cls.join(" ");
      if (reveal || picked) b.disabled = true;

      var cnt = reveal ? ('<span class="cnt">' + (counts[i] || 0) + "</span>") : "";
      b.innerHTML =
        '<span class="lab">' + LABELS[i] + "</span>" +
        '<span class="txt">' + esc(text) + "</span>" + cnt;

      if (!reveal) b.addEventListener("click", function () {
        if (b.disabled) return;
        A.sfx("buzz"); A.vibe(15);
        send({ t: "answer", c: i });
        // Optimistic lock; the server confirms via the next trivia message.
        Array.prototype.forEach.call(opts.children, function (c) { c.disabled = true; });
        b.classList.add("mine");
      });
      opts.appendChild(b);
    });
  }

  function renderQuestion(m) {
    sub("play");
    revealedFor = -1;
    head(m);
    $("tv-answered").textContent = "Answered " + (m.answered || 0) + " / " + (m.total || 0);
    $("tv-q").textContent = m.q || "";
    var mine = (typeof m.mine === "number") ? m.mine : -1;
    noteDeadline(m.deadline, m.dur); runBar(m.deadline, m.dur);
    renderOpts(m, false, mine);
    renderLead(m, false);
  }

  function renderReveal(m) {
    sub("play");
    hide("tv-bar"); clearTicks();
    head(m);
    var gained = (typeof m.gained === "number") ? m.gained : 0;
    $("tv-answered").textContent = gained > 0 ? "+" + gained + " pts" : "";
    $("tv-q").textContent = m.q || "";
    var mine = (typeof m.mine === "number") ? m.mine : -1;
    renderOpts(m, true, mine);
    // Outcome cue once per question: correct if you nailed it, wrong if you
    // picked and missed, silent if you never answered.
    if (revealedFor !== m.i) {
      revealedFor = m.i;
      if (mine >= 0) {
        if (mine === m.correct) { A.sfx("correct"); A.vibe([25, 40, 25]); }
        else { A.sfx("wrong"); A.vibe(120); }
      }
    }
    renderLead(m, true);   // subtle bump as the board updates
  }

  // ---- final ----------------------------------------------------------------
  function renderFinal(m) {
    sub("final");
    hide("tv-lead");
    var b = ranked(m);
    var ol = $("tv-podium");
    ol.innerHTML = "";
    b.forEach(function (p, i) {
      var li = document.createElement("li");
      li.className = "place p" + (i + 1) + (p.pid === A.pid ? " self" : "");
      li.innerHTML =
        '<span class="rank">' + (i + 1) + "</span>" +
        '<span class="pn">' + esc(p.nick) + "</span>" +
        '<span class="ps">' + (p.score || 0) + "</span>";
      ol.appendChild(li);
    });
    // Win/lose cue once per game.
    if (!finaledFor) {
      finaledFor = true;
      if (b.length && b[0].pid === A.pid) { A.sfx("win"); A.vibe([30, 50, 30, 50, 60]); }
      else { A.sfx("lose"); }
    }
  }

  function render(m) {
    route("trivia");
    if (A.view !== "trivia") return;   // still on landing; render once joined
    switch (m.phase) {
      case "lobby": renderLobby(m); break;
      case "countdown": renderCount(m); break;
      case "question": renderQuestion(m); break;
      case "reveal": renderReveal(m); break;
      case "final": renderFinal(m); break;
    }
  }

  // One-time wiring for the persistent controls (script runs after the DOM
  // body, so these elements already exist).
  $("tv-lead-bar").addEventListener("click", function () {
    leadOpen = !leadOpen;
    A.sfx("tick");
    if (last) renderLead(last, false);
  });
  $("tv-ready").addEventListener("click", function () {
    A.sfx("buzz"); A.vibe(15);
    send({ t: "ready", ready: !myready });
  });
  $("tv-again").addEventListener("click", function () {
    A.sfx("start"); A.vibe(20);
    send({ t: "again" });
  });

  A.handlers.trivia = function (m) { last = m; render(m); };
})();
