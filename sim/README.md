# Simulator

Runs the **real** ESP game engine (`esp32/hotspot-arcade-fw/ha_games.h`, compiled to
WebAssembly) in a browser page, with 2–8 phone panels and a Flipper panel. Lets you play
and design games without a Flipper, a dev board, or eight friends.

## Use

```sh
brew install emscripten     # one-time
cd web && node build.mjs     # build the phone client
cd .. && sim/engine/build.sh # build the engine
sim/serve.sh                 # -> http://localhost:8123/sim/web/
```

## Headless tests

```sh
sim/test/all.sh
```

Scripted sessions driven straight against the engine, no browser. Not run in CI (that
needs emsdk in the workflow).

## Finding memory bugs

```sh
sim/engine/build.sh --asan && sim/test/all.sh
```

The engine indexes fixed arrays by player id. On the ESP32 an out-of-bounds write
silently corrupts a neighbour; under ASan it aborts at the line.

## What this is not

No WiFi, no ESP heap accounting, no UART timing — the 8-phone scale test still needs
hardware. The Flipper panel shows UART traffic, not the real 1-bit screens.

## Fidelity

Game rules come from the real engine, so they cannot drift. The one exception is the
trivia pack text parser (`trivia-packs.js`), which duplicates the C parser in
`ha_session.c`; see `docs/superpowers/specs/2026-07-21-sim-harness-design.md`.
