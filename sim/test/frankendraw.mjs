// Frankendraw: the exquisite-corpse drawing game. Everyone starts a sheet, the sheets
// rotate one seat per round, and head / torso / legs each come from a different hand.
// Drives a full three-player game and checks the parts that are easy to get wrong:
// the rotation, the per-player visibility gate (a drawer may see ONLY the sliver of the
// panel above theirs, never the rest of it and never another sheet), the sliver being
// exactly what it claims to be, the gallery reveal, the artwork sink, and the vote.
import assert from "node:assert/strict";
import { newEngine, lastToWs } from "./harness-lib.mjs";

const FD = 20;
const PIDS = [1, 2, 3];
const NICK = { 1: "ALICE", 2: "BOB", 3: "CARA" };

const e = await newEngine();
e.reset();
e.join(1, NICK[1]);
e.join(2, NICK[2]);
e.join(3, NICK[3]);
e.selectGame(FD);

// Lobby -> all ready -> countdown -> panel 1. Frankendraw has no content packs.
e.input(1, { t: "ready", ready: true });
e.input(2, { t: "ready", ready: true });
let out = e.input(3, { t: "ready", ready: true });
for (let ms = 1000; ms <= 4000; ms += 1000) out = out.concat(e.tick(ms));

const view = (items, pid) => lastToWs(items, pid, "frankendraw").msg;
// Quantise a 0..1 wire coordinate the way the engine does, so the test can predict
// exactly which stored value a stroke turns into.
const q = (v) => Math.floor(v * 255 + 0.5);
const quads = (flat) => {
  const o = [];
  for (let i = 0; i < flat.length; i += 4) o.push(flat.slice(i, i + 4));
  return o;
};

const held = []; // held[round-1][pid] = sheet index that player held that round
const lows = {}; // "sheet/panel" -> x of the segment drawn inside the sliver
const highsY = {}; // "sheet/panel" -> y of the segment drawn well above the sliver

for (let round = 1; round <= 3; round++) {
  const map = {};
  for (const pid of PIDS) {
    const m = view(out, pid);
    assert.equal(m.phase, "draw", "round " + round + " is a drawing round");
    assert.equal(m.round, round);
    assert.equal(m.panel, round - 1, "panel index follows the round");
    assert.equal(m.top, (round - 1) * m.band, "the band starts where the panel does");
    assert.equal(m.bot, round * m.band);
    map[pid] = m.sheet;

    if (round === 1) {
      assert.deepEqual(m.ink, [], "nothing to see above the head panel");
    } else {
      // Everything this player may see is the sliver of the panel above, on the sheet
      // now in their hands -- and nothing else.
      const key = m.sheet + "/" + (m.panel - 1);
      const seen = quads(m.ink);
      assert.equal(seen.length, 1, "exactly the one segment drawn inside the sliver");
      const [x0, y0, x1, y1] = seen[0];
      assert.equal(x0, lows[key], "the sliver segment is the previous drawer's");
      assert.equal(x1, lows[key]);
      assert.ok(
        y0 >= m.top - m.over && y1 >= m.top - m.over,
        "no ink from above the sliver line (" + y0 + "," + y1 + " vs " + (m.top - m.over) + ")",
      );
      assert.ok(y0 <= m.top && y1 <= m.top, "the sliver stays in the panel above");
      // Exactly one segment came through and it is the sliver one, so the segment
      // that drawer put at the TOP of the same panel did not.
      assert.notEqual(y0, highsY[key], "the top of the panel above is not leaked");
      // Nor did any other sheet's panel: each sheet's panel carries its own drawer's
      // marker, and only this one's is here.
      for (let other = 0; other < 3; other++) {
        if (other === m.sheet) continue;
        assert.notEqual(x0, lows[other + "/" + (m.panel - 1)], "only the held sheet is serialised");
      }
    }

    // Two segments: one near the top of my band (private), one in the bottom sliver
    // (the next drawer's only clue). x doubles as a per-player marker.
    const mark = pid / 10;
    const yHigh = (m.top + 2) / m.unit;
    const yLow = (m.bot - 1) / m.unit;
    e.input(pid, { t: "stroke", x0: mark, y0: yHigh, x1: mark, y1: yHigh });
    e.input(pid, { t: "stroke", x0: mark, y0: yLow, x1: mark, y1: yLow });
    highsY[m.sheet + "/" + m.panel] = q(yHigh);
    lows[m.sheet + "/" + m.panel] = q(mark);
  }
  held.push(map);
  // Everyone taps done: the panel ends early rather than running its 75s timer.
  for (const pid of PIDS) out = e.input(pid, { t: "done" });
}

