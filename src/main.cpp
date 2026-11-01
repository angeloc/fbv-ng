// SPDX-License-Identifier: GPL-2.0-or-later
#include "framebuffer.hpp"
#include "terminal.hpp"
#include "viewer.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <getopt.h>
#include <string>
#include <vector>

using namespace fbvng;

namespace {

void help(const char *name)
{
	std::printf(
		"Usage: %s [options] image1 image2 image3 ...\n\n"
		"Available options:\n"
		"  -h, --help          Show this help\n"
		"  -a, --alpha         Use the alpha channel (if applicable)\n"
		"  -c, --noclear       Do not clear the screen before and after displaying the image\n"
		"  -u, --unhide        Do not hide the cursor before and after displaying the image\n"
		"  -i, --noinfo        Suppress image information\n"
		"  -f, --shrink        Shrink (using a simple resizing routine) the image to fit onto screen if necessary\n"
		"  -k, --colorshrink   Shrink (using a 'color average' resizing routine) the image to fit onto screen if necessary\n"
		"  -e, --enlarge       Enlarge the image to fit the whole screen if necessary\n"
		"  -l, --widthonly     Fit the image horizontally\n"
		"  -t, --heightonly    Fit the image vertically\n"
		"  -x <percent>, --smartfit <percent>  Show image by covering the whole screen if less than <percent>%% is out of screen\n"
		"  -r, --ignore-aspect Ignore the image aspect while resizing\n"
		"  -s <delay>, --delay <d>  Slideshow, 'delay' is the slideshow delay in tenths of seconds.\n"
		"  -n imagename(s)     Image name(s) shown in help, separated by '^'\n"
		"  -o <mode>, --orientation <mode>  Show image in specific orientation (0 = no rotation, 1 = 90 degrees, 2 = 180 degrees, 3 = 270 degrees)\n"
		"  -y, --skiptty       Shows the image only once and skips tty input mode.\n"
		"  -g, --graphics      Switch the console to graphics mode while running\n"
		"Input keys:\n"
		" r          : Redraw the image\n"
		" < or ,     : Previous image\n"
		" > or .     : Next image\n"
		" a, d, w, x : Pan the image\n"
		" f          : Toggle resizing on/off\n"
		" k          : Toggle resizing quality\n"
		" e          : Toggle enlarging on/off\n"
		" l          : Toggle fitting the image horizontally\n"
		" t          : Toggle fitting the image vertically\n"
		" i          : Toggle respecting the image aspect on/off\n"
		" +, -, 0    : Increase, decrease and reset zoom\n"
		" n          : Rotate the image 90 degrees left\n"
		" m          : Rotate the image 90 degrees right\n"
		" p          : Disable all transformations\n"
		" h          : Help and image information\n"
		" Copyright (C) 2000 - 2004 Mateusz Golicz, Tomasz Sterna.\n"
		" Copyright (C) 2013 yanlin, godspeed1989@gitbub\n"
		" Copyright (C) 2026 Angelo Compagnucci\n", name);
}

bool parse_int(const char *s, int min, int max, int &out)
{
	char *end;
	errno = 0;
	const long v = std::strtol(s, &end, 10);
	if (errno || end == s || *end || v < min || v > max)
		return false;
	out = static_cast<int>(v);
	return true;
}

std::vector<std::string> split_names(const char *names)
{
	std::vector<std::string> out;
	if (!names)
		return out;
	std::string s(names);
	for (std::size_t pos; (pos = s.find('^')) != std::string::npos; s.erase(0, pos + 1))
		out.push_back(s.substr(0, pos));
	out.push_back(s);
	return out;
}

} // namespace

int main(int argc, char **argv)
{
	static const option long_options[] = {
		{"help", no_argument, nullptr, 'h'},
		{"noclear", no_argument, nullptr, 'c'},
		{"alpha", no_argument, nullptr, 'a'},
		{"unhide", no_argument, nullptr, 'u'},
		{"noinfo", no_argument, nullptr, 'i'},
		{"shrink", no_argument, nullptr, 'f'},
		{"colorshrink", no_argument, nullptr, 'k'},
		{"delay", required_argument, nullptr, 's'},
		{"enlarge", no_argument, nullptr, 'e'},
		{"widthonly", no_argument, nullptr, 'l'},
		{"heightonly", no_argument, nullptr, 't'},
		{"smartfit", required_argument, nullptr, 'x'},
		{"ignore-aspect", no_argument, nullptr, 'r'},
		{"orientation", required_argument, nullptr, 'o'},
		{"skiptty", no_argument, nullptr, 'y'},
		{"imagename", required_argument, nullptr, 'n'},
		{"graphics", no_argument, nullptr, 'g'},
		{nullptr, 0, nullptr, 0},
	};

	if (argc < 2) {
		help(argv[0]);
		std::fprintf(stderr, "Error: Required argument missing.\n");
		return 1;
	}

	Options opt;
	const char *names = nullptr;
	int c;
	while ((c = getopt_long_only(argc, argv, "hn:cauifks:eltx:ro:yg", long_options, nullptr)) != -1) {
		bool ok = true;
		switch (c) {
		case 'a': opt.alpha = true; break;
		case 'c': opt.clear = false; break;
		case 's': ok = parse_int(optarg, 0, 864000, opt.delay); break;
		case 'u': opt.hide_cursor = false; break;
		case 'n': names = optarg; break;
		case 'h': help(argv[0]); return 0;
		case 'i': opt.image_info = false; break;
		case 'f': opt.shrink = 1; break;
		case 'k': opt.shrink = 2; break;
		case 'e': opt.enlarge = true; break;
		case 'l': opt.width_only = true; break;
		case 't': opt.height_only = true; break;
		case 'x': ok = parse_int(optarg, 0, 100, opt.smartfit); break;
		case 'r': opt.ignore_aspect = true; break;
		case 'o': ok = parse_int(optarg, 0, 3, opt.orientation); break;
		case 'y': opt.skip_tty = true; break;
		case 'g': opt.graphics = true; break;
		default: return 1;
		}
		if (!ok) {
			std::fprintf(stderr, "Invalid value for -%c: %s\n", c, optarg);
			return 1;
		}
	}

	if (optind >= argc) {
		std::fprintf(stderr, "Required argument missing! Consult %s -h.\n", argv[0]);
		return 1;
	}

	const std::vector<std::string> files(argv + optind, argv + argc);
	const auto display_names = split_names(names);

	const char *device = std::getenv("FRAMEBUFFER");
	try {
		Framebuffer fb(device ? device : "/dev/fb0");
		Terminal term(opt.hide_cursor, opt.graphics);
		Viewer viewer(opt, fb, term);

		for (std::size_t i = 0; i < files.size();) {
			const std::string &name = i < display_names.size() ? display_names[i] : files[i];
			const Navigation nav = viewer.show(files[i], name);
			if (opt.clear) {
				std::printf("\033[H\033[J");
				std::fflush(stdout);
			}
			if (nav == Navigation::quit)
				break;
			if (nav == Navigation::previous)
				i = i ? i - 1 : 0;
			else
				i++;
		}
	} catch (const std::exception &e) {
		std::fprintf(stderr, "fbv: %s\n", e.what());
		return 1;
	}
	return quit_requested ? 128 + quit_requested : 0;
}
