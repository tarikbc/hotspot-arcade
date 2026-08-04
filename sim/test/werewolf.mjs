// Werewolf: hidden-role social deduction. Roles are dealt on the ESP and the
// per-player serializer is the only thing standing between a villager and the
// wolf list -- so most of this file is a standing audit of every payload the
// engine emits, not just a happy-path walk.
//
// Covered: role counts, the secrecy invariant (no player's role in anyone else's
// payload before a legitimate reveal), wolves seeing each other, the seer's
// reading reaching only the seer, a night kill and a day vote resolving, both win
// conditions, and the last werewolf disconnecting.
import assert from "node:assert/strict";
import { newEngine, lastToWs } from "./harness-lib.mjs";

const WW = 18;
const VILLAGER = 1, WOLF = 2, SEER = 3;
const NICKS = ["ALICE", "BOB", "CARA", "DAN", "EVE", "FRANK", "GINA", "HUGO", "IVY"];

/**
 * Drive one game. wsId doubles as the pid (the engine hands them out in join
 * order). Every drained batch is absorbed: the latest view per player is kept
 * for assertions, and each one is run past the secrecy audit below.
 */
function mkGame(e, n) {
  const g = {
    e, n, ms: 0,
    view: {},                    // pid -> latest {t:"werewolf"} message
    role: {},                    // pid -> role, learned from that player's own myrole
    dead: new Set(),             // pids publicly known to be out (alive:false)
    over: false,
  };

  function audit(viewer, m) {
    if (m.myrole !== undefined && m.myrole !== 0) g.role[viewer] = m.myrole;
    if (m.phase === "final") g.over = true;
    (m.players || []).forEach(function (p) { if (p.in && !p.alive) g.dead.add(p.pid); });
    if (m.phase !== "play") return;
    // The invariant: a role in someone else's payload is only ever legitimate for
    // a fellow werewolf or for a player who is already dead.
    (m.players || []).forEach(function (p) {
      if (p.role === undefined) return;
      const ok = p.pid === viewer || g.dead.has(p.pid) ||
        (g.role[viewer] === WOLF && p.role === WOLF);
      assert.ok(ok, `role leak: pid ${viewer} (role ${g.role[viewer]}) saw pid ${p.pid} as role ${p.role}`);
    });
    if (m.check !== undefined) assert.equal(g.role[viewer], SEER, "only the seer gets a reading");
    if (m.packvotes !== undefined) assert.equal(g.role[viewer], WOLF, "only wolves see the pack tally");
  }

  function absorb(items) {
    for (const o of items) {
      if (o.to === "ws" && o.msg && o.msg.t === "werewolf") {
        g.view[o.id] = o.msg;
        audit(o.id, o.msg);
      }
    }
    return items;
  }

  g.tick = (to) => { g.ms = to; return absorb(e.tick(to)); };
  g.join = (wsId, nick) => absorb(e.join(wsId, nick));
  g.send = (pid, obj) => absorb(e.input(pid, obj));
  g.drop = (pid) => absorb(e.disconnect(pid));
  g.pids = () => { const a = []; for (let i = 1; i <= n; i++) a.push(i); return a; };
  g.of = (role) => g.pids().filter((p) => g.role[p] === role);
  g.living = () => g.pids().filter((p) => g.view[p] && g.view[p].alive);
  g.stage = () => g.view[1].stage;
  return g;
}

/** Join n players, ready everyone up, and run the countdown into the role reveal. */
async function start(n) {
  const e = await newEngine();
  e.reset();
  for (let i = 1; i <= n; i++) e.join(i, NICKS[i - 1]);
  e.selectGame(WW);
  const g = mkGame(e, n);
  for (let i = 1; i <= n; i++) g.send(i, { t: "ready", ready: true });
  for (let ms = 1000; ms <= 3000; ms += 1000) g.tick(ms);
  return g;
}

/** Advance past the current phase deadline (all timers are generous). */
function skip(g) { return g.tick(g.ms + 200000); }

