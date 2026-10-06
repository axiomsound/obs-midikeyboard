#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <obs.h>
#include <graphics/graphics.h>
#include <util/base.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using Data = std::unique_ptr<obs_data_t, decltype(&obs_data_release)>;
using Source = std::unique_ptr<obs_source_t, decltype(&obs_source_release)>;

void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
	std::cout << "PASS: " << message << '\n';
}

void load_module(const std::filesystem::path &binary, const std::filesystem::path &data)
{
	obs_module_t *module = nullptr;
	require(obs_open_module(&module, binary.string().c_str(), data.string().c_str()) == MODULE_SUCCESS,
		"Module can be opened");
	require(obs_init_module(module), "Module can be initialized");
}

void wait_for_video()
{
	std::this_thread::sleep_for(std::chrono::milliseconds(400));
	obs_queue_task(OBS_TASK_GRAPHICS, [](void *) {}, nullptr, true);
}

struct Pixels {
	uint32_t width;
	uint32_t height;
	std::vector<uint8_t> rgba;
	size_t visible = 0;
	uint32_t min_x = 0;
	uint32_t max_x = 0;
	uint32_t min_y = 0;
};

Pixels capture(obs_source_t *source, uint32_t scale = 1)
{
	obs_enter_graphics();
	Pixels result{};
	result.width = obs_source_get_width(source) * scale;
	result.height = obs_source_get_height(source) * scale;
	result.min_x = result.width;
	result.min_y = result.height;
	auto *target = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	auto *stage = gs_stagesurface_create(result.width, result.height, GS_RGBA);
	if (!target || !stage || !gs_texrender_begin(target, result.width, result.height)) {
		gs_stagesurface_destroy(stage);
		gs_texrender_destroy(target);
		obs_leave_graphics();
		throw std::runtime_error("Cannot create offscreen render target");
	}
	gs_projection_push();
	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_scale3f(static_cast<float>(scale), static_cast<float>(scale), 1.0f);
	gs_ortho(0, static_cast<float>(result.width), 0, static_cast<float>(result.height), -100, 100);
	vec4 transparent{};
	gs_clear(GS_CLEAR_COLOR, &transparent, 0, 0);
	obs_source_video_render(source);
	gs_matrix_pop();
	gs_projection_pop();
	gs_texrender_end(target);
	gs_stage_texture(stage, gs_texrender_get_texture(target));
	uint8_t *bytes = nullptr;
	uint32_t stride = 0;
	if (gs_stagesurface_map(stage, &bytes, &stride)) {
		result.rgba.resize(static_cast<size_t>(result.width) * result.height * 4);
		for (uint32_t row = 0; row < result.height; ++row) {
			auto *destination = result.rgba.data() + static_cast<size_t>(row) * result.width * 4;
			std::memcpy(destination, bytes + static_cast<size_t>(row) * stride, result.width * 4);
			for (uint32_t column = 0; column < result.width; ++column) {
				if (destination[column * 4 + 3] > 10) {
					++result.visible;
					result.min_x = std::min(result.min_x, column);
					result.max_x = std::max(result.max_x, column);
					result.min_y = std::min(result.min_y, row);
				}
			}
		}
		gs_stagesurface_unmap(stage);
	}
	gs_stagesurface_destroy(stage);
	gs_texrender_destroy(target);
	obs_leave_graphics();
	return result;
}

std::vector<Source> children(obs_source_t *parent)
{
	std::vector<Source> result;
	obs_source_enum_active_sources(
		parent,
		[](obs_source_t *, obs_source_t *child, void *parameter) {
			auto &sources = *static_cast<std::vector<Source> *>(parameter);
			sources.emplace_back(obs_source_get_ref(child), obs_source_release);
		},
		&result);
	return result;
}

