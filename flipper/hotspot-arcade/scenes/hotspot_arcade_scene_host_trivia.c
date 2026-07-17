#include "../hotspot_arcade_i.h"
#include "../helpers/ha_session.h"

static void ha_trivia_button_cb(GuiButtonType result, InputType type, void* context) {
    HotspotArcadeApp* app = context;
    if(type != InputTypeShort) return;
    if(result != GuiButtonTypeCenter) return;
    if(app->trivia_phase == 1) {
        view_dispatcher_send_custom_event(app->view_dispatcher, HaEventTriviaReveal);
    } else if(app->trivia_phase == 2) {
        view_dispatcher_send_custom_event(app->view_dispatcher, HaEventTriviaNext);
    }
}

static void ha_trivia_render(HotspotArcadeApp* app) {
    widget_reset(app->widget);
    FuriString* tmp = furi_string_alloc();

    furi_string_printf(tmp, "Trivia  Q%d/%d", app->trivia_idx + 1, app->trivia_count);
    widget_add_string_element(
        app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, furi_string_get_cstr(tmp));
    widget_add_line_element(app->widget, 0, 12, 127, 12);

    if(app->trivia_phase == 0) {
        widget_add_string_element(
            app->widget, 64, 28, AlignCenter, AlignTop, FontPrimary, "Round complete");
        widget_add_string_element(
            app->widget, 64, 44, AlignCenter, AlignTop, FontSecondary, "Back to lobby");
        furi_string_free(tmp);
        return;
    }

    // Question text (wrapped).
    widget_add_text_box_element(
        app->widget,
        0,
        15,
        127,
        22,
        AlignLeft,
        AlignTop,
        furi_string_get_cstr(app->cur_q),
        true);

    if(app->trivia_phase == 1) {
        furi_string_printf(tmp, "Answers: %d/%d", app->answers_in, app->answers_total);
        widget_add_string_element(
            app->widget, 0, 40, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(tmp));
        widget_add_button_element(
            app->widget, GuiButtonTypeCenter, "Reveal", ha_trivia_button_cb, app);
    } else { // reveal
        char letter = 'A' + app->cur_correct;
        furi_string_printf(
            tmp, "Ans %c: %s", letter, furi_string_get_cstr(app->cur_opts[app->cur_correct]));
        widget_add_string_element(
            app->widget, 0, 40, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(tmp));
        bool last = (app->trivia_idx + 1 >= app->trivia_count);
        widget_add_button_element(
            app->widget, GuiButtonTypeCenter, last ? "Finish" : "Next", ha_trivia_button_cb, app);
    }
    furi_string_free(tmp);
}

void hotspot_arcade_scene_host_trivia_on_enter(void* context) {
    HotspotArcadeApp* app = context;
    if(!ha_trivia_begin(app)) {
        DialogMessage* m = dialog_message_alloc();
        dialog_message_set_header(m, "Trivia", 64, 2, AlignCenter, AlignTop);
        dialog_message_set_text(
            m, "Pack has no valid\nquestions.", 64, 32, AlignCenter, AlignCenter);
        dialog_message_set_buttons(m, NULL, "OK", NULL);
        dialog_message_show(app->dialogs, m);
        dialog_message_free(m);
        scene_manager_previous_scene(app->scene_manager);
        return;
    }
    ha_trivia_render(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, HaViewWidget);
}

bool hotspot_arcade_scene_host_trivia_on_event(void* context, SceneManagerEvent event) {
    HotspotArcadeApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case HaEventTriviaReveal:
        ha_trivia_reveal(app);
        ha_trivia_render(app);
        return true;
    case HaEventTriviaNext:
        ha_trivia_next(app); // sets phase 0 when the pack ends
        ha_trivia_render(app);
        return true;
    case HaEventRefreshView:
        ha_trivia_render(app);
        return true;
    default:
        return false;
    }
}

void hotspot_arcade_scene_host_trivia_on_exit(void* context) {
    HotspotArcadeApp* app = context;
    widget_reset(app->widget);
}
