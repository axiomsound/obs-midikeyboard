#include "piano-layout.h"

#include <algorithm>

namespace {

constexpr float white_width = 24.0f;
constexpr float white_height = 120.0f;
constexpr float black_width = 14.0f;
constexpr float black_height = 72.0f;

bool is_black_key(int note)
{
	const auto semitone = note % 12;
	return semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10;
}

} // namespace

PianoRange normalize_piano_range(int64_t first_note, int64_t num_keys)
{
	const auto count = static_cast<int>(std::clamp<int64_t>(num_keys, 25, 88));
	return {static_cast<int>(std::clamp<int64_t>(first_note, 0, 128 - count)), count};
}

int normalize_piano_outline_width(int64_t width)
{
	return static_cast<int>(std::clamp<int64_t>(width, 1, 4));
}

PianoLayout make_piano_layout(PianoRange range)
{
	range = normalize_piano_range(range.first_note, range.num_keys);
	PianoLayout layout;
	int whites = 0;
	float left = 0.0f;
	float right = 0.0f;
	for (int index = 0; index < range.num_keys; ++index) {
		const auto note = range.first_note + index;
		const bool black = is_black_key(note);
		const float x = whites * white_width - (black ? black_width * 0.5f : 0.0f);
		const KeyRect key{x, 0.0f, black ? black_width : white_width, black ? black_height : white_height,
				  note};
		(black ? layout.black_keys : layout.white_keys).push_back(key);
		left = std::min(left, key.x);
		right = std::max(right, key.x + key.w);
		if (!black)
			++whites;
	}
	for (auto *keys : {&layout.white_keys, &layout.black_keys}) {
		for (auto &key : *keys)
			key.x -= left;
	}
	layout.width = static_cast<uint32_t>(right - left);
	layout.height = static_cast<uint32_t>(white_height);
	return layout;
}
