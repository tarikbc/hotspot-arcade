#pragma once

#include <furi.h>

typedef struct HotspotArcadeApp HotspotArcadeApp;

// Drain the UART, parse frames from the ESP, and update app state. GUI thread.
void ha_session_rx(HotspotArcadeApp* app);

// True if the ESP board is attached (fresh PING seen, or one arrives in wait_ms).
bool ha_board_present(HotspotArcadeApp* app, uint32_t wait_ms);

// Begin a session: reset state, load the manifest, run the start handshake
// (CLEAR_FILES -> stream bundle -> SET_AP -> START). Blocks while files stream.
void ha_session_start(HotspotArcadeApp* app);
void ha_session_stop(HotspotArcadeApp* app);

// Host controls.
void ha_select_game(HotspotArcadeApp* app, uint8_t game);
void ha_reset_scores(HotspotArcadeApp* app);

// Trivia round orchestration.
bool ha_trivia_begin(HotspotArcadeApp* app); // load pack, send question 0
void ha_trivia_reveal(HotspotArcadeApp* app); // close the current question
bool ha_trivia_next(HotspotArcadeApp* app); // advance; false if the pack ended
void ha_round_end(HotspotArcadeApp* app); // end the current game round

// Roster helper for the leaderboard.
int ha_player_count(HotspotArcadeApp* app);