void save_preview(const Pixels &image, const std::filesystem::path &path)
{
	std::ofstream output(path, std::ios::binary);
	output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
	for (size_t index = 0; index < image.rgba.size(); index += 4) {
		// Composite the transparent capture onto a dark background for inspection.
		const auto alpha = image.rgba[index + 3];
		for (size_t channel = 0; channel < 3; ++channel) {
			const auto value = static_cast<uint8_t>(image.rgba[index + channel] * alpha / 255 +
								24 * (255 - alpha) / 255);
			output.put(static_cast<char>(value));
		}
	}
}

void verify_text_quality(obs_source_t *source, obs_data_t *settings, const std::filesystem::path &directory)
{
	obs_data_set_bool(settings, "text_outline", false);
	obs_data_set_int(settings, "text_width", 639);
	obs_data_set_int(settings, "text_height", 119);
	obs_source_update(source, settings);
	wait_for_video();
	save_preview(capture(source), directory / "chord-quality-1x.ppm");
	for (int frame = 0; frame < 3; ++frame) {
		capture(source, 4);
		wait_for_video();
	}
	const auto enlarged = capture(source, 4);
	save_preview(enlarged, directory / "chord-quality-4x.ppm");
	const auto nested = children(source);
	Data native_settings(obs_source_get_settings(nested.front().get()), obs_data_release);
	Data native_font(obs_data_get_obj(native_settings.get(), "font"), obs_data_release);
	Data selected_font(obs_data_get_obj(settings, "chord_font"), obs_data_release);
	require(obs_data_get_int(native_font.get(), "size") == 256 &&
			obs_data_get_int(selected_font.get(), "size") == 64,
		"Enlarging the source rerasterizes the font while preserving the selected size");
	const auto native = capture(nested.front().get());
	const auto offset_x = static_cast<int>(enlarged.min_x) - static_cast<int>(native.min_x);
	const auto offset_y = static_cast<int>(enlarged.min_y) - static_cast<int>(native.min_y);
	bool identical = true;
	for (uint32_t y = 0; y < native.height; ++y) {
		for (uint32_t x = 0; x < native.width; ++x) {
			const int destination_x = static_cast<int>(x) + offset_x;
			const int destination_y = static_cast<int>(y) + offset_y;
			if (destination_x < 0 || destination_y < 0 ||
			    destination_x >= static_cast<int>(enlarged.width) ||
			    destination_y >= static_cast<int>(enlarged.height)) {
				identical = false;
				continue;
			}
			const int actual =
				enlarged.rgba[(static_cast<size_t>(destination_y) * enlarged.width + destination_x) * 4 +
					      3];
			const int expected = native.rgba[(static_cast<size_t>(y) * native.width + x) * 4 + 3];
			identical &= std::abs(actual - expected) <= 2;
		}
	}
	require(identical, "At 4x the chord preserves native glyph coverage without stretching a smaller bitmap");
	obs_data_set_int(settings, "text_width", 640);
	obs_data_set_int(settings, "text_height", 120);
	obs_source_update(source, settings);
	wait_for_video();
}

