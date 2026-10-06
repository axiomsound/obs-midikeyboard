#include "chord-detector.h"
#include "midi-input.h"

#include <obs-module.h>
#include <graphics/graphics.h>
#include <graphics/matrix4.h>
#include <util/platform.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>

namespace {

struct ChordSourceData {
	obs_source_t *source = nullptr;
	obs_source_t *text = nullptr;
	obs_source_t *outline_text = nullptr;
	std::shared_ptr<MidiInput> midi;
	std::string device;
	std::string displayed;
	ChordTracker tracker;
	uint32_t width = 640;
	uint32_t height = 120;
	int channel = -1;
	int alignment = 1;
	bool include_sustain = false;
	bool show_bass = true;
	bool prefer_flats = false;
	bool preview = false;
	bool outline = true;
	bool was_connected = false;
	int font_size = 64;
	int raster_density = 2;
	int pending_density = 0;
	int native_font_size = 128;
	uint32_t pending_width = 0;
	uint32_t pending_height = 0;
	std::atomic<int> requested_density{2};

	~ChordSourceData()
	{
		for (auto *child : {text, outline_text}) {
			if (child) {
				obs_source_remove_active_child(source, child);
				obs_source_release(child);
			}
		}
	}
};

int desired_density(const ChordSourceData *cs, const std::string &label)
{
	// Leave room below native text modules' texture limits, including long labels.
	const auto budget = 3072 / (cs->font_size * std::max<size_t>(1, label.size()));
	return std::min(cs->requested_density.load(), static_cast<int>(std::clamp<size_t>(budget, 1, 4)));
}

void prepare_density(ChordSourceData *cs, int density)
{
	const int native_size = cs->font_size * density;
	if (!cs->text || density == cs->raster_density ||
	    (!cs->pending_density && native_size == cs->native_font_size)) {
		cs->raster_density = density;
		cs->pending_density = 0;
	} else if (cs->pending_density == density && native_size == cs->native_font_size) {
		// A style update can arrive while this font change is still queued.
		return;
	} else {
		// Child updates are deferred. Keep the old scale until the new raster is ready.
		cs->pending_density = density;
		cs->pending_width = obs_source_get_width(cs->text);
		cs->pending_height = obs_source_get_height(cs->text);
	}
	cs->native_font_size = native_size;
}

void update_raster_density(ChordSourceData *cs)
{
	if (!cs->text || cs->displayed.empty())
		return;
	const int density = desired_density(cs, cs->displayed);
	if (cs->native_font_size == cs->font_size * density)
		return;
	prepare_density(cs, density);
	for (auto *child : {cs->text, cs->outline_text}) {
		if (!child)
			continue;
		auto *settings = obs_source_get_settings(child);
		auto *previous = obs_data_get_obj(settings, "font");
		auto *font = obs_data_create();
		obs_data_apply(font, previous);
		obs_data_set_int(font, "size", cs->native_font_size);
		obs_data_set_obj(settings, "font", font);
		obs_data_release(font);
		obs_data_release(previous);
		obs_source_update(child, settings);
		obs_data_release(settings);
	}
}

void populate_devices(obs_property_t *list, const std::string &selected)
{
	obs_property_list_clear(list);
	obs_property_list_add_string(list, obs_module_text("None"), "");
	const auto devices = MidiManager::instance().list_devices();
	for (const auto &device : devices)
		obs_property_list_add_string(list, device.c_str(), device.c_str());
	if (!selected.empty() && std::find(devices.begin(), devices.end(), selected) == devices.end())
		obs_property_list_add_string(list, selected.c_str(), selected.c_str());
}

bool refresh_devices(obs_properties_t *properties, obs_property_t *, void *data)
{
	auto *cs = static_cast<ChordSourceData *>(data);
	populate_devices(obs_properties_get(properties, "midi_device"), cs ? cs->device : "");
	return true;
}

void update_text(ChordSourceData *cs, const std::string &text)
{
	if (cs->displayed == text)
		return;
	cs->displayed = text;
	if (!cs->text)
		return;
	auto *settings = obs_data_create();
	obs_data_set_string(settings, "text", text.c_str());
	obs_source_update(cs->text, settings);
	if (cs->outline_text)
		obs_source_update(cs->outline_text, settings);
	obs_data_release(settings);
}

void chord_source_update(void *data, obs_data_t *settings)
{
	auto *cs = static_cast<ChordSourceData *>(data);
	cs->width = static_cast<uint32_t>(std::clamp(obs_data_get_int(settings, "text_width"), 64LL, 4096LL));
	cs->height = static_cast<uint32_t>(std::clamp(obs_data_get_int(settings, "text_height"), 32LL, 2160LL));
	cs->alignment = static_cast<int>(std::clamp(obs_data_get_int(settings, "text_alignment"), 0LL, 2LL));
	const auto channel = static_cast<int>(std::clamp(obs_data_get_int(settings, "midi_channel"), -1LL, 15LL));
	const bool include_sustain = obs_data_get_bool(settings, "include_sustain");
	if (channel != cs->channel || include_sustain != cs->include_sustain)
		cs->tracker.reset();
	cs->channel = channel;
	cs->include_sustain = include_sustain;
	cs->show_bass = obs_data_get_bool(settings, "show_bass");
	cs->prefer_flats = obs_data_get_bool(settings, "prefer_flats");
	cs->preview = obs_data_get_bool(settings, "preview_chord");
	cs->outline = obs_data_get_bool(settings, "text_outline");
	const auto settle = static_cast<uint64_t>(std::clamp(obs_data_get_int(settings, "settle_ms"), 0LL, 500LL));
	const auto clear = static_cast<uint64_t>(std::clamp(obs_data_get_int(settings, "clear_ms"), 0LL, 2000LL));
	cs->tracker.configure(settle, clear);

	const std::string device = obs_data_get_string(settings, "midi_device");
	if (device != cs->device) {
		cs->tracker.reset();
		cs->device = device;
		cs->midi = MidiManager::instance().get_input(device);
		cs->was_connected = false;
	}
	const auto &current = cs->tracker.current();
	const auto label = cs->preview ? std::string("Cmaj7/E")
			   : current   ? format_chord(*current, cs->show_bass, cs->prefer_flats)
				       : std::string();
	auto *selected = obs_data_get_obj(settings, "chord_font");
	const auto size = selected ? obs_data_get_int(selected, "size") : 64;
	cs->font_size = static_cast<int>(std::clamp(size > 0 ? size : 64, 8LL, 256LL));
	obs_data_release(selected);
	prepare_density(cs, desired_density(cs, label));
	for (auto *child : {cs->text, cs->outline_text}) {
		if (!child)
			continue;
		auto *text_settings = obs_data_create();
		auto *selected_font = obs_data_get_obj(settings, "chord_font");
		auto *font = obs_data_create();
		const char *face = selected_font ? obs_data_get_string(selected_font, "face") : "";
		obs_data_set_string(font, "face", *face ? face : "Arial");
		const char *style = selected_font ? obs_data_get_string(selected_font, "style") : "";
		obs_data_set_string(font, "style", *style ? style : "Regular");
		obs_data_set_int(font, "size", cs->native_font_size);
		obs_data_set_int(font, "flags", selected_font ? obs_data_get_int(selected_font, "flags") : 0);
		obs_data_release(selected_font);
		obs_data_set_obj(text_settings, "font", font);
		obs_data_release(font);
		const auto color = static_cast<uint32_t>(
			obs_data_get_int(settings, child == cs->outline_text ? "text_outline_color" : "text_color"));
		obs_data_set_int(text_settings, "color", color & 0xFFFFFF);
		obs_data_set_int(text_settings, "opacity", 100);
		obs_data_set_int(text_settings, "color1", color | 0xFF000000);
		obs_data_set_int(text_settings, "color2", color | 0xFF000000);
		obs_data_set_bool(text_settings, "outline", false);
		obs_data_set_int(text_settings, "bk_opacity", 0);
		obs_data_set_bool(text_settings, "extents", false);
		obs_data_set_bool(text_settings, "word_wrap", false);
		obs_data_set_int(text_settings, "custom_width", 0);
		obs_data_set_bool(text_settings, "antialiasing", true);
		obs_data_set_string(text_settings, "text", label.c_str());
		obs_source_update(child, text_settings);
		obs_data_release(text_settings);
	}
	cs->displayed = label;
}

void *chord_source_create(obs_data_t *settings, obs_source_t *source)
{
	auto cs = std::make_unique<ChordSourceData>();
	cs->source = source;
#ifdef _WIN32
	const char *id = obs_get_latest_input_type_id("text_gdiplus");
	if (!id)
		id = obs_get_latest_input_type_id("text_ft2_source");
#else
	const char *id = obs_get_latest_input_type_id("text_ft2_source");
#endif
	if (id) {
		cs->text = obs_source_create_private(id, "MIDI chord text", nullptr);
		if (cs->text && !obs_source_add_active_child(source, cs->text)) {
			obs_source_release(cs->text);
			cs->text = nullptr;
		}
		cs->outline_text = obs_source_create_private(id, "MIDI chord outline", nullptr);
		if (cs->outline_text && !obs_source_add_active_child(source, cs->outline_text)) {
			obs_source_release(cs->outline_text);
			cs->outline_text = nullptr;
		}
	}
	if (!cs->text)
		blog(LOG_WARNING, "[MIDI Keyboard] Chord source requires an OBS text module");
	chord_source_update(cs.get(), settings);
	return cs.release();
}

void chord_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "midi_device", "");
	obs_data_set_default_int(settings, "midi_channel", -1);
	obs_data_set_default_bool(settings, "include_sustain", false);
	obs_data_set_default_bool(settings, "show_bass", true);
	obs_data_set_default_bool(settings, "prefer_flats", false);
	obs_data_set_default_int(settings, "settle_ms", 70);
	obs_data_set_default_int(settings, "clear_ms", 180);
	obs_data_set_default_int(settings, "text_width", 640);
	obs_data_set_default_int(settings, "text_height", 120);
	obs_data_set_default_int(settings, "text_alignment", 1);
	obs_data_set_default_int(settings, "text_color", 0xFFFFFF);
	obs_data_set_default_bool(settings, "text_outline", true);
	obs_data_set_default_int(settings, "text_outline_color", 0);
	obs_data_set_default_bool(settings, "preview_chord", false);
	// OBS may apply defaults repeatedly to the same settings object during load.
	// Keep an existing default font object and its ownership intact.
	if (obs_data_has_default_value(settings, "chord_font"))
		return;
	auto *font = obs_data_create();
	obs_data_set_default_string(font, "face", "Arial");
	obs_data_set_default_string(font, "style", "Regular");
	obs_data_set_default_int(font, "size", 64);
	obs_data_set_default_int(font, "flags", 0);
	obs_data_set_default_obj(settings, "chord_font", font);
	obs_data_release(font);
}

