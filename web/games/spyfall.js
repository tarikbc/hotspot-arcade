/* Spyfall — everyone at the table shares a location and holds a role there, except
   one player: the spy, who is told neither. The ESP is authoritative and drives
   {t:"spyfall", phase, ...}: lobby (ready + pack vote) -> countdown -> play (stage
   talk: question each other out loud while a 6-minute clock runs; stage vote: everyone
   accuses someone; stage reveal: location, spy and roles) -> ... -> final podium.
   We send ready / vote / accuse / solve / again.

   This module never learns anything it isn't sent: the location simply isn't in the
   spy's payload until the reveal, and a role only ever arrives for its own holder. */
(function () {
  var myready = false;
  var armed = -1;   // location index the spy tapped once; a second tap calls it
  var ticker = null; // mm:ss clock interval

  function sub(name) {
    ["lobby", "count", "play", "final"].forEach(function (id) {
      $("sf-" + id).classList.toggle("hide", id !== name);
    });
  }
  function stopBar() { A.timebarStop("sf-bar"); hide("sf-bar"); }
  function stopClock() {
    if (ticker) { clearInterval(ticker); ticker = null; }
    $("sf-clock").textContent = "";
  }
  // A readable mm:ss next to the bar: six minutes of talking is far too long to judge
  // from a shrinking bar alone.
  function startClock(deadline) {
    stopClock();
    function paint() {
      var left = Math.max(0, Math.round((deadline - (Date.now() + A.offset)) / 1000));
      var m = Math.floor(left / 60), s = left % 60;
      $("sf-clock").textContent = m + ":" + (s < 10 ? "0" : "") + s;
      $("sf-clock").classList.toggle("hot", left <= 30);
    }
    paint();
    ticker = setInterval(paint, 500);
  }

  function renderLobby(m) {
    sub("lobby");
    stopBar(); stopClock();
    myready = A.readyLobby({ players: m.players, listId: "sf-players", readyId: "sf-ready", meId: "sf-me" });
    var short = (m.players || []).length < m.need;
    $("sf-need").textContent = short ? t("sf.need", { n: m.need }) : "";
    $("sf-need").classList.toggle("hide", !short);
    A.packVote({
      boxId: "sf-topics",
      packs: m.packs, myvote: m.myvote,
      onVote: function (i) { send({ t: "vote", pack: i }); },
    });
  }

  function renderCount(m) {
    sub("count");
    stopBar(); stopClock();
    A.countdown("sf-count-num", m.sec);
  }

  function card(kind, kicker, big, sub2) {
    var c = $("sf-card");
    c.className = "sf-card" + (kind ? " " + kind : "");
    c.innerHTML =
      '<span class="sf-kicker">' + esc(kicker) + "</span>" +
      '<span class="sf-big">' + esc(big) + "</span>" +
      (sub2 ? '<span class="sf-sub">' + esc(sub2) + "</span>" : "");
  }

  function row(cls, left, right) {
    var d = document.createElement("div");
    d.className = "sf-row" + (cls ? " " + cls : "");
    d.innerHTML = '<span class="sf-rl">' + esc(left) + "</span>" +
                  '<span class="sf-rr">' + esc(right) + "</span>";
    return d;
  }

  // The spy's candidate list. One tap arms an entry, a second calls it — calling ends
  // the round for everyone, so it must not be a single stray tap.
  function renderLocations(locs) {
    var box = $("sf-list");
    (locs || []).forEach(function (name, i) {
      var r = row("tap" + (armed === i ? " armed" : ""), name,
                  armed === i ? t("sf.call_confirm") : "");
      r.addEventListener("click", function () {
        if (armed === i) { A.sfx("start"); A.vibe(30); send({ t: "solve", loc: i }); armed = -1; return; }
        armed = i;
        A.sfx("buzz"); A.vibe(12);
        box.innerHTML = "";
        renderLocations(locs);
      });
      box.appendChild(r);
    });
  }

  function renderVote(m) {
    var box = $("sf-list");
    var locked = m.myvote > 0;
    (m.cands || []).forEach(function (p) {
      if (p.pid === A.pid) return; // you can't accuse yourself
      var mineTag = (locked && m.myvote === p.pid) ? "✓" : "";
      var r = row((locked ? "" : "tap") + (mineTag ? " armed" : ""),
                  (p.avatar || "🙂") + " " + p.nick, mineTag);
      if (!locked) r.addEventListener("click", function () {
        A.sfx("buzz"); A.vibe(18);
        send({ t: "accuse", pid: p.pid });
      });
      box.appendChild(r);
    });
    $("sf-note").textContent = locked
      ? t("sf.vote_locked", { n: m.voted, total: (m.cands || []).length })
      : t("sf.vote_hint");
  }

  function renderReveal(m) {
    var win = m.mygain > 0;
    var head = m.outcome === "caught" ? t("sf.out_caught")
      : m.outcome === "escaped" ? t("sf.out_escaped")
        : m.outcome === "solved" ? t("sf.out_solved")
          : m.outcome === "failed" ? t("sf.out_failed", { loc: m.called || "?" })
            : t("sf.out_aborted");
    card(win ? "good" : "bad", head, m.loc, t("sf.spy_was", { nick: m.spyNick }));
    $("sf-listhead").textContent = t("sf.reveal_roles");
    show("sf-listhead");
    var box = $("sf-list");
    (m.roles || []).forEach(function (r) {
      var voted = null;
      (m.votes || []).forEach(function (v) { if (v.pid === r.pid) voted = v; });
      var right = voted ? t("sf.voted_for", { nick: voted.for }) : "";
      box.appendChild(row(r.spy ? "spy" : "", r.nick + " · " + (r.spy ? t("sf.the_spy") : r.role), right));
    });
    $("sf-note").textContent = m.mygain ? t("sf.gain", { g: m.mygain }) : t("common.zero_round");
  }

  var lastRound = -1, lastStage = "";
  function renderPlay(m) {
    sub("play");
    $("sf-meta").textContent = t("common.round", { n: m.round, total: m.rounds });
    noteDeadline(m.deadline, m.dur);
    A.timebar("sf-bar", m.deadline, m.dur, false);
    startClock(m.deadline);
    if (lastRound !== m.round) armed = -1;

    $("sf-list").innerHTML = "";
    hide("sf-listhead");
    $("sf-note").textContent = "";

    if (m.stage === "reveal") {
      renderReveal(m);
      if (lastRound !== m.round || lastStage !== "reveal") {
        A.sfx(m.mygain ? "correct" : "buzz"); A.vibe(m.mygain ? 25 : 12);
      }
    } else if (!m.me) {
      // Joined mid-round: no location, no role, nothing to give away.
      card("", t("sf.waiting_kicker"), t("sf.waiting"), "");
      $("sf-note").textContent = t("sf.waiting_note");
    } else if (m.spy) {
      card("spy", t("sf.you_are"), t("sf.the_spy"), t("sf.spy_hint"));
      if (m.stage === "talk") {
        $("sf-listhead").textContent = t("sf.locations");
        show("sf-listhead");
        renderLocations(m.locs);
        $("sf-note").textContent = t("sf.call_hint");
      } else {
        renderVote(m);
      }
    } else {
      card("", t("sf.location"), m.loc, t("sf.your_role", { role: m.role }));
      if (m.stage === "talk") $("sf-note").textContent = t("sf.role_hint");
      else renderVote(m);
    }
    lastRound = m.round; lastStage = m.stage;
  }

  function renderFinal(m) {
    sub("final");
    stopBar(); stopClock();
    var b = A.podium("sf-podium", m.board);
    if (b && b.length && b[0].pid === A.pid) { A.sfx("win"); A.vibe([30, 50, 30]); }
    else { A.sfx("start"); A.vibe(20); }
  }

  A.handlers.spyfall = function (m) {
    route("spyfall");
    if (A.view !== "spyfall") return;
    switch (m.phase) {
      case "lobby": renderLobby(m); break;
      case "countdown": renderCount(m); break;
      case "play": renderPlay(m); break;
      case "final": renderFinal(m); break;
    }
  };

  $("sf-ready").addEventListener("click", function () {
    A.sfx("buzz"); A.vibe(15);
    send({ t: "ready", ready: !myready });
  });
  $("sf-again").addEventListener("click", function () {
    A.sfx("start"); A.vibe(20);
    send({ t: "again" });
  });
})();
