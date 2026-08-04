/* Werewolf — hidden-role social deduction. The ESP is authoritative and holds
   every role; this module only draws what the server chose to tell THIS phone.
   {t:"werewolf", phase, ...}: lobby (ready up, needs 5) -> countdown -> play with
   a stage of roles / night / dawn / day / dusk, looping until one side wins ->
   final (every role revealed + podium). We send ready / kill / see / accuse /
   again. The talking happens in the room, not on the phone. */
(function () {
  var myready = false;
  var ROLE_ICON = { 1: "🧑‍🌾", 2: "🐺", 3: "🔮" };
  var ROLE_KEY = { 1: "werewolf.role_villager", 2: "werewolf.role_wolf", 3: "werewolf.role_seer" };
  function roleName(r) { return ROLE_KEY[r] ? t(ROLE_KEY[r]) : ""; }

  function sub(name) {
    ["lobby", "count", "play", "final"].forEach(function (id) {
      $("ww-" + id).classList.toggle("hide", id !== name);
    });
  }
  function stopBar() { A.timebarStop("ww-bar"); hide("ww-bar"); }

  function renderLobby(m) {
    sub("lobby");
    stopBar();
    $("werewolf").classList.remove("ww-night");
    myready = A.readyLobby({ players: m.players, listId: "ww-players", readyId: "ww-ready", meId: "ww-me" });
    var n = (m.players || []).length;
    $("ww-need").textContent = m.enough ? "" : t("werewolf.need", { min: m.min, n: n });
  }

  function renderCount(m) {
    sub("count");
    stopBar();
    A.countdown("ww-count-num", m.sec);
  }

  /* One tappable row per player. pickable() decides which pids can be tapped and
     onPick() fires for them; counts is a pid -> tally map drawn as a badge. */
  function renderList(m, pickable, counts, onPick) {
    var box = $("ww-list");
    box.innerHTML = "";
    (m.players || []).forEach(function (p) {
      var row = document.createElement("div");
      var cls = "ww-row";
      if (!p.alive || !p.in) cls += " out";
      if (p.pid === m.you) cls += " self";
      var can = pickable && pickable(p);
      if (can) cls += " tap";
      row.className = cls;
      // The role span is only ever filled from what the server sent: no role
      // field means this phone was not told, and nothing is inferred locally.
      var tag = p.role
        ? '<span class="ww-tag r' + p.role + '">' + ROLE_ICON[p.role] + " " + esc(roleName(p.role)) + "</span>"
        : (!p.in ? '<span class="ww-tag ghost">' + esc(t("werewolf.watching")) + "</span>" : "");
      var n = counts && counts[p.pid];
      row.innerHTML =
        '<span class="ww-av">' + esc(p.avatar || "🙂") + "</span>" +
        '<span class="ww-name">' + esc(p.nick) + "</span>" +
        tag +
        (n ? '<span class="ww-count">' + n + "</span>" : "");
      if (can) row.addEventListener("click", function () {
        A.sfx("buzz"); A.vibe(15);
        onPick(p.pid);
      });
      box.appendChild(row);
    });
  }

  function tally(list) {
    var c = {};
    (list || []).forEach(function (v) { c[v.pid] = (c[v.pid] || 0) + 1; });
    return c;
  }
  function nickOfPid(m, pid) {
    var p = (m.players || []).find(function (x) { return x.pid === pid; });
    return p ? p.nick : "";
  }
  function roleOfPid(m, pid) {
    var p = (m.players || []).find(function (x) { return x.pid === pid; });
    return p && p.role ? roleName(p.role) : "";
  }

  var lastStage = "", lastDay = -1;
  function renderPlay(m) {
    sub("play");
    var stage = m.stage;
    var alive = !!m.alive, iam = m.myrole || 0;
    var night = (stage === "night" || stage === "roles" || stage === "dawn");
    $("werewolf").classList.toggle("ww-night", night);
    $("ww-meta").textContent = stage === "roles" ? t("werewolf.roles_title")
      : night ? t("werewolf.night", { n: m.day }) : t("werewolf.day", { n: m.day });
    $("ww-role").textContent = iam ? ROLE_ICON[iam] + " " + roleName(iam) : "";
    $("ww-counts").textContent = t("werewolf.counts", { v: m.villagersleft, w: m.wolvesleft });

    noteDeadline(m.deadline, m.dur);
    A.timebar("ww-bar", m.deadline, m.dur, false);

    var banner = $("ww-banner"), note = $("ww-note"), check = $("ww-check");
    check.classList.add("hide");
    // The seer's reading, whenever the server saw fit to include it. Nobody else
    // ever receives this field, so there is nothing to hide client-side.
    if (m.check) {
      check.classList.remove("hide");
      check.className = "ww-check " + (m.check.wolf ? "bad" : "good");
      check.textContent = m.check.wolf
        ? t("werewolf.check_wolf", { nick: m.check.nick })
        : t("werewolf.check_clear", { nick: m.check.nick });
    }

    if (stage === "roles") {
      banner.textContent = t("werewolf.you_are", { role: roleName(iam) });
      note.textContent = iam === 2 ? t("werewolf.pack_note") : t("werewolf.roles_note");
      renderList(m, null, null, null);
    } else if (stage === "night") {
      banner.textContent = t("werewolf.night_falls");
      if (!iam) {
        note.textContent = t("werewolf.spectating");
        renderList(m, null, null, null);
      } else if (!alive) {
        note.textContent = t("werewolf.dead_note");
        renderList(m, null, tally(m.packvotes), null);
      } else if (iam === 2) {
        var picked = typeof m.mykill === "number" && m.mykill > 0;
        note.textContent = picked
          ? t("werewolf.victim_picked", { nick: nickOfPid(m, m.mykill) })
          : t("werewolf.pick_victim");
        renderList(m, function (p) { return p.in && p.alive && p.role !== 2; },
          tally(m.packvotes), function (pid) { send({ t: "kill", n: pid }); });
      } else if (iam === 3 && !m.check) {
        note.textContent = t("werewolf.pick_check");
        renderList(m, function (p) { return p.in && p.alive && p.pid !== m.you; },
          null, function (pid) { send({ t: "see", n: pid }); });
      } else {
        note.textContent = iam === 3 ? t("werewolf.checked") : t("werewolf.sleep");
        renderList(m, null, null, null);
      }
    } else if (stage === "dawn") {
      banner.textContent = t("werewolf.dawn");
      note.textContent = m.victim
        ? t("werewolf.died", { nick: nickOfPid(m, m.victim), role: roleOfPid(m, m.victim) })
        : t("werewolf.nobody_died");
      renderList(m, null, null, null);
    } else if (stage === "day") {
      banner.textContent = t("werewolf.day_banner");
      var mine = (typeof m.myvote === "number" && m.myvote > 0) ? m.myvote : 0;
      note.textContent = !iam ? t("werewolf.spectating")
        : !alive ? t("werewolf.dead_note")
          : mine ? t("werewolf.voted", { nick: nickOfPid(m, mine) })
            : t("werewolf.vote_note");
      renderList(m, function (p) {
        return iam && alive && p.in && p.alive && p.pid !== m.you;
      }, tally(m.votes), function (pid) { send({ t: "accuse", n: pid }); });
    } else { // dusk
      banner.textContent = t("werewolf.dusk");
      note.textContent = m.lynched
        ? t("werewolf.voted_out", { nick: nickOfPid(m, m.lynched), role: roleOfPid(m, m.lynched) })
        : t("werewolf.no_majority");
      renderList(m, null, null, null);
    }

    if (stage !== lastStage || m.day !== lastDay) {
      if (stage === "dawn" || stage === "dusk") { A.sfx("buzz"); A.vibe(25); }
      else if (stage === "night") { A.sfx("tick"); A.vibe(12); }
      lastStage = stage; lastDay = m.day;
    }
  }

  function renderFinal(m) {
    sub("final");
    stopBar();
    $("werewolf").classList.remove("ww-night");
    var wolves = m.winner === "wolves";
    $("ww-winner").textContent = wolves ? t("werewolf.wolves_win") : t("werewolf.village_wins");
    $("ww-winner").className = "ww-winner " + (wolves ? "bad" : "good");
    var box = $("ww-roles");
    box.innerHTML = "";
    (m.players || []).forEach(function (p) {
      if (!p.in) return;
      var row = document.createElement("div");
      row.className = "ww-row" + (p.alive ? "" : " out") + (p.pid === m.you ? " self" : "");
      row.innerHTML =
        '<span class="ww-av">' + esc(p.avatar || "🙂") + "</span>" +
        '<span class="ww-name">' + esc(p.nick) + "</span>" +
        '<span class="ww-tag r' + p.role + '">' + ROLE_ICON[p.role] + " " + esc(roleName(p.role)) + "</span>";
      box.appendChild(row);
    });
    A.podium("ww-podium", m.board);
    var iWon = (m.myrole === 2) === wolves;
    if (iWon) { A.sfx("win"); A.vibe([30, 50, 30]); } else { A.sfx("start"); A.vibe(20); }
    lastStage = ""; lastDay = -1;
  }

  A.handlers.werewolf = function (m) {
    route("werewolf");
    if (A.view !== "werewolf") return;
    switch (m.phase) {
      case "lobby": renderLobby(m); break;
      case "countdown": renderCount(m); break;
      case "play": renderPlay(m); break;
      case "final": renderFinal(m); break;
    }
  };

  $("ww-ready").addEventListener("click", function () {
    A.sfx("buzz"); A.vibe(15);
    send({ t: "ready", ready: !myready });
  });
  $("ww-again").addEventListener("click", function () {
    A.sfx("start"); A.vibe(20);
    send({ t: "again" });
  });
})();
