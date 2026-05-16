#include "piano-source.h"
#include "midi-input.h"
#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <algorithm>
#include <cmath>

static bool is_black_key(int semitone)
{
    return semitone == 1 || semitone == 3 || semitone == 6 ||
           semitone == 8 || semitone == 10;
}

void PianoSourceData::calculate_layout()
{
    if (num_keys == last_num_keys && first_note == last_first_note)
        return;

    white_keys.clear();
    black_keys.clear();

    int wi = 0;
    for (int i = 0; i < num_keys; i++)
    {
        int note = first_note + i;
        int semitone = ((note % 12) + 12) % 12;

        if (is_black_key(semitone))
        {
            float gap_center = wi * white_key_w;
            float x = gap_center - black_key_w * 0.5f;
            black_keys.push_back({x, 0, black_key_w, black_key_h, note});
        }
        else
        {
            float x = wi * white_key_w;
            white_keys.push_back({x, 0, white_key_w, white_key_h, note});
            wi++;
        }
    }

    width = static_cast<uint32_t>(wi * white_key_w);
    height = static_cast<uint32_t>(white_key_h);

    last_num_keys = num_keys;
    last_first_note = first_note;
}

static const char *piano_source_get_name(void *)
{
    return obs_module_text("PianoKeyboard");
}

static void *piano_source_create(obs_data_t *settings, obs_source_t *source)
{
    auto *ps = new PianoSourceData();
    ps->source = source;

    ps->white_key_color = static_cast<uint32_t>(
        obs_data_get_int(settings, "white_key_color"));
    ps->black_key_color = static_cast<uint32_t>(
        obs_data_get_int(settings, "black_key_color"));
    ps->active_note_color = static_cast<uint32_t>(
        obs_data_get_int(settings, "active_note_color"));
    ps->outline_color = static_cast<uint32_t>(
        obs_data_get_int(settings, "outline_color"));
    ps->num_keys = static_cast<int>(obs_data_get_int(settings, "num_keys"));
    ps->first_note = static_cast<int>(obs_data_get_int(settings, "first_note"));
    ps->velocity_sensitive = obs_data_get_bool(settings, "velocity_sensitive");
    ps->show_outline = obs_data_get_bool(settings, "show_outline");
    ps->outline_width = static_cast<int>(obs_data_get_int(settings, "outline_width"));

    const char *device = obs_data_get_string(settings, "midi_device");
    if (device && *device)
    {
        ps->midi_device_name = device;
        ps->midi_input = MidiManager::instance().get_input(device);
        if (ps->midi_input)
            ps->midi_input->open();
    }

    ps->calculate_layout();
    return ps;
}

static void piano_source_destroy(void *data)
{
    auto *ps = static_cast<PianoSourceData *>(data);
    delete ps;
}

static void piano_source_update(void *data, obs_data_t *settings)
{
    auto *ps = static_cast<PianoSourceData *>(data);

    ps->white_key_color = static_cast<uint32_t>(
        obs_data_get_int(settings, "white_key_color"));
    ps->black_key_color = static_cast<uint32_t>(
        obs_data_get_int(settings, "black_key_color"));
    ps->active_note_color = static_cast<uint32_t>(
        obs_data_get_int(settings, "active_note_color"));
    ps->outline_color = static_cast<uint32_t>(
        obs_data_get_int(settings, "outline_color"));
    ps->num_keys = static_cast<int>(obs_data_get_int(settings, "num_keys"));
    ps->first_note = static_cast<int>(obs_data_get_int(settings, "first_note"));
    ps->velocity_sensitive = obs_data_get_bool(settings, "velocity_sensitive");
    ps->show_outline = obs_data_get_bool(settings, "show_outline");
    ps->outline_width = static_cast<int>(obs_data_get_int(settings, "outline_width"));

    const char *device = obs_data_get_string(settings, "midi_device");

    std::string new_device = device ? device : "";
    if (new_device != ps->midi_device_name)
    {
        ps->midi_device_name = new_device;
        ps->midi_input.reset();
        if (!new_device.empty())
        {
            ps->midi_input = MidiManager::instance().get_input(new_device);
            if (ps->midi_input)
                ps->midi_input->open();
        }
    }

    ps->calculate_layout();
}

