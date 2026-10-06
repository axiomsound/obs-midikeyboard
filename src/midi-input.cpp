#include "midi-input.h"
#include "midi-device.h"
#include "RtMidi.h"

#include <obs-module.h>
#include <algorithm>
#include <chrono>
#include <exception>

MidiInput::MidiInput(const std::string &device_name) : device_name_(device_name)
{
	worker_ = std::thread(&MidiInput::run, this);
}

MidiInput::~MidiInput()
{
	running_.store(false);
	wake_.notify_all();
	if (worker_.joinable())
		worker_.join();
}

void MidiInput::disconnect()
{
	connected_.store(false);
	if (midi_in_) {
		// Stop the backend before removing its callback; never hold the note mutex here.
		try {
			midi_in_->closePort();
			midi_in_->cancelCallback();
		} catch (const std::exception &error) {
			blog(LOG_WARNING, "[MIDI Keyboard] Closing '%s': %s", device_name_.c_str(), error.what());
		}
		midi_in_.reset();
	}
	connected_port_name_.clear();
	std::lock_guard lock(state_mutex_);
	state_.clear();
}

void MidiInput::run()
{
	auto last_warning = std::chrono::steady_clock::time_point{};
	while (running_.load()) {
		try {
			if (!midi_in_) {
				midi_in_ = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "OBS MIDI Keyboard");
				midi_in_->ignoreTypes(true, true, true);
				midi_in_->setCallback(&MidiInput::midi_callback, this);
			}
			std::vector<std::string> ports;
			const auto count = midi_in_->getPortCount();
			ports.reserve(count);
			for (unsigned int port = 0; port < count; ++port)
				ports.push_back(midi_in_->getPortName(port));

			// WinMM's isPortOpen() alone does not detect unplugging a device.
			if (connected_.load() && (!midi_in_->isPortOpen() ||
						  std::count(ports.begin(), ports.end(), connected_port_name_) != 1)) {
				blog(LOG_INFO, "[MIDI Keyboard] Disconnected: %s", device_name_.c_str());
				disconnect();
			}
			if (!connected_.load() && midi_in_) {
				const auto port = select_midi_port(ports, device_name_,
								   midi_in_->getCurrentApi() == RtMidi::WINDOWS_MM);
				if (port) {
					midi_in_->openPort(*port, "OBS MIDI Keyboard");
					if (midi_in_->isPortOpen()) {
						connected_port_name_ = ports[*port];
						connected_.store(true);
						blog(LOG_INFO, "[MIDI Keyboard] Connected: %s",
						     connected_port_name_.c_str());
					}
				}
			}
		} catch (const std::exception &error) {
			const auto now = std::chrono::steady_clock::now();
			if (now - last_warning >= std::chrono::seconds(10)) {
				blog(LOG_WARNING, "[MIDI Keyboard] Device '%s': %s", device_name_.c_str(),
				     error.what());
				last_warning = now;
			}
			disconnect();
		}
		std::unique_lock lock(wait_mutex_);
		wake_.wait_for(lock, std::chrono::seconds(1), [this] { return !running_.load(); });
	}
	disconnect();
}

MidiSnapshot MidiInput::snapshot(int channel, bool include_sustain) const
{
	std::lock_guard lock(state_mutex_);
	return state_.snapshot(channel, include_sustain);
}

std::map<int, uint8_t> MidiInput::get_active_notes() const
{
	std::map<int, uint8_t> result;
	for (const auto &note : snapshot().notes)
		result[note.note] = std::max(result[note.note], note.velocity);
	return result;
}

void MidiInput::midi_callback(double, std::vector<unsigned char> *message, void *user_data) noexcept
{
	if (!message || !user_data)
		return;
	auto *self = static_cast<MidiInput *>(user_data);
	if (!self->running_.load())
		return;
	try {
		const auto now = std::chrono::steady_clock::now().time_since_epoch();
		const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
		std::lock_guard lock(self->state_mutex_);
		self->state_.handle_message(*message, static_cast<uint64_t>(milliseconds));
	} catch (...) {
		// Exceptions must not escape into the native MIDI callback.
	}
}

MidiManager &MidiManager::instance()
{
	static MidiManager manager;
	return manager;
}

std::shared_ptr<MidiInput> MidiManager::get_input(const std::string &device_name)
{
	if (device_name.empty())
		return nullptr;
	std::unique_lock lock(mutex_);
	const auto key = midi_device_key(device_name);
	auto &entry = inputs_[key];
	if (auto input = entry.lock())
		return input;
	// A saved WinMM name can retain an old port index after reconnecting.
	// Resolve new aliases once, outside rendering, and share the existing owner.
	lock.unlock();
	const auto ports = list_devices();
	lock.lock();
	if (auto input = entry.lock())
		return input;
#ifdef _WIN32
	constexpr bool winmm_names = true;
#else
	constexpr bool winmm_names = false;
#endif
	const auto selected = select_midi_port(ports, device_name, winmm_names);
	if (selected) {
		for (const auto &[other_key, other_entry] : inputs_) {
			if (auto input = other_entry.lock();
			    input && select_midi_port(ports, input->device_name(), winmm_names) == selected) {
				entry = input;
				return input;
			}
		}
	}
	try {
		auto input = std::make_shared<MidiInput>(device_name);
		entry = input;
		return input;
	} catch (const std::exception &error) {
		blog(LOG_ERROR, "[MIDI Keyboard] Cannot start '%s': %s", device_name.c_str(), error.what());
		return nullptr;
	}
}

std::vector<std::string> MidiManager::list_devices()
{
	std::vector<std::string> devices;
	try {
		RtMidiIn midi_in(RtMidi::UNSPECIFIED, "OBS MIDI Keyboard Enum");
		const auto count = midi_in.getPortCount();
		for (unsigned int port = 0; port < count; ++port)
			devices.push_back(midi_in.getPortName(port));
	} catch (const std::exception &error) {
		blog(LOG_WARNING, "[MIDI Keyboard] Enumerating devices: %s", error.what());
	}
	return devices;
}