obs_properties_t *chord_source_properties(void *data)
{
	auto *cs = static_cast<ChordSourceData *>(data);
	auto *properties = obs_properties_create();
	auto *midi = obs_properties_create();
	auto *devices = obs_properties_add_list(midi, "midi_device", obs_module_text("MIDIDevice"), OBS_COMBO_TYPE_LIST,
						OBS_COMBO_FORMAT_STRING);
	populate_devices(devices, cs ? cs->device : "");
	obs_properties_add_button2(midi, "refresh_devices", obs_module_text("RefreshDevices"), refresh_devices, cs);
	const char *status = !cs || cs->device.empty()         ? "Chord.SelectDevice"
			     : cs->midi && cs->midi->is_open() ? "Chord.Connected"
							       : "Chord.WaitingDevice";
	obs_properties_add_text(midi, "connection_status", obs_module_text(status), OBS_TEXT_INFO);
	auto *channels = obs_properties_add_list(midi, "midi_channel", obs_module_text("Chord.Channel"),
						 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(channels, obs_module_text("Chord.AllChannels"), -1);
	for (int channel = 0; channel < 16; ++channel) {
		const auto label = std::to_string(channel + 1);
		obs_property_list_add_int(channels, label.c_str(), channel);
	}
	obs_properties_add_bool(midi, "include_sustain", obs_module_text("Chord.IncludeSustain"));
	obs_properties_add_group(properties, "midi", obs_module_text("Chord.MIDI"), OBS_GROUP_NORMAL, midi);

	auto *appearance = obs_properties_create();
	obs_properties_add_font(appearance, "chord_font", obs_module_text("Chord.Font"));
	obs_properties_add_color(appearance, "text_color", obs_module_text("Chord.Color"));
	obs_properties_add_bool(appearance, "text_outline", obs_module_text("Chord.Outline"));
	obs_properties_add_color(appearance, "text_outline_color", obs_module_text("Chord.OutlineColor"));
	auto *alignment = obs_properties_add_list(appearance, "text_alignment", obs_module_text("Chord.Alignment"),
						  OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(alignment, obs_module_text("Chord.Left"), 0);
	obs_property_list_add_int(alignment, obs_module_text("Chord.Center"), 1);
	obs_property_list_add_int(alignment, obs_module_text("Chord.Right"), 2);
	obs_properties_add_int(appearance, "text_width", obs_module_text("Chord.Width"), 64, 4096, 1);
	obs_properties_add_int(appearance, "text_height", obs_module_text("Chord.Height"), 32, 2160, 1);
	obs_properties_add_bool(appearance, "preview_chord", obs_module_text("Chord.Preview"));
	obs_properties_add_group(properties, "appearance", obs_module_text("Appearance"), OBS_GROUP_NORMAL, appearance);

	auto *recognition = obs_properties_create();
	obs_properties_add_bool(recognition, "show_bass", obs_module_text("Chord.ShowBass"));
	obs_properties_add_bool(recognition, "prefer_flats", obs_module_text("Chord.PreferFlats"));
	obs_properties_add_int_slider(recognition, "settle_ms", obs_module_text("Chord.Settle"), 0, 500, 10);
	obs_properties_add_int_slider(recognition, "clear_ms", obs_module_text("Chord.Clear"), 0, 2000, 10);
	obs_properties_add_text(recognition, "recognition_help", obs_module_text("Chord.Help"), OBS_TEXT_INFO);
	obs_properties_add_group(properties, "recognition", obs_module_text("Chord.Recognition"), OBS_GROUP_NORMAL,
				 recognition);
	if (cs && !cs->text) {
		auto *warning = obs_properties_add_text(properties, "text_warning",
							obs_module_text("Chord.TextMissing"), OBS_TEXT_INFO);
		obs_property_text_set_info_type(warning, OBS_TEXT_INFO_WARNING);
	}
	return properties;
}

void chord_source_tick(void *data, float)
{
	auto *cs = static_cast<ChordSourceData *>(data);
	const bool connected = cs->midi && cs->midi->is_open();
	if (connected != cs->was_connected) {
		cs->was_connected = connected;
		cs->tracker.reset();
		obs_source_update_properties(cs->source);
	}
	if (cs->preview) {
		update_text(cs, "Cmaj7/E");
		update_raster_density(cs);
		return;
	}
	if (!connected) {
		cs->tracker.reset();
		update_text(cs, "");
		return;
	}
	const auto snapshot = cs->midi->snapshot(cs->channel, cs->include_sustain);
	const auto &chord = cs->tracker.update(snapshot.notes, os_gettime_ns() / 1000000);
	update_text(cs, chord ? format_chord(*chord, cs->show_bass, cs->prefer_flats) : "");
	update_raster_density(cs);
}

void chord_source_render(void *data, gs_effect_t *)
{
	auto *cs = static_cast<ChordSourceData *>(data);
	if (!cs->text || cs->displayed.empty())
		return;
	const auto width = obs_source_get_width(cs->text);
	const auto height = obs_source_get_height(cs->text);
	if (!width || !height)
		return;
	if (cs->pending_density && (width != cs->pending_width || height != cs->pending_height)) {
		cs->raster_density = cs->pending_density;
		cs->pending_density = 0;
	}
	matrix4 transform;
	gs_matrix_get(&transform);
	const float scene_scale =
		std::max(std::hypot(transform.x.x, transform.x.y), std::hypot(transform.y.x, transform.y.y));
	const int density = static_cast<int>(std::ceil(std::clamp(scene_scale, 2.0f, 4.0f)));
	int requested = cs->requested_density.load();
	while (requested < density && !cs->requested_density.compare_exchange_weak(requested, density)) {
	}
	constexpr float padding = 4.0f;
	const float available_width = static_cast<float>(cs->width) - 2 * padding;
	const float available_height = static_cast<float>(cs->height) - 2 * padding;
	const float scale = std::min({1.0f / cs->raster_density, available_width / width, available_height / height});
	const float x = std::round(padding + (available_width - width * scale) * (cs->alignment / 2.0f));
	const float y = std::round(padding + (available_height - height * scale) / 2.0f);
	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	gs_blend_state_push();
	if (cs->outline && cs->outline_text && obs_source_get_width(cs->outline_text) == width &&
	    obs_source_get_height(cs->outline_text) == height) {
		// Render a colored outline consistently across GDI+ and FreeType text modules.
		constexpr float offsets[8][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}, {-1, -1}, {-1, 1}, {1, -1}, {1, 1}};
		for (const auto &offset : offsets) {
			gs_matrix_push();
			gs_matrix_translate3f(offset[0], offset[1], 0.0f);
			gs_matrix_scale3f(scale, scale, 1.0f);
			obs_source_video_render(cs->outline_text);
			gs_matrix_pop();
		}
	}
	gs_matrix_scale3f(scale, scale, 1.0f);
	obs_source_video_render(cs->text);
	gs_blend_state_pop();
	gs_matrix_pop();
}

void chord_source_enum(void *data, obs_source_enum_proc_t callback, void *parameter)
{
	auto *cs = static_cast<ChordSourceData *>(data);
	if (cs->text)
		callback(cs->source, cs->text, parameter);
	if (cs->outline_text)
		callback(cs->source, cs->outline_text, parameter);
}

} // namespace

struct obs_source_info midi_chord_source_info = {
	.id = "midi_chord_source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name = [](void *) { return obs_module_text("Chord.SourceName"); },
	.create = chord_source_create,
	.destroy = [](void *data) { delete static_cast<ChordSourceData *>(data); },
	.get_width = [](void *data) { return static_cast<ChordSourceData *>(data)->width; },
	.get_height = [](void *data) { return static_cast<ChordSourceData *>(data)->height; },
	.get_defaults = chord_source_defaults,
	.get_properties = chord_source_properties,
	.update = chord_source_update,
	.video_tick = chord_source_tick,
	.video_render = chord_source_render,
	.enum_active_sources = chord_source_enum,
	.icon_type = OBS_ICON_TYPE_TEXT,
};
