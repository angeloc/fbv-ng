// SPDX-License-Identifier: GPL-2.0-or-later
// Runs fbv on a fake framebuffer and checks the pixels it left there.
// Usage: viewer_test <fbv> <fakefb.so> <bpp> <image> [fbv options]
#include "check.hpp"
#include "decode.hpp"
#include "fake_fb.hpp"

using namespace fbvng;

int main(int argc, char **argv)
{
	if (argc < 5) {
		std::fprintf(stderr, "usage: %s <fbv> <fakefb.so> <bpp> <image> [options]\n", argv[0]);
		return 2;
	}
	FakeFramebuffer fb(static_cast<unsigned>(std::atoi(argv[3])));

	bool alpha = false;
	std::vector<std::string> args = {argv[1], "-y", "-i", "-c", "-u"};
	for (int i = 5; i < argc; i++) {
		args.emplace_back(argv[i]);
		alpha |= !std::strcmp(argv[i], "-a");
	}
	args.emplace_back(argv[4]);
	CHECK(wait_exit(spawn(args, fb.environment(argv[2]))) == 0);

	// The last frame fbv showed
	Picture pic = decode_file(argv[4]);
	while (pic.animation && pic.animation->delay().count())
		pic.animation->next(pic.image);
	const std::size_t bad = fb.diff(pic.image, alpha);
	if (bad)
		std::fprintf(stderr, "%zu of %u framebuffer pixels differ\n", bad,
			     FakeFramebuffer::width * FakeFramebuffer::height);
	CHECK(bad == 0);
	return check_result();
}
