#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

struct MidiNote {
	int note = 0;
	uint8_t velocity = 0;
	int channel = 0;
	bool pressed = false;
	uint64_t onset_ms = 0;
};

struct MidiSnapshot {
	std::vector<MidiNote> notes;
	uint64_t revision = 0;
};

// The owner supplies synchronization. This state machine has no OBS or backend dependencies.
class MidiState {
public:
	bool handle_message(std::span<const unsigned char> message, uint64_t now_ms);
	void clear();
	MidiSnapshot snapshot(int channel = -1, bool include_sustain = false) const;

private:
	struct NoteState {
		uint16_t presses = 0;
		uint8_t velocity = 0;
		bool sustained = false;
		uint64_t onset_ms = 0;
	};
	std::array<std::array<NoteState, 128>, 16> notes_{};
	std::array<bool, 16> sustain_{};
	uint64_t revision_ = 0;
};
