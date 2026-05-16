#pragma once
#include <obs-module.h>
#include <memory>
#include <vector>
#include <string>

class MidiInput;

struct KeyRect
{
    float x, y, w, h;
    int midi_note;
};

struct PianoSourceData
{
    obs_source_t *source = nullptr;
    std::shared_ptr<MidiInput> midi_input;

    std::string midi_device_name;
    int num_keys = 49;
    int first_note = 36;

    uint32_t white_key_color = 0xFFFFFFFF;
    uint32_t black_key_color = 0xFF1A1A1A;
    uint32_t active_note_color = 0xFFFF9933;
    uint32_t outline_color = 0xFF000000;
    bool velocity_sensitive = true;
    bool show_outline = true;
    int outline_width = 1;

    int last_num_keys = 0;
    int last_first_note = 0;
    std::vector<KeyRect> white_keys;
    std::vector<KeyRect> black_keys;

    float white_key_w = 24.0f;
    float white_key_h = 120.0f;
    float black_key_w = 14.0f;
    float black_key_h = 72.0f;

    uint32_t width = 696;
    uint32_t height = 120;

    void calculate_layout();
};
