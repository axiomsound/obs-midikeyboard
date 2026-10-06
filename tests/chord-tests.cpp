#include "chord-detector.h"
#include "midi-device.h"
#include "midi-state.h"

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

size_t checks = 0;

void require(bool condition, const std::string &message)
{
	++checks;
	if (!condition)
		throw std::runtime_error(message);
}

std::vector<MidiNote> notes(std::initializer_list<int> pitches)
{
	std::vector<MidiNote> result;
	for (int pitch : pitches)
		result.push_back({pitch, 100, 0, true, 0});
	return result;
}

std::string best(std::span<const MidiNote> input)
{
	const auto result = detect_chords(input);
	return result.empty() ? "" : format_chord(result.front());
}

void send(MidiState &state, std::initializer_list<unsigned char> message, uint64_t time = 0)
{
	state.handle_message(std::span(message.begin(), message.size()), time);
}

void chord_examples()
{
	const std::array examples = {
		std::pair{notes({60, 64, 67}), "C"},
		std::pair{notes({64, 67, 72}), "C/E"},
		std::pair{notes({55, 60, 64}), "C/G"},
		std::pair{notes({57, 60, 64}), "Am"},
		std::pair{notes({60, 63, 66}), "Cdim"},
		std::pair{notes({60, 64, 68}), "Caug"},
		std::pair{notes({60, 62, 67}), "Csus2"},
		std::pair{notes({60, 65, 67}), "Csus4"},
		std::pair{notes({60, 64, 67, 70}), "C7"},
		std::pair{notes({60, 64, 67, 71}), "Cmaj7"},
		std::pair{notes({60, 63, 67, 70}), "Cm7"},
		std::pair{notes({60, 63, 67, 71}), "CmMaj7"},
		std::pair{notes({60, 63, 66, 69}), "Cdim7"},
		std::pair{notes({60, 63, 66, 70}), "Cm7b5"},
		std::pair{notes({60, 64, 67, 69}), "C6"},
		std::pair{notes({60, 63, 67, 69}), "Cm6"},
		std::pair{notes({60, 64, 67, 74}), "Cadd9"},
		std::pair{notes({60, 63, 67, 74}), "Cmadd9"},
		std::pair{notes({60, 64, 67, 69, 74}), "C6/9"},
		std::pair{notes({60, 64, 67, 70, 74}), "C9"},
		std::pair{notes({60, 64, 67, 71, 74}), "Cmaj9"},
		std::pair{notes({60, 63, 67, 70, 74}), "Cm9"},
		std::pair{notes({60, 65, 67, 70, 74}), "C9sus4"},
		std::pair{notes({60, 63, 67, 70, 74, 77}), "Cm11"},
		std::pair{notes({60, 64, 67, 70, 74, 81}), "C13"},
		std::pair{notes({60, 64, 67, 70, 73}), "C7b9"},
		std::pair{notes({60, 64, 67, 70, 75}), "C7#9"},
		std::pair{notes({60, 64, 70}), "C7(no5)"},
		std::pair{notes({60, 64, 71, 74}), "Cmaj9(no5)"},
	};
	for (const auto &[input, expected] : examples)
		require(best(input) == expected, std::string("Expected ") + expected + ", got " + best(input));
	const auto inversion = detect_chords(notes({64, 67, 72})).front();
	require(format_chord(inversion, false) == "C", "Bass display toggle");
	const auto flat = detect_chords(notes({58, 62, 65})).front();
	require(format_chord(flat, true, true) == "Bb", "Flat spelling");
	require(format_chord(flat, true, false) == "A#", "Sharp spelling");
	require(best(notes({60, 64, 67, 72, 76, 79})) == "C", "Octave doubling");
	require(best(notes({79, 64, 72, 60, 67})) == "C", "Input ordering must not change the bass");
}

