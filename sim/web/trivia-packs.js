// Parses the trivia pack text format so the harness can feed packs to the engine
// the way the Flipper does.
//
// KNOWN DUPLICATION: ha_session.c parses this same format in C. It is the only
// rule-ish logic implemented twice in this project (see the design doc). The format
// is frozen: "Pack:" / "Q:" / "A:".."D:" / "Answer:", blocks split by "---".
//
// Output shape matches what Engine::triviaAddQ actually parses (verified against
// esp32/hotspot-arcade-fw/ha_games.h): options live under "o" and the correct index
// under "c" (NOT "a"/"correct" as an earlier draft of this file assumed).
export function parsePack(text) {
  const out = { name: "", questions: [] };
  for (const block of text.split(/^---$/m)) {
    const q = { q: "", o: [], c: 0 };
    let gotQ = false;
    for (const raw of block.split("\n")) {
      const line = raw.trim();
      const val = (p) => line.slice(p.length).trim();
      if (line.startsWith("Pack:")) out.name = val("Pack:");
      else if (line.startsWith("Q:")) { q.q = val("Q:"); gotQ = true; }
      else if (/^[A-D]:/.test(line)) q.o.push(line.slice(2).trim());
      else if (line.startsWith("Answer:")) {
        const letter = val("Answer:").toUpperCase().charAt(0);
        q.c = Math.max(0, "ABCD".indexOf(letter));
      }
    }
    if (gotQ && q.o.length === 4) out.questions.push(q);
  }
  return out;
}

/** Load the repo's sample packs through the harness's HTTP server. */
export async function loadSamplePacks(names = ["general", "movies", "science"]) {
  const packs = [];
  for (const n of names) {
    try {
      const res = await fetch(`../../trivia-packs/${n}.txt`);
      if (res.ok) packs.push(parsePack(await res.text()));
    } catch (e) { /* pack simply unavailable; the panel shows what loaded */ }
  }
  return packs;
}
