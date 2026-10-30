// SPDX-License-Identifier: GPL-2.0-or-later
// Feeds truncated and corrupted copies of the given images to the decoders.
// Every input must either decode or throw DecodeError. Run it under the
// sanitizers to catch memory errors.
#include "decode.hpp"

#include <cstdio>
#include <random>

using namespace fbvng;

namespace {

std::size_t failures = 0;

void feed(const std::vector<std::uint8_t> &data)
{
	try {
		auto pic = decode(data);
		for (int i = 0; i < 16 && pic.animation && pic.animation->delay().count(); i++)
			pic.animation->next(pic.image);
	} catch (const DecodeError &) {
	} catch (const std::exception &e) {
		std::fprintf(stderr, "unexpected exception: %s\n", e.what());
		failures++;
	}
}

} // namespace

int main(int argc, char **argv)
{
	std::mt19937 rng(0x0fb7);
	for (int i = 1; i < argc; i++) {
		const auto data = read_file(argv[i]);

		const std::size_t step = std::max<std::size_t>(1, data.size() / 256);
		for (std::size_t len = 0; len < data.size(); len += step)
			feed({data.begin(), data.begin() + static_cast<std::ptrdiff_t>(len)});

		for (int round = 0; round < 500; round++) {
			auto copy = data;
			const int flips = 1 + static_cast<int>(rng() % 8);
			for (int f = 0; f < flips; f++)
				copy[rng() % copy.size()] = static_cast<std::uint8_t>(rng());
			feed(copy);
		}
	}
	return failures ? 1 : 0;
}