void transpositions_and_inversions()
{
	const std::array shapes = {notes({48, 52, 55}),        notes({48, 51, 55}),     notes({48, 52, 55, 58}),
				   notes({48, 52, 55, 59}),    notes({48, 51, 55, 58}), notes({48, 51, 54, 58}),
				   notes({48, 52, 55, 58, 62})};
	for (const auto &shape : shapes) {
		const auto original = detect_chords(shape).front();
		for (int transpose = 0; transpose < 12; ++transpose) {
			auto input = shape;
			for (auto &note : input)
				note.note += transpose;
			auto root_position = detect_chords(input).front();
			require(root_position.root == transpose && root_position.suffix == original.suffix,
				"Chord must transpose to every root");
			for (size_t bass_index = 0; bass_index < input.size(); ++bass_index) {
				auto inversion = input;
				for (size_t index = 0; index < bass_index; ++index)
					inversion[index].note += 24;
				const auto result = detect_chords(inversion, root_position);
				require(!result.empty(), "Inversion must be recognized");
				require(result.front().root == root_position.root &&
						result.front().suffix == original.suffix,
					"Context preserves the chord identity in inversions");
				require(result.front().bass == inversion[bass_index].note % 12,
					"Actual lowest note is the bass");
			}
		}
	}
}

void ambiguity_and_conservative_matching()
{
	const auto input = notes({60, 64, 67, 69});
	const auto candidates = detect_chords(input);
	require(best(input) == "C6", "Bass preference for an ambiguous set");
	require(std::any_of(candidates.begin(), candidates.end(),
			    [](const auto &chord) { return format_chord(chord) == "Am7/C"; }),
		"Keep the valid alternate interpretation");
	const auto previous = detect_chords(notes({45, 60, 64, 67})).front();
	require(format_chord(detect_chords(input, previous).front()) == "Am7/C", "Continuity breaks close ties");
	require(detect_chords(notes({60, 64})).empty(), "Do not guess a triad from two notes");
	require(detect_chords(notes({60, 72, 84})).empty(), "Octaves do not count as distinct notes");
	require(detect_chords(notes({60, 61, 62})).empty(), "Unsupported cluster stays unknown");
	const auto extra_pitch = detect_chords(notes({60, 64, 67, 68}));
	require(std::none_of(extra_pitch.begin(), extra_pitch.end(),
			     [](const auto &chord) { return chord.root == 0 && chord.suffix.empty(); }),
		"An extra pitch must not be silently reduced to C major");
	require(detect_chords(notes({-1, 128, 60, 64})).empty(), "Invalid note numbers are ignored");
	const auto rootless = detect_chords(notes({64, 67, 70}));
	require(std::none_of(rootless.begin(), rootless.end(), [](const auto &chord) { return chord.root == 0; }),
		"Do not infer an absent root");
}

void midi_channels_and_validation()
{
	MidiState state;
	send(state, {0x90, 60, 20}, 100);
	send(state, {0x91, 60, 127}, 200);
	send(state, {0x80, 60, 0});
	const auto snapshot = state.snapshot();
	require(snapshot.notes.size() == 1 && snapshot.notes.front().channel == 1, "Note Off is scoped to its channel");
	require(snapshot.notes.front().velocity == 127 && snapshot.notes.front().onset_ms == 200, "Note metadata");
	require(state.snapshot(0).notes.empty() && state.snapshot(1).notes.size() == 1, "Channel filtering");
	send(state, {0x91, 60, 0});
	require(state.snapshot().notes.empty(), "Zero velocity Note On is Note Off");
	const auto revision = state.snapshot().revision;
	for (const auto &message : {std::vector<unsigned char>{},
				    {0x90},
				    {0x90, 60},
				    {0x90, 128, 100},
				    {0x90, 60, 128},
				    {0x90, 60, 90, 0},
				    {0xFF, 0},
				    {0x70, 60, 90},
				    {0xB0, 1, 127}})
		require(!state.handle_message(message, 0), "Malformed and unrelated messages must not alter state");
	require(state.snapshot().revision == revision, "Ignored messages do not alter the revision");
	send(state, {0x90, 60, 100});
	send(state, {0x90, 60, 80});
	send(state, {0x80, 60, 0});
	require(state.snapshot().notes.size() == 1, "Overlapping Note On messages remain active until balanced");
	send(state, {0x80, 60, 0});
	require(state.snapshot().notes.empty(), "Balanced repeated Note Off messages");
	require(state.snapshot(16).notes.empty(), "Invalid channel selection is safe");
}