void verify_keyboard(obs_source_t *keyboard)
{
	Data settings(obs_source_get_settings(keyboard), obs_data_release);
	auto *properties = obs_source_properties(keyboard);
	auto *devices = obs_properties_get(properties, "midi_device");
	const std::string selected = obs_data_get_string(settings.get(), "midi_device");
	auto selected_count = [&] {
		size_t count = 0;
		for (size_t index = 0; index < obs_property_list_item_count(devices); ++index)
			count += selected == obs_property_list_item_string(devices, index);
		return count;
	};
	require(selected_count() == 1, "Keyboard properties preserve the selected missing device");
	require(std::string(obs_property_description(obs_properties_get(properties, "device_status"))) ==
			"Waiting for the selected MIDI device. Reconnection is automatic.",
		"Keyboard properties explain why the selected device is unavailable");
	for (int attempt = 0; attempt < 2; ++attempt) {
		require(obs_property_button_clicked(obs_properties_get(properties, "refresh_devices"), keyboard),
			"Keyboard device list can be refreshed");
		require(selected_count() == 1, "Refresh neither drops nor duplicates the missing selected port");
	}
	obs_data_set_int(settings.get(), "num_keys", 88);
	obs_data_set_int(settings.get(), "first_note", 108);
	require(obs_property_modified(obs_properties_get(properties, "num_keys"), settings.get()) &&
			obs_property_int_max(obs_properties_get(properties, "first_note")) == 40 &&
			obs_data_get_int(settings.get(), "first_note") == 40,
		"Changing the number of keys constrains the first-note control to MIDI 127");
	obs_data_set_int(settings.get(), "num_keys", 25);
	obs_data_set_int(settings.get(), "first_note", 127);
	obs_property_modified(obs_properties_get(properties, "first_note"), settings.get());
	require(obs_property_int_max(obs_properties_get(properties, "first_note")) == 103 &&
			obs_data_get_int(settings.get(), "first_note") == 103,
		"Changing the first note also validates the combined MIDI range");
	obs_properties_destroy(properties);

	struct Example {
		int first;
		int count;
		uint32_t width;
	};
	const Example examples[] = {{36, 25, 360},  {36, 49, 696}, {36, 61, 864},
				    {21, 88, 1248}, {37, 25, 350}, {36, 26, 367}};
	for (const auto &example : examples) {
		obs_data_set_int(settings.get(), "first_note", example.first);
		obs_data_set_int(settings.get(), "num_keys", example.count);
		for (const auto outline : {0, 1, 4}) {
			obs_data_set_bool(settings.get(), "show_outline", outline != 0);
			obs_data_set_int(settings.get(), "outline_width", outline ? outline : 1);
			obs_source_update(keyboard, settings.get());
			wait_for_video();
			const auto pixels = capture(keyboard);
			require(pixels.width == example.width && pixels.height == 120 && pixels.visible > 0,
				"Keyboard renders standard and black-edge ranges with supported outlines");
			if (example.first == 37) {
				const auto alpha = [&](uint32_t x, uint32_t y) {
					return pixels.rgba[(static_cast<size_t>(y) * pixels.width + x) * 4 + 3];
				};
				require(alpha(0, 36) == 255 && alpha(pixels.width - 1, 36) == 255,
					"Both boundary black keys render to the full edges of the canvas");
				require(alpha(0, 100) == 0 && alpha(pixels.width - 1, 100) == 0,
					"Empty space below boundary black keys remains transparent");
			}
		}
	}

	obs_data_set_int(settings.get(), "num_keys", std::numeric_limits<long long>::max());
	obs_data_set_int(settings.get(), "first_note", std::numeric_limits<long long>::max());
	obs_data_set_int(settings.get(), "outline_width", std::numeric_limits<long long>::max());
	Source invalid(obs_source_create_private("piano_keyboard_source", "Invalid saved keyboard", settings.get()),
		       obs_source_release);
	require(invalid != nullptr, "Keyboard can load a scene with extreme saved integer values");
	Data normalized(obs_source_get_settings(invalid.get()), obs_data_release);
	require(obs_data_get_int(normalized.get(), "num_keys") == 88 &&
			obs_data_get_int(normalized.get(), "first_note") == 40 &&
			obs_data_get_int(normalized.get(), "outline_width") == 4,
		"Keyboard create normalizes saved ranges and outlines before integer conversion");
	obs_data_set_int(normalized.get(), "num_keys", -1);
	obs_data_set_int(normalized.get(), "first_note", -1);
	obs_data_set_int(normalized.get(), "outline_width", -1);
	obs_source_update(invalid.get(), normalized.get());
	wait_for_video();
	require(obs_data_get_int(normalized.get(), "num_keys") == 25 &&
			obs_data_get_int(normalized.get(), "first_note") == 0 &&
			obs_data_get_int(normalized.get(), "outline_width") == 1,
		"Keyboard update also normalizes invalid saved values");
	require(capture(invalid.get()).width == 360, "Normalized saved keyboard renders a bounded canvas");
	Data saved(obs_save_source(invalid.get()), obs_data_release);
	obs_data_erase(saved.get(), "uuid");
	obs_data_set_string(saved.get(), "name", "Restored normalized keyboard");
	Data serialized(obs_data_create_from_json(obs_data_get_json(saved.get())), obs_data_release);
	Source restored(obs_load_source(serialized.get()), obs_source_release);
	require(restored != nullptr && capture(restored.get()).width == 360,
		"Normalized keyboard settings survive a serialized scene round trip");
}

