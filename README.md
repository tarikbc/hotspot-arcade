# Hotspot Arcade

**Offline multiplayer party games hosted from a Flipper Zero + ESP32-S2 WiFi board.**
No internet, no app install. You host an open WiFi network from the Flipper; people
nearby join it, a captive page hands them into a game in their phone browser, and
everyone plays together over the local network. Built for dead zones: buses, planes,
subways, campsites, anywhere with no signal.

The Flipper is the **game master**: it shows the lobby and live scoreboard and drives
the rounds. The ESP32 board is the **referee**: it runs the WiFi access point, serves
the game to phones, and keeps the real-time game state. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Games (v1)

- **Trivia** — Kahoot-style. The host (Flipper) drives questions from a pack on the SD
  card; phones buzz in A/B/C/D; points for correct and fast; live leaderboard. Scales
  to the whole group.
- **Connect Four** — players challenge each other from their phones and play 1v1;
  multiple matches run at once; wins score on the Flipper leaderboard.

Drawing/guessing and real-time arcade are planned follow-ups on the same engine.

## Hardware

- **Flipper Zero** (developed on **Momentum** firmware; other forks work with a matching
  `ufbt` SDK).
- **Official Flipper WiFi Dev Board (ESP32-S2)** — mounts on the GPIO header, wiring the
  two together over UART.

## How it works

```
 Phones (browser)  <-- WebSocket -->  ESP32-S2  <-- UART 921600 -->  Flipper Zero
   play the game                     AP + web + referee              host / scoreboard
```

- The ESP hosts an **open AP + wildcard DNS + catch-all web server**, so joining the
  WiFi pops a captive page on every phone.
- The captive page hands off to the game web app at `http://192.168.4.1` (captive
  mini-browsers are too limited for WebSockets, so it is a "tap to open in your browser"
  handoff).
- The Flipper streams the (gzipped) web bundle and trivia content to the ESP over a
  framed UART protocol, then orchestrates rounds. Real-time game traffic stays on the
  ESP and never crosses the slow UART. Protocol: [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Install & flash

Three parts: flash the ESP firmware, build+install the Flipper app, and put the web
bundle + trivia packs on the SD card. Full commands and gotchas are in
[CLAUDE.md](CLAUDE.md); the short version:

**1. Build the web bundle**
```sh
cd web && node build.mjs        # -> web/dist/{index.html.gz, manifest.json}
```

**2. ESP32-S2 firmware** (arduino-cli, esp32 core 2.0.17, vendored libs in `esp32/libs`)
```sh
arduino-cli compile --fqbn esp32:esp32:esp32s2:PartitionScheme=huge_app \
  --libraries esp32/libs --output-dir esp32/hotspot-arcade-fw/build esp32/hotspot-arcade-fw
# then esptool write-flash the four images (see CLAUDE.md; never --erase-all on the S2)
```

**3. Flipper app + SD content**
```sh
cd flipper/hotspot-arcade && ufbt          # -> dist/hotspot_arcade.fap
python3 tools/deploy-to-flipper.py --port /dev/cu.usbmodemflip_XXXX
```
The deploy script pushes the fap to `/ext/apps/GPIO/`, the web bundle to
`/ext/apps_data/hotspot_arcade/web/`, and the trivia packs to `.../trivia/`.

## Usage

On the Flipper: **Apps → GPIO → [ESP32] Hotspot Arcade**.

1. **Set SSID** and pick a **Trivia** pack (optional, only needed for trivia).
2. **Start Session** — the ESP brings up the AP; the dashboard shows **Broadcasting**.
3. People **join the WiFi** and open `192.168.4.1`, pick a nickname, and land in the lobby.
4. **Select Game** → **Trivia** (drive questions, tap Reveal / Next) or **Connect Four**
   (players challenge each other; the host watches the feed).
5. **Leaderboard** shows live scores; **Console** shows the raw event log.

## Trivia packs

Simple text files under `trivia-packs/` (`Pack:` / `Q:` / `A:`-`D:` / `Answer:`, blocks
split by `---`). Drop your own into the SD `trivia/` folder. See
[trivia-packs/README.md](trivia-packs/README.md).

## Responsible use

Hotspot Arcade runs an **open** WiFi access point and a captive page that serves a game.
It is for fun and learning on your own hardware, among people who want to play. Running
an open AP may be restricted in some places (e.g. on aircraft, or where it could
interfere) — only operate it where that is allowed. It captures no credentials and
serves only the bundled game.

## Layout

```
flipper/hotspot-arcade/   Flipper app (C, ufbt/Momentum) — host + scoreboard
esp32/hotspot-arcade-fw/  ESP32-S2 firmware (Arduino) — AP + web + WebSocket referee
esp32/libs/               vendored AsyncTCP + ESPAsyncWebServer
web/                      phone game client (vanilla JS, gzipped bundle)
trivia-packs/             sample trivia content
tools/deploy-to-flipper.py
docs/                     ARCHITECTURE.md, PROTOCOL.md
```

Sibling project to [flytrap](../flytrap), which this reuses the AP/captive-portal
plumbing and Flipper UART patterns from.
