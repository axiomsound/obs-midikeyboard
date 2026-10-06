#include "chord-detector.h"

#include <algorithm>
#include <array>
#include <bit>
#include <initializer_list>

namespace {

constexpr uint16_t mask(std::initializer_list<int> intervals)
{
	uint16_t result = 0;
	for (int interval : intervals)
		result |= static_cast<uint16_t>(1u << interval);
	return result;
}

struct ChordTemplate {
	std::string_view suffix;
	uint16_t full;
	uint16_t optional = 0;
};

// Only the perfect fifth may be omitted, and only in explicitly listed extended chords.
constexpr auto fifth = mask({7});
constexpr std::array templates = {
	ChordTemplate{"", mask({0, 4, 7})},
	ChordTemplate{"m", mask({0, 3, 7})},
	ChordTemplate{"dim", mask({0, 3, 6})},
	ChordTemplate{"aug", mask({0, 4, 8})},
	ChordTemplate{"sus2", mask({0, 2, 7})},
	ChordTemplate{"sus4", mask({0, 5, 7})},
	ChordTemplate{"7", mask({0, 4, 7, 10}), fifth},
	ChordTemplate{"maj7", mask({0, 4, 7, 11}), fifth},
	ChordTemplate{"m7", mask({0, 3, 7, 10}), fifth},
	ChordTemplate{"mMaj7", mask({0, 3, 7, 11}), fifth},
	ChordTemplate{"dim7", mask({0, 3, 6, 9})},
	ChordTemplate{"m7b5", mask({0, 3, 6, 10})},
	ChordTemplate{"7sus4", mask({0, 5, 7, 10})},
	ChordTemplate{"7sus2", mask({0, 2, 7, 10})},
	ChordTemplate{"7b5", mask({0, 4, 6, 10})},
	ChordTemplate{"7#5", mask({0, 4, 8, 10})},
	ChordTemplate{"maj7#5", mask({0, 4, 8, 11})},
	ChordTemplate{"6", mask({0, 4, 7, 9})},
	ChordTemplate{"m6", mask({0, 3, 7, 9})},
	ChordTemplate{"add9", mask({0, 2, 4, 7})},
	ChordTemplate{"madd9", mask({0, 2, 3, 7})},
	ChordTemplate{"6/9", mask({0, 2, 4, 7, 9})},
	ChordTemplate{"m6/9", mask({0, 2, 3, 7, 9})},
	ChordTemplate{"9", mask({0, 2, 4, 7, 10}), fifth},
	ChordTemplate{"maj9", mask({0, 2, 4, 7, 11}), fifth},
	ChordTemplate{"m9", mask({0, 2, 3, 7, 10}), fifth},
	ChordTemplate{"mMaj9", mask({0, 2, 3, 7, 11}), fifth},
	ChordTemplate{"9sus4", mask({0, 2, 5, 7, 10}), fifth},
	ChordTemplate{"11", mask({0, 2, 4, 5, 7, 10}), fifth},
	ChordTemplate{"m11", mask({0, 2, 3, 5, 7, 10}), fifth},
	ChordTemplate{"13", mask({0, 2, 4, 7, 9, 10}), fifth},
	ChordTemplate{"maj13", mask({0, 2, 4, 7, 9, 11}), fifth},
	ChordTemplate{"m13", mask({0, 2, 3, 7, 9, 10}), fifth},
	ChordTemplate{"7b9", mask({0, 1, 4, 7, 10}), fifth},
	ChordTemplate{"7#9", mask({0, 3, 4, 7, 10}), fifth},
	ChordTemplate{"7#11", mask({0, 4, 6, 7, 10}), fifth},
};

struct NoteSet {
	uint16_t mask = 0;
	int bass = -1;
};

NoteSet note_set(std::span<const MidiNote> notes)
{
	NoteSet result;
	int lowest = 128;
	for (const auto &note : notes) {
		if (note.note < 0 || note.note > 127)
			continue;
		result.mask |= static_cast<uint16_t>(1u << (note.note % 12));
		lowest = std::min(lowest, note.note);
	}
	if (lowest != 128)
		result.bass = lowest % 12;
	return result;
}

} // namespace

