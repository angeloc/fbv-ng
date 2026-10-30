// SPDX-License-Identifier: GPL-2.0-or-later
// Reader for the .ref files written by gen_data.py.
#pragma once

#include "decode.hpp"

#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct Reference {
	int width = 0, height = 0;
	std::vector<int> delays;                       // ms per frame
	std::vector<std::vector<std::uint8_t>> frames; // RGBA
};

inline Reference load_reference(const std::string &path)
{
	const auto data = fbvng::read_file(path);
	const std::string text(data.begin(), data.end());
	const auto l1 = text.find('\n');
	const auto l2 = l1 == std::string::npos ? l1 : text.find('\n', l1 + 1);
	if (l2 == std::string::npos)
		throw std::runtime_error(path + ": bad reference");

	Reference ref;
	std::string magic;
	std::size_t count = 0;
	std::istringstream header(text.substr(0, l1));
	header >> magic >> ref.width >> ref.height >> count;
	std::istringstream delays(text.substr(l1 + 1, l2 - l1 - 1));
	for (int d; delays >> d;)
		ref.delays.push_back(d);

	const std::size_t size = static_cast<std::size_t>(ref.width) * static_cast<std::size_t>(ref.height) * 4;
	if (magic != "FBVREF" || ref.delays.size() != count || data.size() != l2 + 1 + count * size)
		throw std::runtime_error(path + ": bad reference");
	for (std::size_t i = 0; i < count; i++) {
		const auto first = data.begin() + static_cast<std::ptrdiff_t>(l2 + 1 + i * size);
		ref.frames.emplace_back(first, first + static_cast<std::ptrdiff_t>(size));
	}
	return ref;
}
