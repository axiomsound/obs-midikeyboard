#include "midi-state.h"

#include <algorithm>
#include <limits>

bool MidiState::handle_message(std::span<const unsigned char> message, uint64_t now_ms)
{
	if (message.empty())
		return false;
	if (message[0] == 0xFF) {
		if (message.size() != 1)
			return false;
		clear();
		return true;
	}
	const auto status = message[0] & 0xF0;
	if ((status != 0x80 && status != 0x90 && status != 0xB0) || message.size() != 3 || message[1] > 127 ||
	    message[2] > 127)
		return false;
	const auto channel = message[0] & 0x0F;
	auto &notes = notes_[channel];
	auto &note = notes[message[1]];
	if (status == 0x90 && message[2] != 0) {
		if (note.presses < std::numeric_limits<uint16_t>::max())
			++note.presses;
		note.velocity = message[2];
		note.sustained = false;
		note.onset_ms = now_ms;
	} else if (status == 0x80 || status == 0x90) {
		if (!note.presses)
			return false;
		if (--note.presses == 0)
			note.sustained = sustain_[channel];
	} else if (message[1] == 64) {
		const bool down = message[2] >= 64;
		if (down == sustain_[channel])
			return false;
		sustain_[channel] = down;
		if (!down) {
			for (auto &entry : notes)
				entry.sustained = false;
		}
	} else if (message[1] == 120) {
		notes = {};
	} else if (message[1] == 123) {
		for (auto &entry : notes) {
			entry.sustained = sustain_[channel] && (entry.presses || entry.sustained);
			entry.presses = 0;
		}
	} else if (message[1] == 121) {
		sustain_[channel] = false;
		for (auto &entry : notes)
			entry.sustained = false;
	} else {
		return false;
	}
	++revision_;
	return true;
}

void MidiState::clear()
{
	notes_ = {};
	sustain_ = {};
	++revision_;
}

MidiSnapshot MidiState::snapshot(int channel, bool include_sustain) const
{
	MidiSnapshot result;
	result.revision = revision_;
	if (channel < -1 || channel > 15)
		return result;
	for (int ch = 0; ch < 16; ++ch) {
		if (channel != -1 && channel != ch)
			continue;
		for (int pitch = 0; pitch < 128; ++pitch) {
			const auto &note = notes_[ch][pitch];
			if (note.presses || (include_sustain && note.sustained))
				result.notes.push_back({pitch, note.velocity, ch, note.presses != 0, note.onset_ms});
		}
	}
	return result;
}
