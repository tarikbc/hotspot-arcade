// Spyfall: everyone shares a location and a role there except one spy, who is told
// neither. This test drives the real engine through a whole game and pins the rules
// that a client bug could otherwise paper over:
//   * exactly one spy per round, and the spy rotates between rounds;
//   * the location NEVER reaches the spy before the reveal (the only location text in
//     their payload is the pack's full candidate list, which is identical every round
//     and so carries nothing);
//   * a role only ever reaches its own holder;
//   * the vote resolves (majority -> caught, split -> escaped);
//   * the spy calling the location ends the round immediately, right or wrong;
//   * the documented scoring: non-spies +1 when the spy is caught or calls it wrong,
//     spy +1 for surviving the vote, +2 for calling the location.
import assert from "node:assert/strict";
import { newEngine, lastToWs } from "./harness-lib.mjs";

const SF = 19;
const PIDS = [1, 2, 3];
// Blocks exactly as the Flipper streams them: one JSON pair per "Key: value" line, so
// the several "R:" role lines arrive as the SAME key repeated in one object.
const ITEMS = [
  '{"loc":"Beach","r":"Lifeguard","r":"Surfer","r":"Ice cream vendor"}',
  '{"loc":"Hospital","r":"Surgeon","r":"Nurse","r":"Patient"}',
  '{"loc":"Airport","r":"Pilot","r":"Passenger","r":"Security officer"}',
  '{"loc":"Circus","r":"Clown","r":"Acrobat","r":"Ringmaster"}',
];
const ALL_LOCS = ["Beach", "Hospital", "Airport", "Circus"];

const e = await newEngine();
let now = 0;
const tickTo = (ms) => { now = ms; return e.tick(ms); };

function loadPacks() {
  e.contentClear();
  e.contentPack(SF, "Test");
  for (const it of ITEMS) e.contentItem(it);
}

// Every connected player's latest spyfall payload, keyed by pid.
function views(out) {
  const v = {};
  for (const pid of PIDS) {
    const m = lastToWs(out, pid, "spyfall");
    if (m) v[pid] = m.msg;
  }
  return v;
}

// The round's secret, read from a non-spy (who is allowed to know it).
function inspect(v) {
  const spies = PIDS.filter((p) => v[p] && v[p].spy);
  assert.equal(spies.length, 1, "exactly one spy per round");
  const spy = spies[0];
  const others = PIDS.filter((p) => p !== spy);
  const loc = v[others[0]].loc;
  assert.ok(ALL_LOCS.includes(loc), "non-spies are told the location");
  for (const p of others) assert.equal(v[p].loc, loc, "the table shares one location");

  // --- the hidden-information rule ---
  const sm = v[spy];
  assert.equal(sm.loc, undefined, "the spy is never sent the location");
  assert.equal(sm.role, undefined, "the spy holds no role");
  assert.deepEqual(sm.locs, ALL_LOCS, "the spy gets the pack's full candidate list");
  // Strip that list and the location must not appear anywhere else in the payload.
  const rest = JSON.stringify({ ...sm, locs: undefined });
  assert.ok(!rest.includes(loc), "no other field leaks the location to the spy: " + rest);
  assert.equal(sm.roles, undefined, "the role table only exists on the reveal");

  // --- roles are private ---
  const roles = {};
  for (const p of others) {
    assert.equal(typeof v[p].role, "string", "a non-spy is told their own role");
    assert.ok(v[p].role.length > 0);
    roles[p] = v[p].role;
    assert.equal(v[p].roles, undefined, "the role table only exists on the reveal");
    assert.equal(v[p].locs, undefined, "only the spy needs the candidate list");
  }
  for (const p of others) {
    for (const q of others) {
      if (p === q || roles[p] === roles[q]) continue;
      assert.ok(
        !JSON.stringify(v[p]).includes(roles[q]),
        "player " + p + " must not see player " + q + "'s role",
      );
    }
  }
  return { spy, others, loc };
}

// --- a full game -------------------------------------------------------------------
e.reset();
for (const p of PIDS) e.join(p, "P" + p);
e.selectGame(SF);
loadPacks();

// Quorum: three players is the minimum, so two ready players must not start a game.
e.disconnect(3);
e.input(1, { t: "ready", ready: true });
let out = e.input(2, { t: "ready", ready: true });
assert.equal(lastToWs(out, 1, "spyfall").msg.phase, "lobby", "two players can't start");
assert.equal(lastToWs(out, 1, "spyfall").msg.need, 3, "the lobby advertises the quorum");
e.join(3, "P3");
e.input(1, { t: "ready", ready: true });
e.input(2, { t: "ready", ready: true });
e.input(3, { t: "ready", ready: true });
out = tickTo(3000);

const score = { 1: 0, 2: 0, 3: 0 };
const spies = [];