// ---------------------------------------------------------------------------
// 1. Eight players: role counts, secrecy, the pack, the seer, a full village win
// ---------------------------------------------------------------------------
{
  const g = await start(8);
  for (const p of g.pids()) {
    assert.equal(g.view[p].phase, "play", "dealt in after the countdown");
    assert.equal(g.view[p].stage, "roles", "the private role reveal comes first");
    assert.ok(g.view[p].myrole >= 1 && g.view[p].myrole <= 3, "everyone holds a role");
  }
  // 8 players -> 8/4 = 2 werewolves, exactly 1 seer, the rest villagers.
  assert.equal(g.of(WOLF).length, 2, "two werewolves at eight players");
  assert.equal(g.of(SEER).length, 1, "exactly one seer");
  assert.equal(g.of(VILLAGER).length, 5, "the rest are villagers");

  const wolves = g.of(WOLF), seer = g.of(SEER)[0];
  const village = g.pids().filter((p) => g.role[p] !== WOLF);

  // Werewolves see each other -- and nobody else.
  for (const w of wolves) {
    const seen = g.view[w].players.filter((p) => p.role !== undefined).map((p) => p.pid).sort();
    assert.deepEqual(seen, wolves.slice().sort(), "a wolf sees exactly the pack");
  }
  // Before anyone has died, a villager's payload carries no role but their own.
  for (const v of village) {
    const seen = g.view[v].players.filter((p) => p.role !== undefined).map((p) => p.pid);
    assert.deepEqual(seen, [v], `pid ${v} sees only their own role`);
  }

  // ---- night 1 ----
  skip(g); // role reveal window expires
  assert.equal(g.stage(), "night", "night falls");
  const prey = village.filter((p) => p !== seer)[0];
  // The seer checks a wolf; only the seer is told.
  g.send(seer, { t: "see", n: wolves[0] });
  for (const p of g.pids()) {
    if (p === seer) {
      assert.equal(g.view[p].check.pid, wolves[0], "the seer gets their reading");
      assert.equal(g.view[p].check.wolf, true, "...and it names the wolf");
    } else {
      assert.equal(g.view[p].check, undefined, `pid ${p} must not see the reading`);
    }
  }
  // A villager cannot fake a night kill, and a wolf cannot eat the pack.
  g.send(prey, { t: "kill", n: wolves[0] });
  g.send(wolves[0], { t: "kill", n: wolves[1] });
  assert.equal(g.view[wolves[0]].packvotes.length, 0, "neither tap was accepted");
  // Both wolves agree; the second tap completes the night and it resolves at once.
  g.send(wolves[0], { t: "kill", n: prey });
  g.send(wolves[1], { t: "kill", n: prey });
  assert.equal(g.stage(), "dawn", "the night resolves once every wolf has picked");
  assert.equal(g.view[1].victim, prey, "the agreed victim is taken");
  for (const p of g.pids()) {
    const row = g.view[p].players.find((x) => x.pid === prey);
    assert.equal(row.alive, false, "the body is public");
    assert.equal(row.role, g.role[prey], "a dead player's role is revealed to everyone");
  }

  // ---- day 1: the village votes out a wolf ----
  skip(g); // dawn announcement expires
  assert.equal(g.stage(), "day", "day breaks");
  const jury = g.living().filter((p) => p !== wolves[0]);
  for (const p of jury) g.send(p, { t: "accuse", n: wolves[0] });
  assert.equal(g.stage(), "day", "still open: the accused has not voted");
  g.send(wolves[0], { t: "accuse", n: wolves[0] }); // nobody votes for themselves
  assert.equal(g.stage(), "day", "a self-accusation is refused");
  g.send(wolves[0], { t: "accuse", n: seer }); // the wolf deflects, and is outvoted
  assert.equal(g.stage(), "dusk", "the vote resolves once everyone alive has voted");
  assert.equal(g.view[1].lynched, wolves[0], "the accused is voted out");
  assert.equal(
    g.view[prey].players.find((x) => x.pid === wolves[0]).role, WOLF,
    "the eliminated player's role is revealed",
  );

  // ---- run it out: the village lynches the second wolf too ----
  skip(g); // dusk -> night 2
  assert.equal(g.stage(), "night", "night two");
  const nextPrey = g.living().filter((p) => g.role[p] !== WOLF && p !== seer)[0];
  g.send(wolves[1], { t: "kill", n: nextPrey });
  skip(g); // dawn
  skip(g); // day 2
  assert.equal(g.stage(), "day");
  for (const p of g.living()) {
    g.send(p, { t: "accuse", n: p === wolves[1] ? seer : wolves[1] });
  }
  skip(g); // dusk -> the win check
  for (const p of g.pids()) {
    const m = g.view[p];
    assert.equal(m.phase, "final", "the game ends with the last wolf gone");
    assert.equal(m.winner, "villagers", "villagers win");
    assert.ok(m.players.every((x) => !x.in || x.role !== undefined), "every role is revealed at the end");
  }
  // 1 point per surviving winner: the villagers still standing, and nobody else.
  const board = g.view[1].board;
  const survivors = g.view[1].players
    .filter((x) => x.in && x.alive && x.role !== WOLF).map((x) => x.pid);
  for (const row of board) {
    const want = survivors.includes(row.pid) ? 1 : 0;
    assert.equal(row.score, want, `pid ${row.pid} scored ${row.score}, expected ${want}`);
  }
  assert.ok(survivors.length >= 1, "somebody survived to score");
}