void run(std::filesystem::path plugin, std::filesystem::path plugin_data, std::filesystem::path runtime, bool freetype)
{
	plugin = std::filesystem::absolute(plugin);
	plugin_data = std::filesystem::absolute(plugin_data);
	runtime = std::filesystem::absolute(runtime);
	const auto preview_path = std::filesystem::current_path() / "chord-preview.ppm";
	const auto fixture = std::filesystem::current_path() / "obs-smoke-layout";
	std::filesystem::create_directories(fixture / "bin/64bit");
	std::filesystem::create_directories(fixture / "data/libobs");
	std::filesystem::copy(runtime / "data/libobs", fixture / "data/libobs",
			      std::filesystem::copy_options::recursive |
				      std::filesystem::copy_options::overwrite_existing);
	// libobs on Windows searches ../../data/libobs relative to the working directory.
	// Stage its shader files in the build directory rather than using a deprecated API.
	std::filesystem::current_path(fixture / "bin/64bit");
	const auto graphics = runtime / "bin/64bit/libobs-d3d11.dll";
	obs_video_info video{};
	const auto graphics_path = graphics.string();
	video.graphics_module = graphics_path.c_str();
	video.fps_num = 30;
	video.fps_den = 1;
	video.base_width = video.output_width = 640;
	video.base_height = video.output_height = 120;
	video.output_format = VIDEO_FORMAT_RGBA;
	video.colorspace = VIDEO_CS_DEFAULT;
	video.range = VIDEO_RANGE_FULL;
	video.scale_type = OBS_SCALE_BILINEAR;
	require(obs_reset_video(&video) == OBS_VIDEO_SUCCESS, "OBS graphics initialize");
	const auto text_module = freetype ? "text-freetype2" : "obs-text";
	load_module(runtime / "obs-plugins/64bit" / (std::string(text_module) + ".dll"),
		    runtime / "data/obs-plugins" / text_module);
	load_module(plugin, plugin_data);
	require(obs_get_latest_input_type_id("midi_chord_source") != nullptr, "Chord source is registered");
	require(obs_get_latest_input_type_id("piano_keyboard_source") != nullptr, "Keyboard source remains registered");
	Data settings(obs_get_source_defaults("midi_chord_source"), obs_data_release);
	obs_data_set_bool(settings.get(), "preview_chord", true);
	Source chord(obs_source_create_private("midi_chord_source", "Chord integration test", settings.get()),
		     obs_source_release);
	require(chord != nullptr, "Chord source can be created");
	obs_set_output_source(0, chord.get());
	wait_for_video();
	const auto preview = capture(chord.get());
	require(preview.width == 640 && preview.height == 120, "Default text area has fixed dimensions");
	require(preview.visible > 100, "Sample chord renders visible glyphs");
	require(preview.rgba[3] == 0, "Background is transparent");
	save_preview(preview, preview_path);
	{
		const auto nested = children(chord.get());
		require(nested.size() == 2, "Private text and outline sources are enumerated");
		for (const auto &child : nested) {
			Data child_settings(obs_source_get_settings(child.get()), obs_data_release);
			require(std::string(obs_data_get_string(child_settings.get(), "text")) == "Cmaj7/E",
				"Sample chord reaches the native text source");
			Data font(obs_data_get_obj(child_settings.get(), "font"), obs_data_release);
			require(font && obs_data_get_int(font.get(), "size") == 128,
				"Default font survives repeated application of source defaults");
		}
	}
	verify_text_quality(chord.get(), settings.get(), preview_path.parent_path());
	obs_data_set_bool(settings.get(), "text_outline", false);
	obs_source_update(chord.get(), settings.get());
	wait_for_video();
	require(capture(chord.get()).visible < preview.visible, "Outline adds a visible border around the glyphs");
	obs_data_set_bool(settings.get(), "text_outline", true);
	obs_data_set_int(settings.get(), "text_color", 0x0000FF);
	obs_data_set_int(settings.get(), "text_outline_color", 0xFF0000);
	obs_source_update(chord.get(), settings.get());
	wait_for_video();
	const auto colored = capture(chord.get());
	bool red = false;
	bool blue = false;
	for (size_t index = 0; index < colored.rgba.size(); index += 4) {
		if (colored.rgba[index + 3] < 100)
			continue;
		red |= colored.rgba[index] > 150 && colored.rgba[index + 2] < 100;
		blue |= colored.rgba[index + 2] > 150 && colored.rgba[index] < 100;
	}
	require(red && blue, "Text and outline colors use OBS's channel order");
	Data custom_font(obs_data_create(), obs_data_release);
	obs_data_set_string(custom_font.get(), "face", "Arial");
	obs_data_set_string(custom_font.get(), "style", "Bold");
	obs_data_set_int(custom_font.get(), "size", 96);
	obs_data_set_int(custom_font.get(), "flags", OBS_FONT_BOLD);
	obs_data_set_obj(settings.get(), "chord_font", custom_font.get());
	obs_data_set_int(settings.get(), "text_color", 0xFFFFFF);
	obs_data_set_int(settings.get(), "text_outline_color", 0);
	obs_data_set_int(settings.get(), "text_alignment", 0);
	obs_source_update(chord.get(), settings.get());
	wait_for_video();
	const auto left = capture(chord.get());
	obs_data_set_int(settings.get(), "text_alignment", 2);
	obs_source_update(chord.get(), settings.get());
	wait_for_video();
	const auto right = capture(chord.get());
	require(left.visible > 0 && right.visible > 0 && left.min_x < right.min_x,
		"Left and right alignment move the label");
	obs_data_set_int(settings.get(), "text_width", 64);
	obs_data_set_int(settings.get(), "text_height", 32);
	obs_source_update(chord.get(), settings.get());
	wait_for_video();
	const auto compact = capture(chord.get());
	require(compact.width == 64 && compact.height == 32 && compact.visible > 0, "Text fits a small fixed area");
	obs_data_set_int(settings.get(), "text_width", -10);
	obs_data_set_int(settings.get(), "text_height", 0);
	obs_source_update(chord.get(), settings.get());
	wait_for_video();
	require(obs_source_get_width(chord.get()) == 64 && obs_source_get_height(chord.get()) == 32,
		"Invalid saved dimensions are clamped");
	obs_data_set_bool(settings.get(), "preview_chord", false);
	obs_source_update(chord.get(), settings.get());
	wait_for_video();
	require(capture(chord.get()).visible == 0, "No MIDI notes leaves the label empty");
	{
		Data saved(obs_save_source(chord.get()), obs_data_release);
		// Load a second instance while the original is alive, with a fresh UUID.
		obs_data_erase(saved.get(), "uuid");
		obs_data_set_string(saved.get(), "name", "Restored chord test");
		Source restored(obs_load_source(saved.get()), obs_source_release);
		require(restored != nullptr, "Chord source can be restored from a saved scene");
		wait_for_video();
		require(obs_source_get_width(restored.get()) == 64, "Fixed dimensions survive save and reload");
		Data serialized(obs_data_create_from_json(obs_data_get_json(saved.get())), obs_data_release);
		obs_data_set_string(serialized.get(), "name", "JSON chord test");
		Source from_json(obs_load_source(serialized.get()), obs_source_release);
		require(from_json != nullptr, "Chord source reloads from serialized scene JSON");
		wait_for_video();
		const auto nested = children(from_json.get());
		Data child_settings(obs_source_get_settings(nested.front().get()), obs_data_release);
		Data font(obs_data_get_obj(child_settings.get(), "font"), obs_data_release);
		Data source_settings(obs_source_get_settings(from_json.get()), obs_data_release);
		Data selected_font(obs_data_get_obj(source_settings.get(), "chord_font"), obs_data_release);
		require(font && obs_data_get_int(selected_font.get(), "size") == 96 &&
				obs_data_get_int(font.get(), "size") >= 96 &&
				obs_data_get_int(font.get(), "flags") == OBS_FONT_BOLD,
			"Selected font survives a serialized scene round trip");
	}
	obs_data_set_string(settings.get(), "midi_device", "__obs_midikeyboard_missing_test_device__");
	obs_source_update(chord.get(), settings.get());
	Data keyboard_settings(obs_get_source_defaults("piano_keyboard_source"), obs_data_release);
	obs_data_set_string(keyboard_settings.get(), "midi_device", "__obs_midikeyboard_missing_test_device__");
	Source keyboard(obs_source_create_private("piano_keyboard_source", "Keyboard integration test",
						  keyboard_settings.get()),
			obs_source_release);
	require(keyboard != nullptr, "Keyboard and chord sources can use the same unavailable device");
	verify_keyboard(keyboard.get());
	wait_for_video();
	keyboard.reset();
	require(capture(chord.get()).visible == 0, "Removing the keyboard keeps the chord source alive");
	auto *properties = obs_source_properties(chord.get());
	require(properties != nullptr, "Chord properties remain accessible while waiting for a device");
	obs_properties_destroy(properties);
	obs_set_output_source(0, nullptr);
}

} // namespace

