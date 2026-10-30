// SPDX-License-Identifier: GPL-2.0-or-later
#include "viewer.hpp"
#include "decode.hpp"
#include "transform.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <unistd.h>

namespace fbvng {

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;

constexpr int pan_stepping = 20;

struct View {
	bool shrink, cal, enlarge, ignore_aspect, width_only, height_only;
	int rotation;
	double zoom = 1;
};

void fit_to_screen(Image &img, std::int64_t sw, std::int64_t sh, bool ignore_aspect,
		   bool width_only, bool height_only, bool cal)
{
	if (img.width <= sw && img.height <= sh)
		return;

	std::int64_t w = img.width, h = img.height;
	if (ignore_aspect) {
		w = std::min(w, sw);
		h = std::min(h, sh);
	} else if (width_only) {
		w = sw;
		h = img.height * sw / img.width;
	} else if (height_only) {
		w = img.width * sh / img.height;
		h = sh;
	} else if (img.height * sw / img.width <= sh) {
		w = sw;
		h = img.height * sw / img.width;
	} else {
		w = img.width * sh / img.height;
		h = sh;
	}
	w = std::max<std::int64_t>(w, 1);
	h = std::max<std::int64_t>(h, 1);
	img = cal ? resize_average(img, static_cast<int>(w), static_cast<int>(h)) :
		    resize_nearest(img, static_cast<int>(w), static_cast<int>(h));
}

void enlarge(Image &img, std::int64_t sw, std::int64_t sh, bool ignore_aspect,
	     bool width_only, bool height_only)
{
	if ((img.width > sw || img.height > sh) && !ignore_aspect)
		return;
	if (img.width >= sw && img.height >= sh)
		return;

	std::int64_t w = img.width, h = img.height;
	if (ignore_aspect) {
		w = std::max(w, sw);
		h = std::max(h, sh);
	} else if (width_only) {
		w = sw;
		h = img.height * sw / img.width;
	} else if (height_only) {
		w = img.width * sh / img.height;
		h = sh;
	} else if (img.height * sw / img.width <= sh) {
		w = sw;
		h = img.height * sw / img.width;
	} else if (img.width * sh / img.height <= sw) {
		w = img.width * sh / img.height;
		h = sh;
	} else {
		return;
	}
	img = resize_nearest(img, static_cast<int>(std::max<std::int64_t>(w, 1)),
			     static_cast<int>(std::max<std::int64_t>(h, 1)));
}

Image transform(const Image &src, const View &v, int sw, int sh)
{
	Image img = rotate(src, v.rotation);
	const auto zw = static_cast<std::int64_t>(src.width / v.zoom);
	const auto zh = static_cast<std::int64_t>(src.height / v.zoom);
	if (v.zoom > 1)
		fit_to_screen(img, std::max<std::int64_t>(zw, 1), std::max<std::int64_t>(zh, 1),
			      false, false, false, false);
	if (v.zoom < 1)
		enlarge(img, zw, zh, false, false, false);
	if (v.shrink)
		fit_to_screen(img, sw, sh, v.ignore_aspect, v.width_only, v.height_only, v.cal);
	if (v.enlarge)
		enlarge(img, sw, sh, v.ignore_aspect, v.width_only, v.height_only);
	return img;
}

constexpr char status_fmt[] =
	"\nviewer status:\n"
	"%cshrink(f)%c          %cquality shrin(k)%c    %c(e)nlarge%c\n"
	"%chorizonta(l)_fit%c   %cver(t)ical_fit%c      %caspect(i)%c\n"
	" zoom:%g\n";

constexpr char inline_help[] =
	"keys:\n"
	"r\t\tRedraw the image\n"
	"< or ,\t\tPrevious image\n"
	"> or .\t\tNext image\n"
	"a, d, w, x\tScroll the image (cursor keys also do that)\n"
	"f\t\tToggle shrinking on/off\n"
	"k\t\tToggle shrinking quality\n"
	"e\t\tToggle enlarging on/off\n"
	"l\t\tToggle fitting the image horizontally\n"
	"t\t\tToggle fitting the image vertically\n"
	"i\t\tToggle respecting the image aspect on/off\n"
	"+, -, 0\t\tIncrease, decrease and reset zoom\n"
	"n\t\tRotate the image 90 degrees left\n"
	"m\t\tRotate the image 90 degrees right\n"
	"p\t\tDisable all transformations\n"
	"h\t\tHelp and image information\n";

char opener(bool on) { return on ? '[' : ' '; }
char closer(bool on) { return on ? ']' : ' '; }

void print_info(const std::string &name, const Image &src, const View &v)
{
	std::printf("fbv - The Framebuffer Viewer\n%s\n%d x %d\n", name.c_str(), src.width, src.height);
	std::printf(status_fmt,
		    opener(v.shrink), closer(v.shrink), opener(v.cal), closer(v.cal),
		    opener(v.enlarge), closer(v.enlarge), opener(v.width_only), closer(v.width_only),
		    opener(v.height_only), closer(v.height_only), opener(v.ignore_aspect),
		    closer(v.ignore_aspect), 1 / v.zoom);
	if (::isatty(STDOUT_FILENO))
		std::printf("\n%s", inline_help);
}

void clear_console()
{
	std::printf("\033[H\033[J");
}

} // namespace

