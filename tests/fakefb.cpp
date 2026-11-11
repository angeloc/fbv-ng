// SPDX-License-Identifier: GPL-2.0-or-later
// LD_PRELOAD shim answering the framebuffer ioctls, so a regular file can
// stand in for the device. FAKEFB_GEOMETRY="<width>,<height>,<bpp>"; each
// line is padded by 16 bytes to exercise the stride handling.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <linux/fb.h>
#include <sys/ioctl.h>

namespace {

void geometry(unsigned &w, unsigned &h, unsigned &bpp)
{
	w = 64, h = 48, bpp = 32;
	if (const char *g = std::getenv("FAKEFB_GEOMETRY"))
		std::sscanf(g, "%u,%u,%u", &w, &h, &bpp);
}

} // namespace

extern "C" int ioctl(int, unsigned long request, ...)
{
	va_list ap;
	va_start(ap, request);
	void *arg = va_arg(ap, void *);
	va_end(ap);

	unsigned w, h, bpp;
	geometry(w, h, bpp);
	const unsigned line = w * ((bpp + 7) / 8) + 16;

	switch (request) {
	case FBIOGET_VSCREENINFO: {
		auto *v = static_cast<fb_var_screeninfo *>(arg);
		std::memset(v, 0, sizeof(*v));
		v->xres = v->xres_virtual = w;
		v->yres = v->yres_virtual = h;
		v->bits_per_pixel = bpp;
		if (bpp == 16) {
			v->red = {11, 5, 0};
			v->green = {5, 6, 0};
			v->blue = {0, 5, 0};
		} else if (bpp >= 24) {
			v->red = {16, 8, 0};
			v->green = {8, 8, 0};
			v->blue = {0, 8, 0};
		}
		return 0;
	}
	case FBIOGET_FSCREENINFO: {
		auto *f = static_cast<fb_fix_screeninfo *>(arg);
		std::memset(f, 0, sizeof(*f));
		f->line_length = line;
		f->smem_len = line * h;
		return 0;
	}
	case FBIOGETCMAP:
	case FBIOPUTCMAP:
		return 0;
	case FBIOBLANK:
		// Logs the blank levels asked, in $FAKEFB_BLANK.
		if (const char *log = std::getenv("FAKEFB_BLANK"))
			if (FILE *f = std::fopen(log, "a")) {
				std::fprintf(f, "%lu\n", reinterpret_cast<unsigned long>(arg));
				std::fclose(f);
			}
		return 0;
	default:
		return -1;
	}
}
