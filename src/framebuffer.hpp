// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "unique_fd.hpp"

#include <array>
#include <cstdint>
#include <linux/fb.h>
#include <span>
#include <string>
#include <vector>

namespace fbvng {

// Visible page of a Linux framebuffer device. Throws std::system_error or
// std::runtime_error when the device cannot be used.
class Framebuffer {
public:
	explicit Framebuffer(const std::string &device);
	~Framebuffer();
	Framebuffer(const Framebuffer &) = delete;
	Framebuffer &operator=(const Framebuffer &) = delete;

	int width() const { return static_cast<int>(var_.xres); }
	int height() const { return static_cast<int>(var_.yres); }

	void clear();

	// RGB triplets of the w x h rectangle at x, y, clipped to the screen.
	std::vector<std::uint8_t> read(int x, int y, int w, int h) const;
	void write(int x, int y, int w, int h, std::span<const std::uint8_t> rgb);

	// A rectangle of the screen in the pixel format of the screen.
	struct Pixels {
		int x = 0, y = 0, w = 0, h = 0;
		std::vector<std::uint8_t> data;
	};
	// The w x h RGB rectangle at x, y converted for the screen and clipped
	// to it, to put() it later as many times as needed.
	Pixels convert(int x, int y, int w, int h, std::span<const std::uint8_t> rgb) const;
	void put(const Pixels &pixels);

private:
	struct Channel {
		unsigned offset, length;
	};

	std::uint32_t pack(const std::uint8_t *rgb) const;
	void unpack(std::uint32_t pixel, std::uint8_t *rgb) const;
	std::uint8_t *row(int y) const;
	bool clip(int &x, int &y, int &w, int &h) const;
	void unblank();

	UniqueFd fd_;
	fb_var_screeninfo var_{};
	fb_fix_screeninfo fix_{};
	std::uint8_t *map_ = nullptr;
	std::size_t map_size_ = 0;
	unsigned bytes_per_pixel_ = 0;
	Channel red_{}, green_{}, blue_{};
	std::array<std::array<std::uint8_t, 256>, 3> scale_{};
	bool restore_cmap_ = false;
	bool unblanked_ = false;
	std::array<std::array<std::uint16_t, 256>, 3> saved_cmap_{};
};

} // namespace fbvng
