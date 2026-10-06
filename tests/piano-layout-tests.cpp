#include "piano-layout.h"

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

size_t checks = 0;

void require(bool condition, const char *message)
{
	++checks;
	if (!condition)
		throw std::runtime_error(message);
}

void saved_settings_limits()
{
	constexpr auto minimum = std::numeric_limits<int64_t>::min();
	constexpr auto maximum = std::numeric_limits<int64_t>::max();
	struct Example {
		int64_t first;
		int64_t count;
		PianoRange expected;
	};
	const std::array examples = {
		Example{36, 49, {36, 49}},
		Example{108, 88, {40, 88}},
		Example{108, 25, {103, 25}},
		Example{-1, 49, {0, 49}},
		Example{maximum, maximum, {40, 88}},
		Example{minimum, minimum, {0, 25}},
		Example{maximum, minimum, {103, 25}},
		Example{minimum, maximum, {0, 88}},
		Example{127, 1, {103, 25}},
	};
	for (const auto &example : examples) {
		const auto range = normalize_piano_range(example.first, example.count);
		require(range == example.expected, "Saved ranges are clamped before narrowing to int");
		require(normalize_piano_range(range.first_note, range.num_keys) == range,
			"Range normalization is idempotent");
	}
	for (const auto width : {minimum, int64_t{-1}, int64_t{0}, int64_t{1}, int64_t{4}, int64_t{8}, maximum}) {
		const auto outline = normalize_piano_outline_width(width);
		require(outline >= 1 && outline <= 4, "Saved outline width stays in the supported range");
		require(normalize_piano_outline_width(outline) == outline, "Outline normalization is idempotent");
	}
	require(normalize_piano_outline_width(8) == 4, "An oversized outline cannot consume the black key");
}

void known_layouts()
{
	struct Example {
		PianoRange range;
		uint32_t width;
	};
	const std::array examples = {
		Example{{36, 25}, 360},  Example{{36, 49}, 696}, Example{{36, 61}, 864},
		Example{{21, 88}, 1248}, Example{{37, 25}, 350}, Example{{36, 26}, 367},
	};
	for (const auto &example : examples) {
		const auto layout = make_piano_layout(example.range);
		require(layout.width == example.width && layout.height == 120, "Known keyboard dimensions");
		require(layout.white_keys.front().w == 24 && layout.white_keys.front().h == 120,
			"Existing white key dimensions are preserved");
		require(layout.black_keys.front().w == 14 && layout.black_keys.front().h == 72,
			"Existing black key dimensions are preserved");
	}
	const auto black_edges = make_piano_layout({37, 25});
	require(black_edges.black_keys.front().x == 0, "A leading black key is fully inside the canvas");
	require(black_edges.white_keys.front().x == 7,
		"White keys are shifted to leave room for the leading black key");
	require(black_edges.black_keys.back().x + black_edges.black_keys.back().w == black_edges.width,
		"A trailing black key determines the right edge of the canvas");
	const auto invalid = make_piano_layout({108, 88});
	require(invalid.white_keys.size() + invalid.black_keys.size() == 88, "Invalid saved ranges preserve key count");
	require(invalid.white_keys.front().midi_note == 40, "Layout generation also rejects notes beyond MIDI 127");
}

void all_supported_ranges()
{
	for (int count = 25; count <= 88; ++count) {
		for (int first = 0; first + count <= 128; ++first) {
			const PianoRange range{first, count};
			require(normalize_piano_range(first, count) == range, "Valid saved ranges stay unchanged");
			const auto layout = make_piano_layout(range);
			require(layout.width > 0 && layout.height == 120, "Canvas dimensions are positive and bounded");
			require(layout.white_keys.size() + layout.black_keys.size() == static_cast<size_t>(count),
				"Every requested MIDI note has a key");
			std::array<bool, 128> seen{};
			float left = static_cast<float>(layout.width);
			float right = 0;
			for (const auto *keys : {&layout.white_keys, &layout.black_keys}) {
				for (const auto &key : *keys) {
					require(key.midi_note >= first && key.midi_note < first + count,
						"Key labels belong to the selected MIDI range");
					require(!seen[key.midi_note], "Every note appears exactly once");
					seen[key.midi_note] = true;
					require(key.x >= 0 && key.y >= 0 && key.x + key.w <= layout.width &&
							key.y + key.h <= layout.height,
						"Complete key rectangles fit inside the declared canvas");
					left = std::min(left, key.x);
					right = std::max(right, key.x + key.w);
					for (int outline = 1; outline <= 4; ++outline)
						require(key.w - 2 * outline > 0 && key.h - 2 * outline > 0,
							"All supported outlines leave a positive inner rectangle");
				}
			}
			require(left == 0 && right == layout.width,
				"Declared width covers the full keyboard without empty margins");
			for (size_t index = 1; index < layout.white_keys.size(); ++index) {
				const auto &previous = layout.white_keys[index - 1];
				require(previous.x + previous.w == layout.white_keys[index].x,
					"White keys have no gaps or overlaps");
			}
		}
	}
}

} // namespace

int main()
{
	const std::array<std::pair<const char *, std::function<void()>>, 3> tests = {{
		{"saved range and outline limits", saved_settings_limits},
		{"standard keyboards and black edges", known_layouts},
		{"all 4640 supported MIDI ranges", all_supported_ranges},
	}};
	size_t failures = 0;
	for (const auto &[name, test] : tests) {
		try {
			test();
			std::cout << "PASS: " << name << '\n';
		} catch (const std::exception &error) {
			++failures;
			std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
		}
	}
	std::cout << checks << " checks, " << failures << " failed groups\n";
	return failures ? 1 : 0;
}