void Viewer::draw(const Image &img, int pan_x, int pan_y, int offs_x, int offs_y)
{
	const int vw = std::min(img.width, fb_.width());
	const int vh = std::min(img.height, fb_.height());
	const bool blend = img.has_alpha() &&
			   background_.size() == static_cast<std::size_t>(vw) * static_cast<std::size_t>(vh) * 3;

	std::vector<std::uint8_t> out(static_cast<std::size_t>(vw) * static_cast<std::size_t>(vh) * 3);
	for (int y = 0; y < vh; y++)
		for (int x = 0; x < vw; x++) {
			const auto s = static_cast<std::size_t>(pan_y + y) * static_cast<std::size_t>(img.width) +
				       static_cast<std::size_t>(pan_x + x);
			const auto d = static_cast<std::size_t>(y) * static_cast<std::size_t>(vw) +
				       static_cast<std::size_t>(x);
			for (std::size_t c = 0; c < 3; c++) {
				unsigned v = img.rgb[s * 3 + c];
				if (blend) {
					const unsigned a = img.alpha[s];
					v = (v * a + background_[d * 3 + c] * (255 - a) + 127) / 255;
				}
				out[d * 3 + c] = static_cast<std::uint8_t>(v);
			}
		}
	fb_.write(offs_x, offs_y, vw, vh, out);
}

