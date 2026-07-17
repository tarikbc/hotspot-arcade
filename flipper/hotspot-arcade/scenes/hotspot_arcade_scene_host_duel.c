#include "../hotspot_arcade_i.h"
#include "../helpers/ha_session.h"

static void ha_duel_button_cb(GuiButtonType result, InputType type, void* context) {
    HotspotArcadeApp* app = context;
    if(type != InputTypeShort) return;
    if(result == GuiButtonTypeRight)
        view_dispatcher_send_custom_event(app->view_dispatcher, HaEventShowLeaderboard);
}

static void ha_duel_render(HotspotArcadeApp* app) {
    widget_reset(app->widget);
    FuriString* tmp = furi_string_alloc();

    widget_add_string_element(
        app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, "Connect Four");
    widget_add_line_element(app->widget, 0, 12, 127, 12);

    widget_add_text_box_element(
        app->widget,
        0,
        15,
        127,
        20,
        AlignLeft,
        AlignTop,
        "Players challenge each other from their phones.",
        true);

    furi_string_printf(tmp, "Players: %d", ha_player_count(app));
    widget_add_string_element(
        app->widget, 0, 38, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(tmp));

    if(!furi_string_empty(app->last_event)) {
        widget_add_string_element(
            app->widget,
            0,
            49,
            AlignLeft,
            AlignTop,
            FontSecondary,
            furi_string_get_cstr(app->last_event));
    }

    widget_add_button_element(
        app->widget, GuiButtonTypeRight, "Scores", ha_duel_button_cb, app);
    furi_string_free(tmp);
}

void hotspot_arcade_scene_host_duel_on_enter(void* context) {
    HotspotArcadeApp* app = context;
    furi_string_reset(app->last_event);
    ha_duel_render(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, HaViewWidget);
}

bool hotspot_arcade_scene_host_duel_on_event(void* context, SceneManagerEvent event) {
    HotspotArcadeApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case HaEventRefreshView:
        ha_duel_render(app);
        return true;
    case HaEventShowLeaderboard:
        scene_manager_next_scene(app->scene_manager, HaSceneLeaderboard);
        return true;
    default:
        return false;
    }
}

void hotspot_arcade_scene_host_duel_on_exit(void* context) {
    HotspotArcadeApp* app = context;
    widget_reset(app->widget);
}
