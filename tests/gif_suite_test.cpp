// SPDX-License-Identifier: GPL-2.0-or-later
// Checks a GIF from gif-test-suite against its expected frames.
// Usage: gif_suite_test <gif> <delays ms, comma separated> <frame.png>...
#include "check.hpp"
#include "decode.hpp"

#include <cstdlib>
#include <sstream>
#include <string>

using namespace fbvng;

int main(int argc, char **argv)
{
	if (argc < 4) {
		std::fprintf(stderr, "usage: %s <gif> <delays> <frame.png>...\n", argv[0]);
		return 2;
	}

	std::vector<long> delays;
	std::istringstream list(argv[2]);
	for (std::string d; std::getline(list, d, ',');)
		delays.push_back(std::stol(d));

	Picture pic = decode_file(argv[1]);
	const int frames = argc - 3;
	CHECK(static_cast<std::size_t>(frames) == delays.size());

	for (int i = 0; i < frames && !check_failures; i++) {
		if (i) {
			CHECK(pic.animation != nullptr);
			if (!pic.animation)
				break;
			pic.animation->next(pic.image);
		}
		const Image expected = decode_file(argv[3 + i]).image;
		const Image &got = pic.image;
		CHECK(got.width == expected.width && got.height == expected.height);
		if (check_failures)
			break;

		std::size_t bad = 0;
		const std::size_t pixels = static_cast<std::size_t>(got.width) * static_cast<std::size_t>(got.height);
		for (std::size_t p = 0; p < pixels; p++) {
			const int a = got.has_alpha() ? got.alpha[p] : 255;
			const int e = expected.has_alpha() ? expected.alpha[p] : 255;
			bool ok = a == e;
			for (std::size_t c = 0; ok && e && c < 3; c++)
				ok = got.rgb[p * 3 + c] == expected.rgb[p * 3 + c];
			bad += !ok;
		}
		if (bad)
			std::fprintf(stderr, "frame %d: %zu of %zu pixels differ\n", i, bad, pixels);
		CHECK(bad == 0);

		const long delay = pic.animation ? static_cast<long>(pic.animation->delay().count()) : 0;
		if (delay != delays[static_cast<std::size_t>(i)])
			std::fprintf(stderr, "frame %d: delay %ld ms, expected %ld\n", i, delay,
				     delays[static_cast<std::size_t>(i)]);
		CHECK(delay == delays[static_cast<std::size_t>(i)]);
	}
	return check_result();
}
