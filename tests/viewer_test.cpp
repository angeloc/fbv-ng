// SPDX-License-Identifier: GPL-2.0-or-later
// Runs fbv on a fake framebuffer and checks the pixels it left there.
// Usage: viewer_test <fbv> <fakefb.so> <bpp> <image> [fbv options]
#include "check.hpp"
#include "decode.hpp"

#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

using namespace fbvng;

namespace {

constexpr unsigned fb_w = 64, fb_h = 48;

std::uint32_t pack(unsigned bpp, unsigned r, unsigned g, unsigned b)
{
	auto q = [](unsigned v, unsigned bits) { return (v * ((1u << bits) - 1) + 127) / 255; };
	if (bpp == 16)
		return q(r, 5) << 11 | q(g, 6) << 5 | q(b, 5);
	return r << 16 | g << 8 | b;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 5) {
		std::fprintf(stderr, "usage: %s <fbv> <fakefb.so> <bpp> <image> [options]\n", argv[0]);
		return 2;
	}
	const unsigned bpp = static_cast<unsigned>(std::atoi(argv[3]));
	const unsigned cpp = (bpp + 7) / 8, line = fb_w * cpp + 16;

	char path[] = "/tmp/fbv-test-XXXXXX";
	const int fd = ::mkstemp(path);
	if (fd < 0 || ::ftruncate(fd, static_cast<off_t>(line * fb_h)) < 0) {
		std::perror("fake framebuffer");
		return 2;
	}
	// Pattern the screen so pixels fbv should clear are noticed.
	std::vector<std::uint8_t> screen(line * fb_h, 0x5a);
	if (::pwrite(fd, screen.data(), screen.size(), 0) != static_cast<ssize_t>(screen.size()))
		return 2;

	bool alpha = false;
	std::vector<std::string> args = {argv[1], "-y", "-i", "-c", "-u"};
	for (int i = 5; i < argc; i++) {
		args.emplace_back(argv[i]);
		alpha |= !std::strcmp(argv[i], "-a");
	}
	args.emplace_back(argv[4]);

	std::vector<std::string> env;
	for (char **e = environ; *e; e++)
		env.emplace_back(*e);
	env.push_back(std::string("FRAMEBUFFER=") + path);
	env.push_back(std::string("LD_PRELOAD=") + argv[2]);
	env.push_back("FAKEFB_GEOMETRY=" + std::to_string(fb_w) + "," + std::to_string(fb_h) + "," + std::to_string(bpp));
	env.push_back("ASAN_OPTIONS=verify_asan_link_order=0");

	auto ptrs = [](std::vector<std::string> &v) {
		std::vector<char *> p;
		for (auto &s : v)
			p.push_back(s.data());
		p.push_back(nullptr);
		return p;
	};
	auto argp = ptrs(args), envp = ptrs(env);

	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
	pid_t pid;
	int status = 0;
	if (posix_spawn(&pid, argv[1], &actions, nullptr, argp.data(), envp.data()) != 0 ||
	    ::waitpid(pid, &status, 0) != pid) {
		std::perror("spawn fbv");
		return 2;
	}
	posix_spawn_file_actions_destroy(&actions);
	CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	if (::pread(fd, screen.data(), screen.size(), 0) != static_cast<ssize_t>(screen.size()))
		return 2;
	::close(fd);
	::unlink(path);

	// The last frame fbv showed, centered on a cleared screen.
	Picture pic = decode_file(argv[4]);
	while (pic.animation && pic.animation->delay().count())
		pic.animation->next(pic.image);
	const Image &img = pic.image;
	const unsigned ox = (fb_w - static_cast<unsigned>(img.width)) / 2;
	const unsigned oy = (fb_h - static_cast<unsigned>(img.height)) / 2;

	std::size_t bad = 0;
	for (unsigned y = 0; y < fb_h; y++)
		for (unsigned x = 0; x < fb_w; x++) {
			unsigned rgb[3] = {0, 0, 0};
			if (x >= ox && y >= oy && x < ox + static_cast<unsigned>(img.width) &&
			    y < oy + static_cast<unsigned>(img.height)) {
				const std::size_t p = (y - oy) * static_cast<std::size_t>(img.width) + (x - ox);
				const unsigned a = alpha && img.has_alpha() ? img.alpha[p] : 255;
				for (std::size_t c = 0; c < 3; c++)
					rgb[c] = (img.rgb[p * 3 + c] * a + 127) / 255;
			}
			std::uint32_t want = pack(bpp, rgb[0], rgb[1], rgb[2]), got = 0;
			std::memcpy(&got, &screen[y * line + x * cpp], cpp);
			if (cpp == 2)
				got &= 0xffff;
			else if (cpp == 3)
				got &= 0xffffff;
			bad += got != want;
		}
	if (bad)
		std::fprintf(stderr, "%zu of %u framebuffer pixels differ\n", bad, fb_w * fb_h);
	CHECK(bad == 0);
	return check_result();
}
