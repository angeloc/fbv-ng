// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "control.hpp"
#include "framebuffer.hpp"
#include "image.hpp"
#include "terminal.hpp"

#include <string>
#include <vector>

namespace fbvng {

struct Options {
	bool clear = true;
	bool alpha = false;
	bool hide_cursor = true;
	bool image_info = true;
	int shrink = 0;  // 1 simple, 2 color average
	bool enlarge = false;
	bool width_only = false;
	bool height_only = false;
	bool ignore_aspect = false;
	int smartfit = -1;
	int delay = 0;  // slideshow delay in tenths of a second
	int orientation = 0;
	bool skip_tty = false;
	bool graphics = false;
	bool paused = false;  // animations start paused on their first frame
	std::string control;  // control socket path, empty for none
};

enum class Navigation { quit, next, previous };

class Viewer {
public:
	Viewer(const Options &options, Framebuffer &fb, Terminal &term, ControlServer *control)
		: opt_(options), fb_(fb), term_(term), control_(control)
	{
	}

	Navigation show(const std::string &path, const std::string &display_name);

private:
	// img at its place on the screen, converted for it.
	Framebuffer::Pixels render(const Image &img, int pan_x, int pan_y, int offs_x, int offs_y);

	const Options &opt_;
	Framebuffer &fb_;
	Terminal &term_;
	ControlServer *control_;
	std::vector<std::uint8_t> background_;
};

} // namespace fbvng