static void piano_source_defaults(obs_data_t *settings)
{
    obs_data_set_default_int(settings, "num_keys", 49);
    obs_data_set_default_int(settings, "first_note", 36);
    obs_data_set_default_int(settings, "white_key_color", 0xFFFFFFFF);
    obs_data_set_default_int(settings, "black_key_color", 0xFF1A1A1A);
    obs_data_set_default_int(settings, "active_note_color", 0xFFFF9933);
    obs_data_set_default_int(settings, "outline_color", 0xFF000000);
    obs_data_set_default_bool(settings, "velocity_sensitive", true);
    obs_data_set_default_bool(settings, "show_outline", true);
    obs_data_set_default_int(settings, "outline_width", 1);
    obs_data_set_default_string(settings, "midi_device", "");
}

static bool refresh_devices(obs_properties_t *props, obs_property_t *,
                            void *)
{
    auto *device_list = obs_properties_get(props, "midi_device");
    if (!device_list)
        return false;

    obs_property_list_clear(device_list);
    obs_property_list_add_string(device_list, obs_module_text("None"), "");

    auto devices = MidiManager::instance().list_devices();
    for (const auto &dev : devices)
    {
        obs_property_list_add_string(device_list, dev.c_str(), dev.c_str());
    }

    return true;
}

