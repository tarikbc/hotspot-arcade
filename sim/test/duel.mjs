// Connect Four is the 1v1 path: challenge, accept, alternating moves, and a win
// that scores on the Flipper leaderboard. Exercises the duel match/challenge system.
//
// Correction to the original assumption (verified against Engine::onInput /
// Engine::duelMove in esp32/hotspot-arcade-fw/ha_games.h -- see task-2-report.md):
//   - A move's column is keyed "n" (`ha_json_int(json, "n", &v)` -> duelMove(pid, v)),
//     not "c".
import assert from "node:assert/strict";
import { newEngine } from "./harness-lib.mjs";

const HA_GAME_C4 = 2; // HA_GAME_CONNECT4 in ha_proto.h

const e = await newEngine();
e.reset();
e.selectGame(HA_GAME_C4);
e.join(1, "ana");
e.join(2, "bo");

const challenged = e.input(1, { t: "challenge", to: 2 });
assert.ok(
  challenged.some((o) => o.to === "ws" || o.to === "all"),
  "challenging emits something to the clients",
);

e.input(2, { t: "accept", from: 1 });

// Vertical four in column 0 for player 1, player 2 answering in column 1.
let out = [];
for (let i = 0; i < 4; i++) {
  out = out.concat(e.input(1, { t: "move", n: 0 }));
  if (i < 3) out = out.concat(e.input(2, { t: "move", n: 1 }));
}

const score = out.find((o) => o.to === "uart" && o.kind === "score" && o.delta > 0);
assert.ok(score, "winning a duel scores on the Flipper leaderboard");
assert.equal(score.pid, 1, "the winner scores");

console.log("duel: OK");
