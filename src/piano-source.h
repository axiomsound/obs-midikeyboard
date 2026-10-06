#pragma once
#include "piano-layout.h"
#include <obs-module.h>
#include <memory>
#include <string>

class MidiInput;

struct PianoSourceData {
	obs_source_t *source = nullptr;
	std::shared_ptr<MidiInput> midi_input;

	std::string midi_device_name;
	PianoRange range;

	uint32_t white_key_color = 0xFFFFFFFF;
	uint32_t black_key_color = 0xFF1A1A1A;
	uint32_t active_note_color = 0xFFFF9933;
	uint32_t outline_color = 0xFF000000;
	bool velocity_sensitive = true;
	bool show_outline = true;
	int outline_width = 1;

	PianoRange last_range{-1, 0};
	PianoLayout layout;

	void calculate_layout();
};
