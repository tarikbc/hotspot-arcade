# Content packs

Plain-text content for the pack-driven games, one directory per game
(`packs/trivia/`, and in Phase 2 `packs/wyr/`, `packs/scramble/`, `packs/draw/`).

The format is the same for every game: `Key: value` lines, with a line of `---` or a
blank line between blocks. A `Pack:` key names the pack; without one the filename is
used. The Flipper streams these blocks without interpreting them — each game's meaning
for the keys lives in the ESP firmware.

## Format

The block grammar is generic — the same for every game:

- The first non-empty line of a block **may** be `Pack: <name>` to name the pack.
  If absent, the file name is used.
- Every other line is `Key: value`. Which keys matter, and what they mean, is up to
  the game — the Flipper ships them verbatim without parsing them.
- Blocks are separated by a line containing only `---` (three hyphens) and/or one
  or more blank lines.
- Every line is trimmed of surrounding whitespace before parsing.

### Trivia's keys

Trivia is the one game that uses packs today. Its block is one `Q:` line, exactly
four option lines `A:` / `B:` / `C:` / `D:`, and one `Answer:` line whose value is a
single letter `A`-`D` matching a real option:

```
Pack: General Knowledge
Q: What is the capital of France?
A: Paris
B: London
C: Berlin
D: Madrid
Answer: A
---
Q: How many continents are there?
A: 5
B: 6
C: 7
D: 8
Answer: C
---
```

Keep it short so it renders on phones and fits the UART payload budget: question
<= ~110 characters, each option <= ~38 characters. No em-dashes in the content —
use commas or periods (the `---` separator is plain hyphens and is fine).

## How packs get onto the SD card

Packs live here and are pushed to the Flipper by the deploy script, which copies
every `packs/<game>/*.txt` file to:

```
/ext/apps_data/hotspot_arcade/packs/<game>/<name>.txt
```

Run it with the Flipper connected over USB:

```sh
python3 tools/deploy-to-flipper.py --port /dev/cu.usbmodemflip_XXXX
```

The Flipper reads packs from that directory tree at runtime. The deploy script only
adds files; packs removed from this folder are not deleted from the SD card
automatically.