void sustain_and_resets()
{
	MidiState state;
	send(state, {0xB0, 64, 127});
	send(state, {0x90, 60, 100});
	send(state, {0x91, 64, 100});
	send(state, {0x80, 60, 0});
	send(state, {0x81, 64, 0});
	require(state.snapshot().notes.empty(), "Pressed-only mode excludes pedal notes");
	require(state.snapshot(-1, true).notes.size() == 1, "Sustain is scoped to its channel");
	require(!state.snapshot(0, true).notes.front().pressed, "Pedal notes are distinct from pressed notes");
	send(state, {0x90, 60, 90});
	send(state, {0xB0, 64, 0});
	require(state.snapshot(-1, true).notes.size() == 1, "Pedal release preserves physically held notes");
	send(state, {0x80, 60, 0});
	require(state.snapshot(-1, true).notes.empty(), "Retriggered note releases normally");
	send(state, {0xB0, 64, 64});
	send(state, {0x90, 60, 100});
	send(state, {0x90, 64, 100});
	send(state, {0xB0, 123, 0});
	require(state.snapshot().notes.empty() && state.snapshot(-1, true).notes.size() == 2,
		"All Notes Off releases keys while respecting sustain");
	send(state, {0xB0, 120, 0});
	require(state.snapshot(-1, true).notes.empty(), "All Sound Off clears pedal notes immediately");
	send(state, {0x90, 60, 100});
	send(state, {0x90, 64, 100});
	send(state, {0x80, 60, 0});
	send(state, {0xB0, 121, 0});
	require(state.snapshot(-1, true).notes.size() == 1 && state.snapshot().notes.front().note == 64,
		"Reset Controllers clears sustain while preserving pressed keys");
	send(state, {0x91, 67, 100});
	send(state, {0xFF});
	require(state.snapshot(-1, true).notes.empty(), "System Reset clears all channels");
	send(state, {0x90, 60, 100});
	state.clear();
	require(state.snapshot(-1, true).notes.empty(), "Disconnect reset clears all state");
}

void tracker_timing()
{
	ChordTracker tracker;
	const auto c = notes({60, 64, 67});
	const auto c7 = notes({60, 64, 67, 70});
	require(!tracker.update(c, 0), "Wait for notes to settle");
	require(!tracker.update(c, 69), "Do not publish before the settling deadline");
	require(format_chord(*tracker.update(c, 70)) == "C", "Publish at the settling deadline");
	tracker.configure(70, 180);
	require(tracker.current() && format_chord(*tracker.current()) == "C",
		"Reapplying unchanged timing settings preserves the visible chord");
	require(format_chord(*tracker.update(c7, 100)) == "C", "Hold the old label during a change");
	require(format_chord(*tracker.update(c7, 170)) == "C7", "Publish the new stable chord");
	tracker.update(notes({60, 64}), 200);
	require(tracker.update(notes({60}), 379).has_value(), "Brief incomplete voicings keep the label");
	require(!tracker.update({}, 380), "Continuous invalid notes do not restart the clear deadline");
	tracker.reset();
	tracker.update(c, 0);
	tracker.update(c, 70);
	tracker.update(notes({65, 69, 72}), 100);
	tracker.update(notes({67, 71, 74}), 140);
	tracker.update(notes({57, 60, 64}), 180);
	tracker.update(notes({62, 65, 69}), 220);
	require(format_chord(*tracker.update(notes({64, 67, 71}), 260)) == "Em",
		"Bound the delay during continuous changes");
	tracker.configure(0, 0);
	require(format_chord(*tracker.update(c, 0)) == "C", "Immediate mode");
	require(!tracker.update({}, 1), "Immediate clear mode");
}

