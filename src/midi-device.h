#pragma once

#include <optional>
#include <span>
#include <string>

std::string midi_device_key(const std::string &name);
std::optional<unsigned int> select_midi_port(std::span<const std::string> ports, const std::string &name,
					     bool winmm_port_names = false);