static obs_properties_t *piano_source_properties(void *data)
{
    auto *ps = static_cast<PianoSourceData *>(data);
    auto *props = obs_properties_create();

    auto *device_list = obs_properties_add_list(
        props, "midi_device", obs_module_text("MIDIDevice"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

    obs_property_list_add_string(device_list, obs_module_text("None"), "");

    auto devices = MidiManager::instance().list_devices();
    for (const auto &dev : devices)
    {
        obs_property_list_add_string(device_list, dev.c_str(), dev.c_str());
    }

    obs_properties_add_button2(props, "refresh_devices",
                               obs_module_text("RefreshDevices"),
                               refresh_devices, ps);

    obs_properties_add_int_slider(props, "num_keys",
                                  obs_module_text("NumKeys"),
                                  25, 88, 1);

    obs_properties_add_int(props, "first_note",
                           obs_module_text("FirstNote"),
                           0, 108, 1);

    auto *color_group = obs_properties_create();
    obs_properties_add_color(color_group, "white_key_color",
                             obs_module_text("WhiteKeyColor"));
    obs_properties_add_color(color_group, "black_key_color",
                             obs_module_text("BlackKeyColor"));
    obs_properties_add_color(color_group, "active_note_color",
                             obs_module_text("ActiveNoteColor"));
    obs_properties_add_color(color_group, "outline_color",
                             obs_module_text("OutlineColor"));
    obs_properties_add_bool(color_group, "show_outline",
                            obs_module_text("ShowOutline"));
    obs_properties_add_int_slider(color_group, "outline_width",
                                  obs_module_text("OutlineWidth"),
                                  1, 4, 1);

    obs_properties_add_group(props, "appearance",
                             obs_module_text("Appearance"),
                             OBS_GROUP_NORMAL, color_group);

    obs_properties_add_bool(props, "velocity_sensitive",
                            obs_module_text("VelocitySensitive"));

    return props;
}

static void piano_source_render(void *data, gs_effect_t *)
{
    auto *ps = static_cast<PianoSourceData *>(data);
    if (!ps)
        return;

    ps->calculate_layout();

    if (ps->midi_input)
        ps->midi_input->check_reconnect();

    auto active_notes = ps->midi_input
                            ? ps->midi_input->get_active_notes()
                            : std::map<int, uint8_t>();

    gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    if (!solid)
        return;

    gs_eparam_t *color_param = gs_effect_get_param_by_name(solid, "color");
    if (!color_param)
        return;

    gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");
    if (!tech)
        return;

    gs_technique_begin(tech);
    gs_technique_begin_pass(tech, 0);

    // Draw white keys
    for (const auto &key : ps->white_keys)
    {
        auto it = active_notes.find(key.midi_note);
        struct vec4 color, oclr;

        if (it != active_notes.end())
        {
            vec4_from_rgba(&color, ps->active_note_color);
            if (ps->velocity_sensitive && it->second > 0)
            {
                float t = it->second / 127.0f;
                color.w *= t;
            }
        }
        else
        {
            vec4_from_rgba(&color, ps->white_key_color);
        }

        vec4_from_rgba(&oclr, ps->outline_color);

        gs_matrix_push();
        gs_matrix_translate3f(key.x, key.y, 0.0f);

        if (ps->show_outline)
        {
            gs_effect_set_vec4(color_param, &oclr);
            gs_draw_sprite(nullptr, 0, static_cast<uint32_t>(key.w),
                           static_cast<uint32_t>(key.h));

            float ow = static_cast<float>(ps->outline_width);
            gs_matrix_translate3f(ow, ow, 0.0f);
            gs_effect_set_vec4(color_param, &color);
            gs_draw_sprite(nullptr, 0,
                           static_cast<uint32_t>(key.w - ow * 2.0f),
                           static_cast<uint32_t>(key.h - ow * 2.0f));
        }
        else
        {
            gs_effect_set_vec4(color_param, &color);
            gs_draw_sprite(nullptr, 0, static_cast<uint32_t>(key.w),
                           static_cast<uint32_t>(key.h));
        }

        gs_matrix_pop();
    }

    // Draw black keys
    for (const auto &key : ps->black_keys)
    {
        auto it = active_notes.find(key.midi_note);
        struct vec4 color, oclr;

        if (it != active_notes.end())
        {
            vec4_from_rgba(&color, ps->active_note_color);
            if (ps->velocity_sensitive && it->second > 0)
            {
                float t = it->second / 127.0f;
                color.w *= t;
            }
        }
        else
        {
            vec4_from_rgba(&color, ps->black_key_color);
        }

        vec4_from_rgba(&oclr, ps->outline_color);

        gs_matrix_push();
        gs_matrix_translate3f(key.x, key.y, 0.0f);

        if (ps->show_outline)
        {
            gs_effect_set_vec4(color_param, &oclr);
            gs_draw_sprite(nullptr, 0, static_cast<uint32_t>(key.w),
                           static_cast<uint32_t>(key.h));

            float ow = static_cast<float>(ps->outline_width);
            gs_matrix_translate3f(ow, ow, 0.0f);
            gs_effect_set_vec4(color_param, &color);
            gs_draw_sprite(nullptr, 0,
                           static_cast<uint32_t>(key.w - ow * 2.0f),
                           static_cast<uint32_t>(key.h - ow * 2.0f));
        }
        else
        {
            gs_effect_set_vec4(color_param, &color);
            gs_draw_sprite(nullptr, 0, static_cast<uint32_t>(key.w),
                           static_cast<uint32_t>(key.h));
        }

        gs_matrix_pop();
    }

    gs_technique_end_pass(tech);
    gs_technique_end(tech);
}

static uint32_t piano_source_width(void *data)
{
    auto *ps = static_cast<PianoSourceData *>(data);
    if (!ps)
        return 1;
    return std::max(ps->width, 1u);
}

static uint32_t piano_source_height(void *data)
{
    auto *ps = static_cast<PianoSourceData *>(data);
    if (!ps)
        return 1;
    return std::max(ps->height, 1u);
}

struct obs_source_info piano_keyboard_source_info = {
    .id = "piano_keyboard_source",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
    .get_name = piano_source_get_name,
    .create = piano_source_create,
    .destroy = piano_source_destroy,
    .get_width = piano_source_width,
    .get_height = piano_source_height,
    .get_defaults = piano_source_defaults,
    .get_properties = piano_source_properties,
    .update = piano_source_update,
    .video_render = piano_source_render,
};
