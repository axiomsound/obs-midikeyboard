#include "midi-input.h"
#include <obs-module.h>
#include <algorithm>
#include <cctype>

#ifdef _WIN32
#include <windows.h>
#endif

#include "RtMidi.h"

static std::string str_tolower(const std::string &s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

MidiInput::MidiInput(const std::string &device_name)
    : device_name_(device_name),
      device_name_lower_(str_tolower(device_name))
{
}

MidiInput::~MidiInput()
{
    running_.store(false);
    close();
}

bool MidiInput::open()
{
    if (is_open())
        return true;

    open_attempted_ = true;
    last_reconnect_check_ = std::chrono::steady_clock::now();

    try
    {
        auto *midi_in = new RtMidiIn(RtMidi::UNSPECIFIED, "OBS MIDI Keyboard");
        unsigned int port_count = midi_in->getPortCount();

        for (unsigned int i = 0; i < port_count; ++i)
        {
            std::string port_name = midi_in->getPortName(i);
            if (str_tolower(port_name) == device_name_lower_ ||
                port_name == device_name_)
            {
                midi_in->openPort(i, "OBS MIDI Keyboard");
                midi_in->setCallback(&MidiInput::midi_callback, this);
                midi_in->ignoreTypes(true, true, true);

                midi_in_ = midi_in;
                was_ever_open_ = true;
                blog(LOG_INFO, "[MIDI Keyboard] Opened device: %s", port_name.c_str());
                return true;
            }
        }

        // Not found yet - try partial match
        for (unsigned int i = 0; i < port_count; ++i)
        {
            std::string port_name = str_tolower(midi_in->getPortName(i));
            if (port_name.find(device_name_lower_) != std::string::npos ||
                device_name_lower_.find(port_name) != std::string::npos)
            {
                midi_in->openPort(i, "OBS MIDI Keyboard");
                midi_in->setCallback(&MidiInput::midi_callback, this);
                midi_in->ignoreTypes(true, true, true);

                midi_in_ = midi_in;
                was_ever_open_ = true;
                blog(LOG_INFO, "[MIDI Keyboard] Opened device (partial match): %s",
                     midi_in->getPortName(i).c_str());
                return true;
            }
        }

        delete midi_in;
        blog(LOG_WARNING, "[MIDI Keyboard] Device '%s' not found among %d ports",
             device_name_.c_str(), port_count);
        return false;
    }
    catch (const RtMidiError &e)
    {
        blog(LOG_WARNING, "[MIDI Keyboard] Failed to open device '%s': %s",
             device_name_.c_str(), e.what());
        return false;
    }
}

void MidiInput::close()
{
    if (midi_in_)
    {
        try
        {
            auto *midi_in = static_cast<RtMidiIn *>(midi_in_);
            if (midi_in->isPortOpen())
            {
                midi_in->cancelCallback();
                midi_in->closePort();
            }
            delete midi_in;
        }
        catch (...)
        {
        }
        midi_in_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    active_notes_.clear();
}

bool MidiInput::is_open() const
{
    if (!midi_in_)
        return false;
    try
    {
        return static_cast<RtMidiIn *>(midi_in_)->isPortOpen();
    }
    catch (...)
    {
        return false;
    }
}

void MidiInput::check_reconnect()
{
    auto now = std::chrono::steady_clock::now();

    // Rate-limit reconnect checks to once per second
    if (now - last_reconnect_check_ < std::chrono::seconds(1))
        return;

    last_reconnect_check_ = now;

    if (is_open())
        return;

    // Try to reconnect
    blog(LOG_INFO, "[MIDI Keyboard] Attempting reconnect to '%s'...",
         device_name_.c_str());

    try
    {
        auto *midi_in = new RtMidiIn(RtMidi::UNSPECIFIED, "OBS MIDI Keyboard");
        unsigned int port_count = midi_in->getPortCount();

        std::string target = str_tolower(device_name_);
        for (unsigned int i = 0; i < port_count; ++i)
        {
            std::string port_name = midi_in->getPortName(i);
            std::string port_lower = str_tolower(port_name);

            if (port_lower == target ||
                port_name == device_name_ ||
                port_lower.find(target) != std::string::npos ||
                target.find(port_lower) != std::string::npos)
            {
                midi_in->openPort(i, "OBS MIDI Keyboard");
                midi_in->setCallback(&MidiInput::midi_callback, this);
                midi_in->ignoreTypes(true, true, true);

                // Clean up old state
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    active_notes_.clear();
                }

                if (midi_in_)
                {
                    try
                    {
                        auto *old = static_cast<RtMidiIn *>(midi_in_);
                        old->cancelCallback();
                        old->closePort();
                        delete old;
                    }
                    catch (...)
                    {
                    }
                }

                midi_in_ = midi_in;
                was_ever_open_ = true;
                blog(LOG_INFO, "[MIDI Keyboard] Reconnected to: %s",
                     port_name.c_str());
                return;
            }
        }

        delete midi_in;
    }
    catch (const RtMidiError &e)
    {
        blog(LOG_WARNING, "[MIDI Keyboard] Reconnect attempt failed: %s",
             e.what());
    }
}

std::map<int, uint8_t> MidiInput::get_active_notes() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return active_notes_;
}

void MidiInput::midi_callback(double /*time*/,
                              std::vector<unsigned char> *msg,
                              void *user_data)
{
    if (!msg || msg->empty() || !user_data)
        return;
    auto *self = static_cast<MidiInput *>(user_data);
    if (!self->running_.load())
        return;
    self->handle_message(*msg);
}

void MidiInput::handle_message(const std::vector<unsigned char> &msg)
{
    uint8_t status = msg[0] & 0xF0;
    uint8_t note = (msg.size() > 1) ? msg[1] : 0;
    uint8_t velocity = (msg.size() > 2) ? msg[2] : 0;

    if (status == 0x90 && velocity > 0)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_notes_[note] = velocity;
    }
    else if (status == 0x80 || (status == 0x90 && velocity == 0))
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_notes_.erase(note);
    }

    last_message_time_ = std::chrono::steady_clock::now();
}

MidiManager &MidiManager::instance()
{
    static MidiManager mgr;
    return mgr;
}

std::shared_ptr<MidiInput> MidiManager::get_input(const std::string &device_name)
{
    if (device_name.empty())
        return nullptr;

    std::lock_guard<std::mutex> lock(mutex_);

    auto it = inputs_.find(device_name);
    if (it != inputs_.end())
    {
        auto input = it->second.lock();
        if (input)
            return input;
        inputs_.erase(it);
    }

    auto input = std::make_shared<MidiInput>(device_name);
    inputs_[device_name] = input;
    return input;
}

std::vector<std::string> MidiManager::list_devices()
{
    std::vector<std::string> devices;
    try
    {
        RtMidiIn midi_in(RtMidi::UNSPECIFIED, "OBS MIDI Keyboard Enum");
        unsigned int count = midi_in.getPortCount();
        for (unsigned int i = 0; i < count; ++i)
        {
            devices.push_back(midi_in.getPortName(i));
        }
    }
    catch (const RtMidiError &e)
    {
        blog(LOG_WARNING, "[MIDI Keyboard] Error enumerating devices: %s",
             e.what());
    }
    return devices;
}
