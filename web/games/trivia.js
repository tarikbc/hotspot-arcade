/* Trivia module. Registers a handler for {t:"trivia"} messages. Renders the
   question, four big answer buttons, a cosmetic countdown bar, and the reveal.
   The server is the referee; we only send {t:"answer", c}. */
(function () {
  var LABELS = ["A", "B", "C", "D"];
  var last = null;      // last trivia message, so scores render across phases
  var barAnim = null;

  function rank(scores) {
    if (!scores || !scores.length) return "";
    var sorted = scores.slice().sort(function (a, b) { return b.score - a.score; });
    var me = sorted.findIndex(function (s) { return s.pid === A.pid; });
    var mine = A.pid != null ? scores.find(function (s) { return s.pid === A.pid; }) : null;
    if (me < 0) return "";
    return "#" + (me + 1) + " of " + sorted.length + " . " + (mine ? mine.score : 0) + " pts";
  }

  // Animate the bar from time-remaining down to zero using a transform so it
  // stays smooth without a per-frame timer. Turns red (.hot) in the last 3s.
  function runBar(deadline, dur) {
    var bar = $("tv-bar"), fill = bar.firstElementChild;
    if (barAnim) { clearTimeout(barAnim); barAnim = null; }
    show("tv-bar");
    var remain = deadline - serverNow();
    bar.classList.remove("hot");
    if (remain <= 0 || !dur) { fill.style.transition = "none"; fill.style.transform = "scaleX(0)"; bar.classList.add("hot"); return; }
    var frac = Math.max(0, Math.min(1, remain / dur));
    fill.style.transition = "none";
    fill.style.transform = "scaleX(" + frac + ")";
    // Next frame: animate to empty over the remaining time.
    requestAnimationFrame(function () {
      fill.style.transition = "transform " + remain + "ms linear";
      fill.style.transform = "scaleX(0)";
    });
    if (remain <= 3000) bar.classList.add("hot");
    else barAnim = setTimeout(function () { bar.classList.add("hot"); }, remain - 3000);
  }

  function render(m) {
    route("trivia");
    if (A.view !== "trivia") return;   // still on landing; render when joined
    var status = $("tv-status"), q = $("tv-q"), opts = $("tv-opts");

    if (m.phase === "idle") {
      status.textContent = "Get ready...";
      show("tv-status"); hide("tv-bar");
      q.textContent = ""; opts.innerHTML = "";
      $("tv-rank").textContent = rank(m.scores);
      return;
    }

    hide("tv-status");
    q.textContent = m.q || "";
    $("tv-rank").textContent = rank(m.scores);

    var reveal = m.phase === "reveal";
    var counts = m.counts || [];
    var mine = (typeof m.mine === "number") ? m.mine : -1;
    var picked = mine >= 0;

    if (reveal) { hide("tv-bar"); }
    else { noteDeadline(m.deadline, m.dur); runBar(m.deadline, m.dur); }

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
      // Lock buttons once answered or during reveal.
      if (reveal || picked) b.disabled = true;

      var cnt = reveal ? ('<span class="cnt">' + (counts[i] || 0) + "</span>") : "";
      b.innerHTML =
        '<span class="lab">' + LABELS[i] + "</span>" +
        '<span class="txt">' + esc(text) + "</span>" + cnt;

      b.addEventListener("click", function () {
        if (b.disabled) return;
        send({ t: "answer", c: i });
        // Optimistic lock; server confirms via the next trivia message.
        Array.prototype.forEach.call(opts.children, function (c) { c.disabled = true; });
        b.classList.add("mine");
      });
      opts.appendChild(b);
    });
  }

  A.handlers.trivia = function (m) { last = m; render(m); };
})();