bool ChordCandidate::same_chord(const ChordCandidate &other) const
{
	return root == other.root && bass == other.bass && suffix == other.suffix &&
	       omitted_fifth == other.omitted_fifth;
}

std::vector<ChordCandidate> detect_chords(std::span<const MidiNote> notes,
					  const std::optional<ChordCandidate> &previous)
{
	const auto set = note_set(notes);
	std::vector<ChordCandidate> result;
	if (std::popcount(set.mask) < 3)
		return result;
	for (int root = 0; root < 12; ++root) {
		uint16_t relative = 0;
		for (int pitch = 0; pitch < 12; ++pitch) {
			if (set.mask & (1u << pitch))
				relative |= static_cast<uint16_t>(1u << ((pitch - root + 12) % 12));
		}
		for (const auto &type : templates) {
			const auto required = type.full & ~type.optional;
			if ((relative & required) != required || (relative & ~type.full) != 0)
				continue;
			const auto missing = type.full & ~relative;
			int score = 200 - 35 * std::popcount(static_cast<unsigned int>(missing));
			if (root == set.bass)
				score += 10;
			if (previous && previous->root == root && previous->suffix == type.suffix)
				score += 12;
			result.push_back({root, set.bass, type.suffix, missing != 0, score});
		}
	}
	std::stable_sort(result.begin(), result.end(),
			 [](const auto &left, const auto &right) { return left.score > right.score; });
	return result;
}

std::string format_chord(const ChordCandidate &chord, bool show_bass, bool prefer_flats)
{
	constexpr std::array sharp_names = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	constexpr std::array flat_names = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
	if (chord.root < 0 || chord.root > 11 || chord.bass < 0 || chord.bass > 11)
		return {};
	const auto &names = prefer_flats ? flat_names : sharp_names;
	std::string result = names[chord.root];
	result += chord.suffix;
	if (chord.omitted_fifth)
		result += "(no5)";
	if (show_bass && chord.bass != chord.root) {
		result += '/';
		result += names[chord.bass];
	}
	return result;
}

void ChordTracker::configure(uint64_t settle_ms, uint64_t clear_ms)
{
	const auto settle = std::min(settle_ms, uint64_t{500});
	const auto clear = std::min(clear_ms, uint64_t{2000});
	if (settle_ms_ == settle && clear_ms_ == clear)
		return;
	settle_ms_ = settle;
	clear_ms_ = clear;
	reset();
}

void ChordTracker::reset()
{
	current_.reset();
	pending_.reset();
	initialized_ = false;
	pending_active_ = false;
	clearing_ = false;
}

const std::optional<ChordCandidate> &ChordTracker::update(std::span<const MidiNote> notes, uint64_t now_ms)
{
	const auto set = note_set(notes);
	if (!initialized_ || set.mask != last_mask_ || set.bass != last_bass_) {
		initialized_ = true;
		last_mask_ = set.mask;
		last_bass_ = set.bass;
		changed_at_ = now_ms;
		auto candidates = detect_chords(notes, current_);
		if (candidates.empty()) {
			pending_.reset();
			pending_active_ = false;
			if (!clearing_) {
				clearing_ = true;
				clear_since_ = now_ms;
			}
		} else {
			clearing_ = false;
			pending_ = candidates.front();
			if (current_ && current_->same_chord(*pending_)) {
				pending_active_ = false;
			} else if (!pending_active_) {
				pending_active_ = true;
				pending_since_ = now_ms;
			}
		}
	}
	if (clearing_ && now_ms - clear_since_ >= clear_ms_)
		current_.reset();
	if (pending_active_ && (now_ms - changed_at_ >= settle_ms_ ||
				now_ms - pending_since_ >= std::max(uint64_t{160}, settle_ms_ * 2))) {
		current_ = pending_;
		pending_active_ = false;
	}
	return current_;
}
