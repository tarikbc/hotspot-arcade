// Parses the trivia pack text format so the harness can feed packs to the engine
// the way the Flipper does.
//
// KNOWN DUPLICATION: ha_session.c's trivia_stream_pack (around lines 145-216) parses
// this same format in C. It is the only rule-ish logic implemented twice in this
// project (see the design doc). This function mirrors that C state machine line for
// line, on purpose, rather than taking the obviously-simpler "split on ---" shortcut:
// per trivia-packs/README.md line 35, blocks may be separated by blank lines alone,
// with no "---" at all, and the C parser has no notion of "---" as a delimiter — it
// just ignores it as an unrecognized line. A pack with no "---" would parse as one
// giant block under a naive splitter and silently yield zero questions.
//
// Output shape matches what Engine::triviaAddQ actually parses (verified against
// esp32/hotspot-arcade-fw/ha_games.h): options live under "o" and the correct index
// under "c" (NOT "a"/"correct" as an earlier draft of this file assumed).
export function parsePack(text, fallbackName = "") {
  const out = { name: "", questions: [] };

  // Topic name = the first "Pack:" line if present, else the caller's fallback
  // (mirrors trivia_stream_pack falling back to the filename around ha_session.c:170).
  for (const raw of text.split("\n")) {
    const line = raw.trim();
    if (line.startsWith("Pack:")) { out.name = line.slice(5).trim(); break; }
  }
  if (!out.name) out.name = fallbackName;

  // Stream questions the way the C loop does: a "Q:" line is the delimiter itself
  // (flush whatever was accumulated, then start fresh), not "---". Options are
  // indexed by letter (o[t[0]-'A'] in C) so out-of-order A-D lines still map to the
  // right slot, and A-D/Answer: lines are only honored once a "Q:" has been seen
  // (the `inQ` gate) so stray lines before the first question are ignored.
  let q = null;
  let gotOpts = 0;
  let gotAns = false; // mirrors trivia_stream_pack's gotAns gate in ha_session.c
  let inQ = false;

  const flush = () => {
    // Match the C parser: a question with no valid A-D answer, or fewer than
    // four options, is dropped rather than silently defaulted.
    if (inQ && gotOpts >= 4 && gotAns) out.questions.push(q);
  };

  for (const raw of text.split("\n")) {
    const line = raw.trim(); // trim() also strips a trailing \r, so CRLF is tolerated
    if (line.startsWith("Q:")) {
      flush();
      q = { q: line.slice(2).trim(), o: ["", "", "", ""], c: 0 };
      gotOpts = 0;
      gotAns = false;
      inQ = true;
    } else if (inQ) {
      const opt = /^([A-D]):(.*)$/.exec(line);
      if (opt) {
        q.o[opt[1].charCodeAt(0) - 65] = opt[2].trim();
        gotOpts++;
      } else if (line.startsWith("Answer:")) {
        const letter = line.slice(7).trim().toUpperCase().charAt(0);
        const idx = "ABCD".indexOf(letter);
        if (idx >= 0) { q.c = idx; gotAns = true; }
      }
    }
  }
  flush(); // the last block has no following "Q:" to trigger its flush

  return out;
}

/** Load the repo's sample packs through the harness's HTTP server. */
export async function loadSamplePacks(names = ["general", "movies", "science"]) {
  const packs = [];
  for (const n of names) {
    try {
      const res = await fetch(`../../trivia-packs/${n}.txt`);
      if (res.ok) packs.push(parsePack(await res.text(), n));
      else console.warn(`trivia pack "${n}" failed to load: HTTP ${res.status}`);
    } catch (e) { console.warn(`trivia pack "${n}" failed to load:`, e); }
  }
  return packs;
}
