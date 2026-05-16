#pragma once
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include <vector>
#include <atomic>

class MidiInput : public std::enable_shared_from_this<MidiInput>
{
public:
    MidiInput(const std::string &device_name);
    ~MidiInput();

    bool open();
    void close();
    bool is_open() const;
    void check_reconnect();

    std::map<int, uint8_t> get_active_notes() const;
    const std::string &device_name() const { return device_name_; }
    bool was_ever_open() const { return was_ever_open_; }

private:
    static void midi_callback(double time, std::vector<unsigned char> *msg,
                              void *user_data);
    void handle_message(const std::vector<unsigned char> &msg);

    std::string device_name_;
    std::string device_name_lower_;

    void *midi_in_ = nullptr;

    mutable std::mutex mutex_;
    std::map<int, uint8_t> active_notes_;
    std::chrono::steady_clock::time_point last_message_time_;
    std::chrono::steady_clock::time_point last_reconnect_check_;
    bool was_ever_open_ = false;
    bool open_attempted_ = false;
    std::atomic<bool> running_{true};
};

class MidiManager
{
public:
    static MidiManager &instance();

    std::shared_ptr<MidiInput> get_input(const std::string &device_name);
    std::vector<std::string> list_devices();

private:
    MidiManager() = default;
    ~MidiManager() = default;
    MidiManager(const MidiManager &) = delete;
    MidiManager &operator=(const MidiManager &) = delete;

    std::mutex mutex_;
    std::map<std::string, std::weak_ptr<MidiInput>> inputs_;
};
