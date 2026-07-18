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

// Final podium: top 3 of the roster by score.
static void ha_trivia_podium(HotspotArcadeApp* app) {
    widget_add_string_element(
        app->widget, 64, 16, AlignCenter, AlignTop, FontPrimary, "Final Scores");

    int idx[HA_MAX_PLAYERS];
    int n = 0;
    for(int i = 0; i < HA_MAX_PLAYERS; i++)
        if(app->players[i].used) idx[n++] = i;
    if(n == 0) {
        widget_add_string_element(
            app->widget, 64, 40, AlignCenter, AlignTop, FontSecondary, "No players");
        return;
    }
    int shown = n < 3 ? n : 3;
    for(int r = 0; r < shown; r++) {
        int best = r;
        for(int j = r + 1; j < n; j++)
            if(app->players[idx[j]].score > app->players[idx[best]].score) best = j;
        int t = idx[r];
        idx[r] = idx[best];
        idx[best] = t;
        HaPlayer* p = &app->players[idx[r]];
        FuriString* row = furi_string_alloc();
        furi_string_printf(row, "%d. %s", r + 1, p->nick);
        widget_add_string_element(
            app->widget, 4, 30 + r * 10, AlignLeft, AlignTop, FontSecondary,
            furi_string_get_cstr(row));
        furi_string_printf(row, "%ld", (long)p->score);
        widget_add_string_element(
            app->widget, 123, 30 + r * 10, AlignRight, AlignTop, FontSecondary,
            furi_string_get_cstr(row));
        furi_string_free(row);
    }
}

// Live A/B/C/D answer bars (grow as answers arrive; correct is marked on reveal).
static void ha_trivia_bars(HotspotArcadeApp* app) {
    int maxc = 1;
    for(int k = 0; k < 4; k++)
        if(app->answer_counts[k] > maxc) maxc = app->answer_counts[k];
    for(int k = 0; k < 4; k++) {
        int y = 13 + k * 9;
        char letter[2] = {(char)('A' + k), '\0'};
        widget_add_string_element(app->widget, 2, y, AlignLeft, AlignTop, FontSecondary, letter);
        widget_add_frame_element(app->widget, 14, y, 90, 8, 1);
        int w = (86 * app->answer_counts[k]) / maxc;
        if(w > 0) widget_add_rect_element(app->widget, 16, y + 2, w, 4, 0, true);
        FuriString* c = furi_string_alloc();
        furi_string_printf(c, "%d", app->answer_counts[k]);
        widget_add_string_element(
            app->widget, 107, y, AlignLeft, AlignTop, FontSecondary, furi_string_get_cstr(c));
        furi_string_free(c);
        if(app->trivia_phase == 2 && k == app->cur_correct)
            widget_add_circle_element(app->widget, 123, y + 4, 2, true);
    }
}

static void ha_trivia_render(HotspotArcadeApp* app) {
    widget_reset(app->widget);
    FuriString* tmp = furi_string_alloc();

    furi_string_printf(tmp, "Trivia Q%d/%d", app->trivia_idx + 1, app->trivia_count);
    widget_add_string_element(
        app->widget, 0, 0, AlignLeft, AlignTop, FontPrimary, furi_string_get_cstr(tmp));

    if(app->trivia_phase == 0) {
        widget_add_line_element(app->widget, 0, 12, 127, 12);
        ha_trivia_podium(app);
        furi_string_free(tmp);
        return;
    }

    // Live answer count on the right of the title.
    furi_string_printf(tmp, "%d/%d", app->answers_in, app->answers_total);
    widget_add_string_element(
        app->widget, 127, 1, AlignRight, AlignTop, FontSecondary, furi_string_get_cstr(tmp));
    widget_add_line_element(app->widget, 0, 11, 127, 11);

    ha_trivia_bars(app);

    if(app->trivia_phase == 1) {
        widget_add_button_element(
            app->widget, GuiButtonTypeCenter, "Reveal", ha_trivia_button_cb, app);
    } else {
        bool last = (app->trivia_idx + 1 >= app->trivia_count);
        widget_add_button_element(
            app->widget, GuiButtonTypeCenter, last ? "Finish" : "Next", ha_trivia_button_cb, app);
    }
    furi_string_free(tmp);
}

void hotspot_arcade_scene_host_trivia_on_enter(void* context) {
    HotspotArcadeApp* app = context;
    const char* err = NULL;
    if(furi_string_empty(app->trivia_pack_path)) {
        err = "Pick a trivia pack\nfrom the main menu.";
    } else if(!ha_trivia_begin(app)) {
        err = "That pack has no\nvalid questions.";
    }
    if(err) {
        DialogMessage* m = dialog_message_alloc();
        dialog_message_set_header(m, "Trivia", 64, 2, AlignCenter, AlignTop);
        dialog_message_set_text(m, err, 64, 32, AlignCenter, AlignCenter);
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
