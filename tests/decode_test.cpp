// SPDX-License-Identifier: GPL-2.0-or-later
// Decodes an image and compares every frame and delay with its reference.
// Usage: decode_test <image> <reference> [tolerance]
#include "check.hpp"
#include "decode.hpp"
#include "reference.hpp"

#include <cstdlib>

using namespace fbvng;

namespace {

void compare(const Image &img, const std::vector<std::uint8_t> &rgba, int tolerance, std::size_t frame)
{
	const std::size_t pixels = static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height);
	std::size_t bad = 0;
	for (std::size_t p = 0; p < pixels; p++) {
		const int alpha = img.has_alpha() ? img.alpha[p] : 255;
		const int ref_alpha = rgba[p * 4 + 3];
		bool ok = std::abs(alpha - ref_alpha) <= tolerance;
		// Colors under fully transparent pixels are not defined.
		if (ok && ref_alpha)
			for (std::size_t c = 0; c < 3; c++)
				ok = ok && std::abs(img.rgb[p * 3 + c] - rgba[p * 4 + c]) <= tolerance;
		bad += !ok;
	}
	if (bad)
		std::fprintf(stderr, "frame %zu: %zu of %zu pixels differ\n", frame, bad, pixels);
	CHECK(bad == 0);
}

} // namespace

int run(int argc, char **argv)
{
	if (argc < 3) {
		std::fprintf(stderr, "usage: %s <image> <reference> [tolerance]\n", argv[0]);
		return 2;
	}
	const int tolerance = argc > 3 ? std::atoi(argv[3]) : 0;
	const Reference ref = load_reference(argv[2]);
	Picture pic = decode_file(argv[1]);

	CHECK(pic.image.width == ref.width);
	CHECK(pic.image.height == ref.height);
	if (check_failures)
		return check_result();

	for (std::size_t i = 0; i < ref.frames.size(); i++) {
		if (i) {
			CHECK(pic.animation != nullptr);
			if (!pic.animation)
				break;
			pic.animation->next(pic.image);
		}
		compare(pic.image, ref.frames[i], tolerance, i);
		const long delay = pic.animation ? static_cast<long>(pic.animation->delay().count()) : 0;
		if (delay != ref.delays[i])
			std::fprintf(stderr, "frame %zu: delay %ld ms, expected %d\n", i, delay, ref.delays[i]);
		CHECK(delay == ref.delays[i]);
	}

	// A frame after skipped ones, also past the end of a loop, is rebuilt.
	const std::size_t n = ref.frames.size();
	for (std::size_t i = 1; n > 1 && i <= n; i++) {
		Picture again = decode_file(argv[1]);
		for (std::size_t j = 0; j + 1 < i; j++)
			again.animation->skip();
		again.animation->next(again.image);
		compare(again.image, ref.frames[i % n], tolerance, i % n);
	}
	return check_result();
}

int main(int argc, char **argv)
{
	try {
		return run(argc, argv);
	} catch (const std::exception &e) {
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
