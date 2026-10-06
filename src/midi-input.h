#pragma once

#include "midi-state.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rt::midi {
class RtMidiIn;
}

class MidiInput {
public:
	explicit MidiInput(const std::string &device_name);
	~MidiInput();

	bool is_open() const { return connected_.load(); }
	MidiSnapshot snapshot(int channel = -1, bool include_sustain = false) const;
	std::map<int, uint8_t> get_active_notes() const;
	const std::string &device_name() const { return device_name_; }

private:
	static void midi_callback(double time, std::vector<unsigned char> *msg, void *user_data) noexcept;
	void run();
	void disconnect();

	std::string device_name_;
	std::string connected_port_name_;
	// Only the worker accesses the RtMidi object and its port lifecycle.
	std::unique_ptr<rt::midi::RtMidiIn> midi_in_;
	mutable std::mutex state_mutex_;
	MidiState state_;
	std::atomic<bool> connected_{false};
	std::atomic<bool> running_{true};
	std::mutex wait_mutex_;
	std::condition_variable wake_;
	std::thread worker_;
};

class MidiManager {
public:
	static MidiManager &instance();
	std::shared_ptr<MidiInput> get_input(const std::string &device_name);
	std::vector<std::string> list_devices();

private:
	MidiManager() = default;
	MidiManager(const MidiManager &) = delete;
	MidiManager &operator=(const MidiManager &) = delete;

	std::mutex mutex_;
	std::map<std::string, std::weak_ptr<MidiInput>> inputs_;
};