void midi_sequence()
{
	MidiState state;
	ChordTracker tracker;
	send(state, {0x90, 60, 100}, 0);
	tracker.update(state.snapshot().notes, 0);
	send(state, {0x90, 64, 80}, 20);
	tracker.update(state.snapshot().notes, 20);
	send(state, {0x90, 67, 90}, 40);
	require(!tracker.update(state.snapshot().notes, 40), "A rolled chord settles after the final structural note");
	require(format_chord(*tracker.update(state.snapshot().notes, 110)) == "C", "Recognize a rolled MIDI chord");
	send(state, {0xB0, 64, 127});
	send(state, {0xB0, 123, 0});
	require(format_chord(*tracker.update(state.snapshot(-1, true).notes, 120)) == "C",
		"Pedal mode sustains the chord");
	send(state, {0xB0, 64, 0});
	tracker.update(state.snapshot(-1, true).notes, 130);
	require(!tracker.update(state.snapshot(-1, true).notes, 310), "Clear after releasing the pedal");
}

void device_matching()
{
	const std::array<std::string, 3> ports = {"Keyboard Pro", "Keyboard", "Drums"};
	require(select_midi_port(ports, "KEYBOARD") == 1u, "Exact match wins over an earlier partial match");
	require(!select_midi_port(ports, "Key"), "Ambiguous partial match must not choose a random device");
	require(select_midi_port(ports, "Drum") == 2u, "Unique partial match");
	require(!select_midi_port(ports, ""), "Empty selection must not open a port");
	const std::array<std::string, 2> duplicates = {"Keyboard", "Keyboard"};
	require(!select_midi_port(duplicates, "Keyboard"), "Duplicate names are ambiguous");
	const std::array<std::string, 2> renumbered = {"Drums 0", "Keyboard 49 1"};
	require(select_midi_port(renumbered, "Keyboard 49 3", true) == 1u,
		"WinMM reconnect tolerates a changed port index while preserving the model number");
	require(!select_midi_port(renumbered, "Keyboard 49 3"), "Other backends retain numeric device names");
	const std::array<std::string, 2> identical_models = {"Keyboard 0", "Keyboard 1"};
	require(select_midi_port(identical_models, "Keyboard 1", true) == 1u,
		"An exact WinMM port selection still wins");
	require(!select_midi_port(identical_models, "Keyboard 2", true),
		"An absent WinMM port must not choose between identical models");
}

void exhaustive_pitch_sets()
{
	for (unsigned int mask = 0; mask < 4096; ++mask) {
		std::vector<MidiNote> input;
		for (int pitch = 0; pitch < 12; ++pitch) {
			if (mask & (1u << pitch))
				input.push_back({60 + pitch, 100, 0, true, 0});
		}
		for (const auto &candidate : detect_chords(input)) {
			require((mask & (1u << candidate.root)) != 0, "Every candidate has an observed root");
			require(candidate.bass == input.front().note % 12, "Candidate bass is the lowest note");
			require(!format_chord(candidate).empty(), "All candidates have a valid label");
		}
	}
}

} // namespace

int main()
{
	const std::array<std::pair<const char *, std::function<void()>>, 9> tests = {{
		{"chord examples and formatting", chord_examples},
		{"all roots and inversions", transpositions_and_inversions},
		{"ambiguity and conservative matching", ambiguity_and_conservative_matching},
		{"MIDI channels and validation", midi_channels_and_validation},
		{"sustain and MIDI resets", sustain_and_resets},
		{"tracker timing and bounded latency", tracker_timing},
		{"rolled MIDI chord and pedal sequence", midi_sequence},
		{"unambiguous device matching", device_matching},
		{"all 4096 pitch-class sets", exhaustive_pitch_sets},
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
