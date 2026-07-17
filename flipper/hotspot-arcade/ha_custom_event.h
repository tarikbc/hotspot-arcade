#pragma once

// Custom ViewDispatcher events. Kept above submenu item indices (0..N) so the
// two ranges never collide when routed to a scene's on_event.
typedef enum {
    HaEventSsidDone = 100,
    // Posted by the UART worker when bytes arrive; drained + parsed on the GUI
    // thread in hotspot_arcade.c, then a refresh is dispatched to the top scene.
    HaEventRxData = 101,
    HaEventRefreshView = 102,
    // Start flow: paint a status screen, then run the blocking step next loop.
    HaEventDetectBoard = 103,
    HaEventBeginStart = 104,
    // Lobby host actions.
    HaEventPickGame = 105,
    HaEventShowLeaderboard = 106,
    HaEventShowConsole = 107,
    // Trivia host actions (buttons on the host_trivia widget).
    HaEventTriviaReveal = 108,
    HaEventTriviaNext = 109,
} HaCustomEvent;
