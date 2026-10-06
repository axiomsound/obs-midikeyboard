#pragma once

#include "midi-state.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct ChordCandidate {
	int root = 0;
	int bass = 0;
	std::string_view suffix;
	bool omitted_fifth = false;
	int score = 0;

	bool same_chord(const ChordCandidate &other) const;
};

std::vector<ChordCandidate> detect_chords(std::span<const MidiNote> notes,
					  const std::optional<ChordCandidate> &previous = std::nullopt);
std::string format_chord(const ChordCandidate &chord, bool show_bass = true, bool prefer_flats = false);

class ChordTracker {
public:
	void configure(uint64_t settle_ms, uint64_t clear_ms);
	void reset();
	const std::optional<ChordCandidate> &update(std::span<const MidiNote> notes, uint64_t now_ms);
	const std::optional<ChordCandidate> &current() const { return current_; }

private:
	std::optional<ChordCandidate> current_;
	std::optional<ChordCandidate> pending_;
	uint16_t last_mask_ = 0;
	int last_bass_ = -1;
	bool initialized_ = false;
	bool pending_active_ = false;
	bool clearing_ = false;
	uint64_t settle_ms_ = 70;
	uint64_t clear_ms_ = 180;
	uint64_t pending_since_ = 0;
	uint64_t changed_at_ = 0;
	uint64_t clear_since_ = 0;
};
