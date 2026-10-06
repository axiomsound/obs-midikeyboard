#include "midi-device.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace {

std::string_view without_port_number(std::string_view name)
{
	const auto separator = name.find_last_of(' ');
	if (separator == std::string_view::npos || separator == 0 || separator + 1 == name.size())
		return name;
	for (const char digit : name.substr(separator + 1)) {
		if (digit < '0' || digit > '9')
			return name;
	}
	return name.substr(0, separator);
}

} // namespace

std::string midi_device_key(const std::string &name)
{
	std::string result = name;
	std::transform(result.begin(), result.end(), result.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}

std::optional<unsigned int> select_midi_port(std::span<const std::string> ports, const std::string &name,
					     bool winmm_port_names)
{
	if (name.empty())
		return std::nullopt;
	const auto target = midi_device_key(name);
	for (int pass = 0; pass < 3; ++pass) {
		if (pass == 1 && !winmm_port_names)
			continue;
		std::optional<unsigned int> match;
		for (unsigned int index = 0; index < ports.size(); ++index) {
			const auto port = midi_device_key(ports[index]);
			// RtMidi's WinMM names end in a volatile port number. Compare the full
			// device name without that suffix before attempting a partial match.
			const bool matches = pass == 0   ? port == target
					     : pass == 1 ? without_port_number(port) == without_port_number(target)
							 : !port.empty() && (port.find(target) != std::string::npos ||
									     target.find(port) != std::string::npos);
			if (!matches)
				continue;
			if (match)
				return std::nullopt;
			match = index;
		}
		if (match)
			return match;
	}
	return std::nullopt;
}