Navigation Viewer::show(const std::string &path, const std::string &name)
{
	Picture pic;
	try {
		pic = decode_file(path);
	} catch (const DecodeError &e) {
		std::fprintf(stderr, "%s: %s\n", path.c_str(), e.what());
		return Navigation::next;
	}

	Image frame = std::move(pic.image);
	if (!opt_.alpha)
		frame.alpha.clear();

	const int sw = fb_.width(), sh = fb_.height();
	View v{opt_.shrink != 0, opt_.shrink == 2, opt_.enlarge, opt_.ignore_aspect,
	       opt_.width_only, opt_.height_only, opt_.orientation};

	if (opt_.smartfit >= 0) {
		const std::int64_t xs = frame.width, ys = frame.height;
		v.shrink = v.enlarge = true;
		if (std::int64_t{sw} * ys > xs * sh) {
			if (opt_.smartfit > 100 - 100 * xs * sh / (ys * sw)) {
				v.width_only = true;
				v.height_only = false;
			}
		} else if (opt_.smartfit > 100 - 100 * ys * sw / (xs * sh)) {
			v.width_only = false;
			v.height_only = true;
		}
	}

	Image shown;
	int pan_x = 0, pan_y = 0, offs_x = 0, offs_y = 0;
	bool retransform = true, reframe = false, refresh = true, place = true, noshow = false;
	int delay = opt_.delay;
	const auto start = Clock::now();
	const auto delay_end = start + milliseconds(delay * 100);
	auto animating = [&] { return pic.animation && pic.animation->delay().count() > 0; };
	auto frame_end = start + (animating() ? pic.animation->delay() : milliseconds{});
	const bool keys = term_.interactive() && !opt_.skip_tty;

	for (;;) {
		if (quit_requested)
			return Navigation::quit;

		if (retransform || reframe) {
			shown = transform(frame, v, sw, sh);
			offs_x = shown.width < sw ? (sw - shown.width) / 2 : 0;
			offs_y = shown.height < sh ? (sh - shown.height) / 2 : 0;
			pan_x = std::clamp(pan_x, 0, std::max(shown.width - sw, 0));
			pan_y = std::clamp(pan_y, 0, std::max(shown.height - sh, 0));
			refresh = true;
		}
		if (retransform) {
			pan_x = pan_y = 0;
			if (opt_.smartfit >= 0) {
				pan_x = std::max(shown.width - sw, 0) / 2;
				pan_y = std::max(shown.height - sh, 0) / 2;
			}
			place = true;
			if (opt_.clear)
				clear_console();
			if (opt_.image_info)
				print_info(name, frame, v);
			std::fflush(stdout);
			retransform = false;
		}
		reframe = false;

		if (redraw_requested) {
			redraw_requested = 0;
			refresh = place = true;
		}
		if (refresh && !noshow && vt_active) {
			if (place) {
				if (offs_x || offs_y)
					fb_.clear();
				background_.clear();
				if (shown.has_alpha())
					background_ = fb_.read(offs_x, offs_y, std::min(shown.width, sw),
							       std::min(shown.height, sh));
				place = false;
			}
			draw(shown, pan_x, pan_y, offs_x, offs_y);
			refresh = false;
		}

		std::optional<Clock::time_point> deadline;
		if (delay)
			deadline = delay_end;
		if (animating() && (!deadline || frame_end < *deadline))
			deadline = frame_end;

		std::optional<int> key;
		if (keys) {
			std::optional<milliseconds> timeout;
			if (deadline)
				timeout = std::chrono::ceil<milliseconds>(*deadline - Clock::now());
			key = term_.read_key(timeout);
		} else if (deadline) {
			Terminal::sleep(std::chrono::ceil<milliseconds>(*deadline - Clock::now()));
		} else {
			return Navigation::next;
		}

		if (quit_requested)
			return Navigation::quit;

		if (!key) {
			const auto now = Clock::now();
			if (delay && now >= delay_end)
				return Navigation::next;
			if (animating() && now >= frame_end) {
				pic.animation->next(frame);
				if (!opt_.alpha)
					frame.alpha.clear();
				frame_end += pic.animation->delay();
				if (frame_end < now)
					frame_end = now + pic.animation->delay();
				reframe = true;
			}
			continue;
		}

		// A key stops the slideshow timer for this image.
		delay = 0;

		switch (*key) {
		case -1:
		case 'q':
			return Navigation::quit;
		case ' ':
		case '\n':
		case '\r':
		case '>':
		case '.':
			return Navigation::next;
		case '<':
		case ',':
			return Navigation::previous;
		case 'r':
			refresh = place = true;
			break;
		case 'a':
		case 'D':
			if (pan_x == 0)
				break;
			pan_x = std::max(pan_x - shown.width / pan_stepping, 0);
			refresh = true;
			break;
		case 'd':
		case 'C':
			if (offs_x || pan_x >= shown.width - sw)
				break;
			pan_x = std::min(pan_x + shown.width / pan_stepping, shown.width - sw);
			refresh = true;
			break;
		case 'w':
		case 'A':
			if (pan_y == 0)
				break;
			pan_y = std::max(pan_y - shown.height / pan_stepping, 0);
			refresh = true;
			break;
		case 'x':
		case 'B':
			if (offs_y || pan_y >= shown.height - sh)
				break;
			pan_y = std::min(pan_y + shown.height / pan_stepping, shown.height - sh);
			refresh = true;
			break;
		case 'f':
			v.shrink = !v.shrink;
			retransform = true;
			break;
		case 'e':
			v.enlarge = !v.enlarge;
			retransform = true;
			break;
		case 'l':
			v.width_only = !v.width_only;
			v.height_only = false;
			retransform = true;
			break;
		case 't':
			v.width_only = false;
			v.height_only = !v.height_only;
			retransform = true;
			break;
		case 'k':
			v.cal = !v.cal;
			retransform = true;
			break;
		case 'i':
			v.ignore_aspect = !v.ignore_aspect;
			retransform = true;
			break;
		case '0':
		case '+':
		case '-':
			v.cal = v.ignore_aspect = v.enlarge = v.shrink = false;
			v.width_only = v.height_only = false;
			v.zoom = *key == '0' ? 1 : *key == '+' ? v.zoom / 1.5 : v.zoom * 1.5;
			retransform = true;
			break;
		case 'p':
			v.cal = v.ignore_aspect = v.enlarge = v.shrink = false;
			v.width_only = v.height_only = false;
			v.zoom = 1;
			retransform = true;
			break;
		case 'n':
			v.rotation = (v.rotation + 3) % 4;
			retransform = true;
			break;
		case 'm':
			v.rotation = (v.rotation + 1) % 4;
			retransform = true;
			break;
		case 'h':
		case '\033':
			if ((*key == '\033' && !noshow) || !opt_.image_info)
				break;
			noshow = !noshow;
			retransform = true;
			break;
		default:
			break;
		}
	}
}

} // namespace fbvng
