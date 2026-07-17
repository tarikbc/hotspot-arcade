#include "../hotspot_arcade_i.h"
#include "../helpers/ha_storage.h"

static const char* const on_off[] = {"OFF", "ON"};
static const uint32_t dur_values[] = {10, 15, 20, 30, 45};
static const char* const dur_labels[] = {"10s", "15s", "20s", "30s", "45s"};
#define DUR_COUNT (sizeof(dur_values) / sizeof(dur_values[0]))

static void ha_settings_dur_cb(VariableItem* item) {
    HotspotArcadeApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->question_dur = dur_values[i];
    variable_item_set_current_value_text(item, dur_labels[i]);
    ha_storage_save_config(app);
}

static void ha_settings_sound_cb(VariableItem* item) {
    HotspotArcadeApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->sound_on = (i != 0);
    variable_item_set_current_value_text(item, on_off[i]);
    ha_storage_save_config(app);
}

static void ha_settings_vibro_cb(VariableItem* item) {
    HotspotArcadeApp* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->vibro_on = (i != 0);
    variable_item_set_current_value_text(item, on_off[i]);
    ha_storage_save_config(app);
}

void hotspot_arcade_scene_settings_on_enter(void* context) {
    HotspotArcadeApp* app = context;
    variable_item_list_reset(app->var_item_list);
    VariableItem* item;

    uint8_t dur_idx = 2; // default 20s
    for(uint8_t i = 0; i < DUR_COUNT; i++)
        if(dur_values[i] == app->question_dur) dur_idx = i;
    item = variable_item_list_add(
        app->var_item_list, "Question time", DUR_COUNT, ha_settings_dur_cb, app);
    variable_item_set_current_value_index(item, dur_idx);
    variable_item_set_current_value_text(item, dur_labels[dur_idx]);

    item = variable_item_list_add(app->var_item_list, "Sound", 2, ha_settings_sound_cb, app);
    variable_item_set_current_value_index(item, app->sound_on ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->sound_on ? 1 : 0]);

    item = variable_item_list_add(app->var_item_list, "Vibration", 2, ha_settings_vibro_cb, app);
    variable_item_set_current_value_index(item, app->vibro_on ? 1 : 0);
    variable_item_set_current_value_text(item, on_off[app->vibro_on ? 1 : 0]);

    view_dispatcher_switch_to_view(app->view_dispatcher, HaViewVarItemList);
}

bool hotspot_arcade_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void hotspot_arcade_scene_settings_on_exit(void* context) {
    HotspotArcadeApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
