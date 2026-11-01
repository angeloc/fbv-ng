// SPDX-License-Identifier: GPL-2.0-or-later
// Fake framebuffer for running fbv in tests, see fakefb.cpp.
#pragma once

#include "image.hpp"

#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

class FakeFramebuffer {
public:
	static constexpr unsigned width = 64, height = 48;

	explicit FakeFramebuffer(unsigned bpp) : bpp_(bpp), cpp_((bpp + 7) / 8), line_(width * cpp_ + 16)
	{
		char path[] = "/tmp/fbv-test-XXXXXX";
		const int fd = ::mkstemp(path);
		if (fd < 0)
			throw std::runtime_error("mkstemp failed");
		path_ = path;
		// Pattern the screen so pixels fbv should clear are noticed.
		const std::vector<std::uint8_t> screen(line_ * height, 0x5a);
		const bool ok = ::write(fd, screen.data(), screen.size()) == static_cast<ssize_t>(screen.size());
		::close(fd);
		if (!ok)
			throw std::runtime_error("cannot fill the fake framebuffer");
	}
	~FakeFramebuffer() { ::unlink(path_.c_str()); }
	FakeFramebuffer(const FakeFramebuffer &) = delete;
	FakeFramebuffer &operator=(const FakeFramebuffer &) = delete;

	// Environment for an fbv drawing on this framebuffer through the shim.
	std::vector<std::string> environment(const std::string &shim) const
	{
		std::vector<std::string> env;
		for (char **e = environ; *e; e++)
			env.emplace_back(*e);
		env.push_back("FRAMEBUFFER=" + path_);
		env.push_back("LD_PRELOAD=" + shim);
		env.push_back("FAKEFB_GEOMETRY=" + std::to_string(width) + "," + std::to_string(height) + "," +
			      std::to_string(bpp_));
		env.push_back("ASAN_OPTIONS=verify_asan_link_order=0");
		return env;
	}

	// Pixels differing from img centered on a cleared screen, blended with
	// its alpha when blend is set.
	std::size_t diff(const fbvng::Image &img, bool blend) const
	{
		std::vector<std::uint8_t> screen(line_ * height);
		const int fd = ::open(path_.c_str(), O_RDONLY);
		const bool ok = fd >= 0 && ::read(fd, screen.data(), screen.size()) == static_cast<ssize_t>(screen.size());
		if (fd >= 0)
			::close(fd);
		if (!ok)
			throw std::runtime_error("cannot read the fake framebuffer");

		const unsigned ox = (width - static_cast<unsigned>(img.width)) / 2;
		const unsigned oy = (height - static_cast<unsigned>(img.height)) / 2;
		std::size_t bad = 0;
		for (unsigned y = 0; y < height; y++)
			for (unsigned x = 0; x < width; x++) {
				unsigned rgb[3] = {0, 0, 0};
				if (x >= ox && y >= oy && x < ox + static_cast<unsigned>(img.width) &&
				    y < oy + static_cast<unsigned>(img.height)) {
					const std::size_t p = (y - oy) * static_cast<std::size_t>(img.width) + (x - ox);
					const unsigned a = blend && img.has_alpha() ? img.alpha[p] : 255;
					for (std::size_t c = 0; c < 3; c++)
						rgb[c] = (img.rgb[p * 3 + c] * a + 127) / 255;
				}
				std::uint32_t got = 0;
				std::memcpy(&got, &screen[y * line_ + x * cpp_], cpp_);
				if (cpp_ < 4)
					got &= (1u << (8 * cpp_)) - 1;
				bad += got != pack(rgb[0], rgb[1], rgb[2]);
			}
		return bad;
	}

private:
	std::uint32_t pack(unsigned r, unsigned g, unsigned b) const
	{
		auto q = [](unsigned v, unsigned bits) { return (v * ((1u << bits) - 1) + 127) / 255; };
		if (bpp_ == 16)
			return q(r, 5) << 11 | q(g, 6) << 5 | q(b, 5);
		return r << 16 | g << 8 | b;
	}

	unsigned bpp_, cpp_, line_;
	std::string path_;
};

// Starts a program with stdin on /dev/null, returns its pid.
inline pid_t spawn(std::vector<std::string> args, std::vector<std::string> env)
{
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
	const int rc = posix_spawn(&pid, argp[0], &actions, nullptr, argp.data(), envp.data());
	posix_spawn_file_actions_destroy(&actions);
	if (rc != 0)
		throw std::runtime_error("cannot start " + args[0]);
	return pid;
}

// Exit status of pid, -1 when it did not exit normally.
inline int wait_exit(pid_t pid)
{
	int status = 0;
	if (::waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return -1;
	return WEXITSTATUS(status);
}
