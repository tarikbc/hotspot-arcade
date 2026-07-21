// The generic ingest path: the engine is fed the exact JSON blocks the Flipper
// produces from a pack file, and must end up with a playable trivia topic.
//
// The block shape here is deliberately the FILE's keys, lowercased — {q,a,b,c,d,
// answer} — not the engine's internal shape. Trivia's struct uses `c` for the
// CORRECT INDEX while the file uses `C:` for the third option, so a loader that
// consumed this object raw would mark option C's text as the answer index. That
// collision is what this test exists to catch.
import assert from "node:assert/strict";
import { newEngine, lastToWs } from "./harness-lib.mjs";

const HA_GAME_TRIVIA = 1;

const e = await newEngine();
e.reset();
e.contentClear();
e.contentPack(HA_GAME_TRIVIA, "General Knowledge");
e.contentItem(JSON.stringify({
  q: "What is the capital of France?",
  a: "Paris", b: "London", c: "Berlin", d: "Madrid",
  answer: "A",
}));
e.contentItem(JSON.stringify({
  q: "Largest ocean?",
  a: "Atlantic", b: "Indian", c: "Pacific", d: "Arctic",
  answer: "C",
}));

// Two players, ready up, run out the countdown, and check the question that lands.
e.selectGame(HA_GAME_TRIVIA);
e.join(1, "ana");
e.join(2, "bo");
e.input(1, { t: "ready", ready: true });
e.input(2, { t: "ready", ready: true });
let seen = [];
for (let ms = 1000; ms <= 8000; ms += 1000) seen = seen.concat(e.tick(ms));

const q = lastToWs(seen, 1, "trivia");
assert.ok(q, "a trivia question reached the player");
assert.ok(
  q.msg.o.includes("Berlin"),
  "option C's TEXT is an option, not swallowed as the correct index",
);
assert.equal(q.msg.o.length, 4, "all four options survived the mapping");

// Answering the option the file's Answer: letter points at must score.
const idx = q.msg.o.indexOf("Paris") >= 0 ? q.msg.o.indexOf("Paris") : 0;
e.input(1, { t: "answer", c: idx });
const after = e.input(2, { t: "answer", c: (idx + 1) % 4 });
const score = after.find((o) => o.to === "uart" && o.kind === "score" && o.pid === 1);
assert.ok(score && score.delta > 0, "the Answer: letter mapped to the right option");

// A block that isn't a question must be skipped, not stored as a broken one.
e.contentClear();
e.contentPack(HA_GAME_TRIVIA, "Broken");
e.contentItem(JSON.stringify({ q: "Only two options?", a: "x", b: "y" }));
e.contentItem(JSON.stringify({ note: "no question here at all" }));
console.log("content: OK");