for (let round = 1; round <= 4; round++) {
  const gain = { 1: 0, 2: 0, 3: 0 };
  let outcome;
  let v = views(out);
  for (const p of PIDS) {
    assert.equal(v[p].phase, "play", "round " + round + " is live");
    assert.equal(v[p].stage, "talk", "the questioning stage comes first");
    assert.equal(v[p].round, round, "round counter");
    assert.equal(v[p].me, true, "everyone connected is dealt in");
  }
  const { spy, others, loc } = inspect(v);
  spies.push(spy);

  if (round === 1) {
    // Timer runs out -> everyone votes -> a majority names the spy.
    assert.ok(v[spy].deadline - now >= 300000, "a long questioning window (6 min)");
    out = tickTo(v[spy].deadline);
    v = views(out);
    assert.equal(v[spy].stage, "vote", "the talk timer opens the vote");
    assert.ok(v[spy].cands.length === 3, "everyone in the round is a candidate");
    // The spy still can't see the location during the vote.
    inspect(v);
    e.input(others[0], { t: "accuse", pid: spy });
    e.input(spy, { t: "accuse", pid: others[0] });
    out = e.input(others[1], { t: "accuse", pid: spy });
    for (const p of others) gain[p] = 1;
    outcome = "caught";
  } else if (round === 2) {
    // The spy calls the location correctly -> the round ends there and then.
    const idx = v[spy].locs.indexOf(loc);
    assert.ok(idx >= 0);
    out = e.input(spy, { t: "solve", loc: idx });
    gain[spy] = 2;
    outcome = "solved";
  } else if (round === 3) {
    // The spy calls it wrong -> the round still ends, and the table scores.
    const wrong = v[spy].locs.findIndex((n) => n !== loc);
    out = e.input(spy, { t: "solve", loc: wrong });
    for (const p of others) gain[p] = 1;
    outcome = "failed";
  } else {
    // A split vote: nobody gets a majority against the spy, so the spy walks.
    out = tickTo(v[spy].deadline);
    e.input(others[0], { t: "accuse", pid: others[1] });
    e.input(others[1], { t: "accuse", pid: others[0] });
    out = e.input(spy, { t: "accuse", pid: others[0] });
    gain[spy] = 1;
    outcome = "escaped";
  }
  for (const p of PIDS) score[p] += gain[p];

  // --- the reveal is public: everyone learns the location, the spy, and the roles ---
  const rv = views(out);
  for (const p of PIDS) {
    assert.equal(rv[p].stage, "reveal", "round " + round + " reveals");
    assert.equal(rv[p].outcome, outcome, "outcome of round " + round);
    assert.equal(rv[p].loc, loc, "the location is public on the reveal");
    assert.equal(rv[p].spyPid, spy, "the spy is named on the reveal");
    assert.equal(rv[p].roles.length, 3, "every seat's role is listed on the reveal");
    assert.equal(rv[p].roles.filter((r) => r.spy).length, 1, "one spy in the role table");
    assert.equal(rv[p].mygain, gain[p], "round " + round + " scored " + p);
  }
  const board = rv[spy].scores;
  for (const p of PIDS) {
    const row = board.find((x) => x.pid === p);
    assert.equal(row.score, score[p], "score for " + p + " after round " + round);
  }
  if (round < 4) out = tickTo(rv[spy].deadline);
}

// Four rounds played: the spy rotated every round, and the game lands on the podium.
assert.notEqual(spies[0], spies[1], "a new spy each round (" + spies.join(",") + ")");
assert.notEqual(spies[1], spies[2], "a new spy each round (" + spies.join(",") + ")");
assert.notEqual(spies[2], spies[3], "a new spy each round (" + spies.join(",") + ")");

out = tickTo(views(out)[spies[3]].deadline);
const fin = views(out);
for (const p of PIDS) {
  assert.equal(fin[p].phase, "final", "the game ends after 4 rounds");
  assert.equal(fin[p].board.length, 3, "the podium lists everyone");
}
assert.equal(fin[1].board[0].score, Math.max(score[1], score[2], score[3]),
  "the podium is ranked by the accumulated score");

// --- the spy leaving ends the round without scoring anyone -------------------------
e.reset();
now = 0;
for (const p of PIDS) e.join(p, "P" + p);
e.selectGame(SF);
loadPacks();
e.input(1, { t: "ready", ready: true });
e.input(2, { t: "ready", ready: true });
e.input(3, { t: "ready", ready: true });
out = tickTo(3000);
const live = inspect(views(out));
out = e.disconnect(live.spy);
const rest = PIDS.filter((p) => p !== live.spy);
for (const p of rest) {
  const m = lastToWs(out, p, "spyfall");
  assert.equal(m.msg.stage, "reveal", "the round ends when the spy walks out");
  assert.equal(m.msg.outcome, "aborted", "an abandoned round has no result");
  assert.equal(m.msg.mygain, 0, "nobody scores off an abandoned round");
  for (const row of m.msg.scores) assert.equal(row.score, 0, "no score moved");
}

console.log("spyfall: all checks passed");
