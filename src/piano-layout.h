#pragma once

#include <cstdint>
#include <vector>

struct PianoRange {
	int first_note = 36;
	int num_keys = 49;

	bool operator==(const PianoRange &) const = default;
};

struct KeyRect {
	float x, y, w, h;
	int midi_note;
};

struct PianoLayout {
	std::vector<KeyRect> white_keys;
	std::vector<KeyRect> black_keys;
	uint32_t width = 1;
	uint32_t height = 120;
};

PianoRange normalize_piano_range(int64_t first_note, int64_t num_keys);
int normalize_piano_outline_width(int64_t width);
PianoLayout make_piano_layout(PianoRange range);