int main(int argc, char **argv)
{
#ifdef _WIN32
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	SetUnhandledExceptionFilter([](EXCEPTION_POINTERS *exception) -> LONG {
		std::fprintf(stderr, "Native exception: 0x%08lX\n", exception->ExceptionRecord->ExceptionCode);
		HMODULE module = nullptr;
		GetModuleHandleExA(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<const char *>(exception->ExceptionRecord->ExceptionAddress), &module);
		char path[MAX_PATH]{};
		GetModuleFileNameA(module, path, MAX_PATH);
		std::fprintf(stderr, "Location: %s + 0x%llX, access 0x%llX\n", path,
			     static_cast<unsigned long long>(
				     reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress) -
				     reinterpret_cast<uintptr_t>(module)),
			     static_cast<unsigned long long>(exception->ExceptionRecord->ExceptionInformation[1]));
		return EXCEPTION_EXECUTE_HANDLER;
	});
#endif
	std::jthread watchdog([](std::stop_token stop) {
		for (int step = 0; step < 400 && !stop.stop_requested(); ++step)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		if (!stop.stop_requested()) {
			std::cerr << "FAIL: Integration test timed out\n";
			std::_Exit(3);
		}
	});
	if (argc < 4) {
		std::cerr << "Usage: obs-chord-smoke <plugin.dll> <plugin-data-dir> <OBS-install-dir> [freetype]\n";
		return 2;
	}
	if (!obs_startup("en-US", "obs-smoke-config", nullptr))
		return 2;
	base_set_crash_handler(
		[](const char *format, va_list args, void *) {
			std::vfprintf(stderr, format, args);
			std::fflush(stderr);
			std::_Exit(4);
		},
		nullptr);
	int result = 0;
	try {
		run(argv[1], argv[2], argv[3], argc > 4);
	} catch (const std::exception &error) {
		std::cerr << "FAIL: " << error.what() << '\n';
		result = 1;
	}
	obs_set_output_source(0, nullptr);
	obs_wait_for_destroy_queue();
	obs_shutdown();
	return result;
}
