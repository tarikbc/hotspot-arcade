#include "../hotspot_arcade_i.h"
#include "../helpers/ha_session.h"

typedef enum {
    GameNone,
    GameTrivia,
    GameConnect4,
} GameIndex;

static void ha_game_cb(void* context, uint32_t index) {
    HotspotArcadeApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void ha_msg(HotspotArcadeApp* app, const char* header, const char* text) {
    DialogMessage* m = dialog_message_alloc();
    if(header) dialog_message_set_header(m, header, 64, 2, AlignCenter, AlignTop);
    dialog_message_set_text(m, text, 64, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(m, NULL, "OK", NULL);
    dialog_message_show(app->dialogs, m);
    dialog_message_free(m);
}

void hotspot_arcade_scene_game_select_on_enter(void* context) {
    HotspotArcadeApp* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Select Game");
    submenu_add_item(app->submenu, "Trivia", GameTrivia, ha_game_cb, app);
    submenu_add_item(app->submenu, "Connect Four", GameConnect4, ha_game_cb, app);
    submenu_add_item(app->submenu, "None (lobby)", GameNone, ha_game_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, HaViewSubmenu);
}

bool hotspot_arcade_scene_game_select_on_event(void* context, SceneManagerEvent event) {
    HotspotArcadeApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case GameNone:
        ha_select_game(app, HA_GAME_NONE);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    case GameTrivia:
        if(furi_string_empty(app->trivia_pack_path)) {
            ha_msg(app, "Trivia", "Pick a trivia pack\nfrom the main menu.");
            view_dispatcher_switch_to_view(app->view_dispatcher, HaViewSubmenu);
            return true;
        }
        ha_select_game(app, HA_GAME_TRIVIA);
        scene_manager_next_scene(app->scene_manager, HaSceneHostTrivia);
        return true;
    case GameConnect4:
        ha_select_game(app, HA_GAME_CONNECT4);
        scene_manager_next_scene(app->scene_manager, HaSceneHostDuel);
        return true;
    default:
        return false;
    }
}

void hotspot_arcade_scene_game_select_on_exit(void* context) {
    HotspotArcadeApp* app = context;
    submenu_reset(app->submenu);
}
