#include "../hotspot_arcade_i.h"
#include "../helpers/ha_session.h"

static const char* game_name(uint8_t g) {
    switch(g) {
    case HA_GAME_TRIVIA:
        return "Trivia";
    case HA_GAME_CONNECT4:
        return "Connect 4";
    default:
        return "None";
    }
}

static void ha_lobby_button_cb(GuiButtonType result, InputType type, void* context) {
    HotspotArcadeApp* app = context;
    if(type != InputTypeShort) return;
    if(result == GuiButtonTypeLeft) {
        view_dispatcher_send_custom_event(app->view_dispatcher, HaEventPickGame);
    } else if(result == GuiButtonTypeCenter) {
        view_dispatcher_send_custom_event(app->view_dispatcher, HaEventHostGame);
    } else if(result == GuiButtonTypeRight) {
        view_dispatcher_send_custom_event(app->view_dispatcher, HaEventShowLeaderboard);
    }
}

static void ha_status_screen(HotspotArcadeApp* app, const char* l1, const char* l2) {
    widget_reset(app->widget);
    widget_add_string_element(
        app->widget, 64, 6, AlignCenter, AlignTop, FontPrimary, "Hotspot Arcade");
    widget_add_line_element(app->widget, 0, 20, 127, 20);
    widget_add_string_element(app->widget, 64, 27, AlignCenter, AlignTop, FontSecondary, l1);
    if(l2 && l2[0])
        widget_add_string_element(app->widget, 64, 42, AlignCenter, AlignTop, FontSecondary, l2);
}

static void ha_dashboard(HotspotArcadeApp* app) {
    widget_reset(app->widget);
    FuriString* tmp = furi_string_alloc();

    widget_add_string_element(
        app->widget, 64, 1, AlignCenter, AlignTop, FontPrimary, "Hotspot Arcade");
    widget_add_line_element(app->widget, 0, 13, 127, 13);

    furi_string_printf(tmp, "Join: %s", furi_string_get_cstr(app->ssid));
    widget_add_string_element(
        app->widget, 0, 16, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(tmp));

    bool live = app->portal_running && !app->link_lost;
    const char* state = app->link_lost ? "Board disconnected" :
                        live            ? "Broadcasting" :
                                          "Starting...";
    widget_add_circle_element(app->widget, 4, 29, 3, live);
    widget_add_string_element(app->widget, 12, 26, AlignLeft, AlignTop, FontSecondary, state);

    furi_string_printf(tmp, "Players: %d", ha_player_count(app));
    widget_add_string_element(
        app->widget, 0, 38, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(tmp));
    furi_string_printf(tmp, "Game: %s", game_name(app->active_game));
    widget_add_string_element(
        app->widget, 0, 48, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(tmp));

    // Left picks the game; Right shows scores; Center hosts the active game
    // (drive trivia questions, or watch the Connect Four match feed).
    widget_add_button_element(app->widget, GuiButtonTypeLeft, "Games", ha_lobby_button_cb, app);
    if(app->active_game == HA_GAME_TRIVIA) {
        widget_add_button_element(
            app->widget, GuiButtonTypeCenter, "Start", ha_lobby_button_cb, app);
    } else if(app->active_game == HA_GAME_CONNECT4) {
        widget_add_button_element(
            app->widget, GuiButtonTypeCenter, "Feed", ha_lobby_button_cb, app);
    }
    widget_add_button_element(app->widget, GuiButtonTypeRight, "Scores", ha_lobby_button_cb, app);

    furi_string_free(tmp);
}

static void ha_lobby_render(HotspotArcadeApp* app) {
    const char* s = furi_string_get_cstr(app->status);
    if(strcmp(s, "detecting") == 0) {
        ha_status_screen(app, "Detecting board...", "");
        return;
    }
    if(strcmp(s, "noboard") == 0) {
        ha_status_screen(app, "No board detected", "Attach the ESP32 board");
        return;
    }
    if(app->portal_running || app->link_lost) {
        ha_dashboard(app);
        return;
    }
    if(strstr(s, "err") != NULL) {
        ha_status_screen(app, "Startup error", s);
        return;
    }
    FuriString* tmp = furi_string_alloc();
    furi_string_printf(tmp, "SSID: %s", furi_string_get_cstr(app->ssid));
    ha_status_screen(app, "Starting session...", furi_string_get_cstr(tmp));
    furi_string_free(tmp);
}

void hotspot_arcade_scene_lobby_on_enter(void* context) {
    HotspotArcadeApp* app = context;
    if(!app->session_active) {
        app->awaiting_board = false;
        app->link_lost = false;
        furi_string_set(app->status, "detecting");
        ha_lobby_render(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, HaViewWidget);
        view_dispatcher_send_custom_event(app->view_dispatcher, HaEventDetectBoard);
    } else {
        ha_lobby_render(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, HaViewWidget);
    }
}

bool hotspot_arcade_scene_lobby_on_event(void* context, SceneManagerEvent event) {
    HotspotArcadeApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case HaEventDetectBoard:
        if(ha_board_present(app, 2500)) {
            furi_string_set(app->status, "starting");
            ha_lobby_render(app);
            view_dispatcher_send_custom_event(app->view_dispatcher, HaEventBeginStart);
        } else {
            app->awaiting_board = true;
            furi_string_set(app->status, "noboard");
            ha_lobby_render(app);
        }
        return true;
    case HaEventBeginStart:
        ha_session_start(app); // blocks while the bundle streams
        ha_lobby_render(app);
        return true;
    case HaEventRefreshView:
        ha_lobby_render(app);
        return true;
    case HaEventPickGame:
        scene_manager_next_scene(app->scene_manager, HaSceneGameSelect);
        return true;
    case HaEventHostGame:
        if(app->active_game == HA_GAME_TRIVIA)
            scene_manager_next_scene(app->scene_manager, HaSceneHostTrivia);
        else if(app->active_game == HA_GAME_CONNECT4)
            scene_manager_next_scene(app->scene_manager, HaSceneHostDuel);
        return true;
    case HaEventShowLeaderboard:
        scene_manager_next_scene(app->scene_manager, HaSceneLeaderboard);
        return true;
    default:
        return false;
    }
}

void hotspot_arcade_scene_lobby_on_exit(void* context) {
    HotspotArcadeApp* app = context;
    app->awaiting_board = false;
}