// ---------------------------------------------------------------------------
// 2. Five players: the wolves eat their way to a win, and a mid-game joiner
//    watches from outside the game
// ---------------------------------------------------------------------------
{
  const g = await start(5);
  assert.equal(g.of(WOLF).length, 1, "one werewolf at five players");
  const wolf = g.of(WOLF)[0];
  skip(g); // roles -> night 1

  // Someone wanders in mid-game: no role, no vote, just a seat.
  g.join(9, "LATE");
  assert.equal(g.view[1].players.find((x) => x.pid === 6).in, false, "a late joiner is a spectator");
  assert.equal(g.view[1].players.find((x) => x.pid === 6).role, undefined, "and holds no role");

  // Quiet nights: the wolf eats, nobody is ever voted out, so the pack wins by
  // attrition. Every unattended window just times out, which is the point --
  // a silent room can't stall the game.
  const stages = new Set();
  for (let guard = 0; guard < 40 && !g.over; guard++) {
    stages.add(g.stage());
    if (g.stage() === "night") {
      const target = g.living().filter((p) => g.role[p] !== WOLF)[0];
      g.send(wolf, { t: "kill", n: target });
    }
    if (g.over) break;
    skip(g);
  }
  for (const st of ["night", "dawn", "day", "dusk"]) {
    assert.ok(stages.has(st), `the ${st} phase ran`);
  }
  for (const p of [1, 2, 3, 4, 5]) {
    assert.equal(g.view[p].phase, "final", "the game ended");
    assert.equal(g.view[p].winner, "wolves", "werewolves win once they are not outnumbered");
  }
  const board = g.view[wolf].board;
  assert.equal(board.find((r) => r.pid === wolf).score, 1, "the surviving wolf scores 1");
  for (const r of board) {
    if (r.pid !== wolf && r.pid !== 6) assert.equal(r.score, 0, "the losing village scores nothing");
  }
}

// ---------------------------------------------------------------------------
// 3. The last werewolf walks out mid-game: the village wins by default
// ---------------------------------------------------------------------------
{
  const g = await start(5);
  const wolf = g.of(WOLF)[0];
  skip(g); // roles -> night 1
  assert.equal(g.stage(), "night");
  g.drop(wolf);
  const left = g.pids().filter((p) => p !== wolf);
  for (const p of left) {
    assert.equal(g.view[p].phase, "final", "the game ends the moment the pack is gone");
    assert.equal(g.view[p].winner, "villagers", "villagers win when the last wolf leaves");
  }
  // The vacated pid is genuinely gone, not a ghost still holding a role.
  assert.equal(g.view[left[0]].players.some((x) => x.pid === wolf), false, "the leaver is off the roster");
  for (const r of g.view[left[0]].board) assert.equal(r.score, 1, "every surviving villager scores 1");
}

// ---------------------------------------------------------------------------
// 4. Under the minimum the game never starts
// ---------------------------------------------------------------------------
{
  const e = await newEngine();
  e.reset();
  for (let i = 1; i <= 4; i++) e.join(i, NICKS[i - 1]);
  e.selectGame(WW);
  let out = [];
  for (let i = 1; i <= 4; i++) out = e.input(i, { t: "ready", ready: true });
  for (let ms = 1000; ms <= 6000; ms += 1000) out = out.concat(e.tick(ms));
  const m = lastToWs(out, 1, "werewolf").msg;
  assert.equal(m.phase, "lobby", "four players is not a village");
  assert.equal(m.enough, false, "and the client is told so");
  assert.equal(m.min, 5, "the minimum is advertised");
}

console.log("werewolf: all checks passed");