// --- rotation -----------------------------------------------------------------
for (let s = 0; s < 3; s++) {
  const hands = held.map((map) => PIDS.find((p) => map[p] === s));
  assert.equal(new Set(hands).size, 3, "sheet " + s + " passed through three different hands");
}
for (const pid of PIDS) {
  assert.equal(new Set(held.map((m) => m[pid])).size, 3, "a player never redraws a sheet");
}

// --- gallery + artwork sink ---------------------------------------------------
const whoOf = (s) => held.map((map) => NICK[PIDS.find((p) => map[p] === s)]);

function checkShow(items, n) {
  const g = view(items, 1);
  assert.equal(g.phase, "show", "the gallery walks the finished sheets");
  assert.equal(g.n, n);
  assert.equal(g.total, 3);
  assert.deepEqual(g.who, whoOf(n), "the reveal names the three contributors, in order");
  assert.equal(new Set(g.who).size, 3, "three different contributors");
  assert.equal(g.ink.length, 3, "the reveal carries all three panels");
  for (const panel of g.ink) assert.equal(panel.length, 8, "two segments in every panel");
  // Everyone sees the same finished sheet now.
  for (const pid of PIDS) assert.deepEqual(view(items, pid).ink, g.ink);
}

function checkArt(items, n) {
  const art = items.filter((o) => o.to === "uart" && o.kind === "art");
  const begins = art.filter((a) => a.op === 0);
  const ends = art.filter((a) => a.op === 2);
  const strokes = art.filter((a) => a.op === 1);
  assert.equal(begins.length, 1, "the artwork sink fires once per completed sheet");
  assert.equal(ends.length, 1);
  assert.equal(begins[0].json.id, n);
  assert.equal(begins[0].json.game, "frankendraw");
  const saved = [begins[0].json.w0, begins[0].json.w1, begins[0].json.w2];
  assert.deepEqual(saved, whoOf(n), "the saved sheet credits its three drawers");
  assert.equal(strokes.length, 6, "every stored segment is streamed: two per panel");
  for (let p = 0; p < 3; p++) {
    const mine = strokes.filter((s) => s.json.p === p);
    assert.equal(mine.length, 2);
    const xs = mine.map((s) => s.json.x0);
    assert.ok(xs.every((x) => x === lows[n + "/" + p]), "the sink carries this panel's ink");
  }
}

checkShow(out, 0);
checkArt(out, 0);

out = e.tick(10000);
checkShow(out, 1);
checkArt(out, 1);
out = e.tick(16000);
checkShow(out, 2);
checkArt(out, 2);

// --- favourite-creature vote --------------------------------------------------
out = e.tick(22000);
const v = view(out, 1);
assert.equal(v.phase, "vote", "the gallery ends in a vote");
assert.equal(v.sheets.length, 3);
assert.equal(v.mine, -1);
assert.deepEqual(v.sheets[1].who, whoOf(1));

e.input(1, { t: "vote", sheet: 1 });
e.input(2, { t: "vote", sheet: 1 });
out = e.input(3, { t: "vote", sheet: 1 });

const f = view(out, 1);
assert.equal(f.phase, "final");
assert.equal(f.best, 1, "the most-voted sheet is crowned");
assert.equal(f.votes, 3);
assert.deepEqual(f.who, whoOf(1));
const paid = out.filter((o) => o.to === "uart" && o.kind === "score" && o.reason === "frankendraw");
assert.equal(paid.length, 3, "all three contributors of the winning sheet are paid");
for (const s of paid) assert.equal(s.delta, 300, "3 votes x 100 points");
assert.ok(f.board.every((p) => p.score === 300), "with three players every sheet is everyone's");

// --- the three-player floor ----------------------------------------------------
// A sheet has to pass through three hands, so two players can never start one.
e.disconnect(3);
e.input(1, { t: "ready", ready: true });
out = e.input(2, { t: "ready", ready: true });
const lob = view(out, 1);
assert.equal(lob.phase, "lobby", "two ready players do not start a game");
assert.equal(lob.need, 3);

console.log("frankendraw: all checks passed");
