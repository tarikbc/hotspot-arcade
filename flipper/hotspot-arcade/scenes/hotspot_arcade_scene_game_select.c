#include "../hotspot_arcade_i.h"
#include "../helpers/ha_session.h"

// Pure selector: pick which game is live, tell the ESP, then return to the
// dashboard. Hosting a round is a separate action on the dashboard, so choosing
// a game never jumps you into a screen you didn't ask for.
typedef enum {
    GameTrivia,
    GameConnect4,
    GameNone,
} GameIndex;

static void ha_game_cb(void* context, uint32_t index) {
    HotspotArcadeApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void hotspot_arcade_scene_game_select_on_enter(void* context) {
    HotspotArcadeApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Select Game");
    submenu_add_item(app->submenu, "Trivia", GameTrivia, ha_game_cb, app);
    submenu_add_item(app->submenu, "Connect Four", GameConnect4, ha_game_cb, app);
    submenu_add_item(app->submenu, "None (lobby)", GameNone, ha_game_cb, app);
    uint32_t sel = app->active_game == HA_GAME_TRIVIA   ? GameTrivia :
                   app->active_game == HA_GAME_CONNECT4 ? GameConnect4 :
                                                          GameNone;
    submenu_set_selected_item(app->submenu, sel);
    view_dispatcher_switch_to_view(app->view_dispatcher, HaViewSubmenu);
}

bool hotspot_arcade_scene_game_select_on_event(void* context, SceneManagerEvent event) {
    HotspotArcadeApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case GameTrivia:
        ha_select_game(app, HA_GAME_TRIVIA);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case GameConnect4:
        ha_select_game(app, HA_GAME_CONNECT4);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case GameNone:
        ha_select_game(app, HA_GAME_NONE);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    default:
        return false;
    }
}

void hotspot_arcade_scene_game_select_on_exit(void* context) {
    HotspotArcadeApp* app = context;
    submenu_reset(app->submenu);
}
