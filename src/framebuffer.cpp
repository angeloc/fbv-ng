// SPDX-License-Identifier: GPL-2.0-or-later
#include "framebuffer.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <system_error>

namespace fbvng {

namespace {

[[noreturn]] void fail(const std::string &what)
{
	throw std::system_error(errno, std::generic_category(), what);
}

fb_cmap cmap_of(std::array<std::array<std::uint16_t, 256>, 3> &c)
{
	fb_cmap map{};
	map.start = 0;
	map.len = 256;
	map.red = c[0].data();
	map.green = c[1].data();
	map.blue = c[2].data();
	return map;
}

// 16 and 32 bpp pixels are in CPU byte order, 24 bpp is little endian.
void store(std::uint8_t *p, std::uint32_t px, unsigned bytes)
{
	if (bytes == 2) {
		const auto v = static_cast<std::uint16_t>(px);
		std::memcpy(p, &v, 2);
	} else if (bytes == 4) {
		std::memcpy(p, &px, 4);
	} else {
		for (unsigned b = 0; b < bytes; b++)
			p[b] = static_cast<std::uint8_t>(px >> (8 * b));
	}
}

std::uint32_t load(const std::uint8_t *p, unsigned bytes)
{
	std::uint32_t px = 0;
	if (bytes == 2) {
		std::uint16_t v;
		std::memcpy(&v, p, 2);
		px = v;
	} else if (bytes == 4) {
		std::memcpy(&px, p, 4);
	} else {
		for (unsigned b = 0; b < bytes; b++)
			px |= static_cast<std::uint32_t>(p[b]) << (8 * b);
	}
	return px;
}

} // namespace

Framebuffer::Framebuffer(const std::string &device)
	: fd_(::open(device.c_str(), O_RDWR | O_CLOEXEC))
{
	if (!fd_)
		fail("open " + device);
	if (::ioctl(fd_.get(), FBIOGET_VSCREENINFO, &var_) < 0)
		fail("FBIOGET_VSCREENINFO");
	if (::ioctl(fd_.get(), FBIOGET_FSCREENINFO, &fix_) < 0)
		fail("FBIOGET_FSCREENINFO");

	switch (var_.bits_per_pixel) {
	case 8:
		bytes_per_pixel_ = 1;
		red_ = {5, 3};
		green_ = {2, 3};
		blue_ = {0, 2};
		break;
	case 15:
	case 16:
		bytes_per_pixel_ = 2;
		break;
	case 24:
		bytes_per_pixel_ = 3;
		break;
	case 32:
		bytes_per_pixel_ = 4;
		break;
	default:
		throw std::runtime_error("unsupported framebuffer depth " +
					 std::to_string(var_.bits_per_pixel));
	}

	if (bytes_per_pixel_ > 1) {
		red_ = {var_.red.offset, var_.red.length};
		green_ = {var_.green.offset, var_.green.length};
		blue_ = {var_.blue.offset, var_.blue.length};
		// Some drivers leave the bitfields empty.
		if (!red_.length || !green_.length || !blue_.length) {
			if (var_.bits_per_pixel == 15) {
				red_ = {10, 5}, green_ = {5, 5}, blue_ = {0, 5};
			} else if (var_.bits_per_pixel == 16) {
				red_ = {11, 5}, green_ = {5, 6}, blue_ = {0, 5};
			} else {
				red_ = {16, 8}, green_ = {8, 8}, blue_ = {0, 8};
			}
		}
	}
	for (const auto &c : {red_, green_, blue_})
		if (!c.length || c.length > 8 || c.offset + c.length > bytes_per_pixel_ * 8)
			throw std::runtime_error("unsupported framebuffer pixel format");

	const Channel channels[] = {red_, green_, blue_};
	for (int c = 0; c < 3; c++) {
		const unsigned max = (1u << channels[c].length) - 1;
		for (unsigned v = 0; v < 256; v++)
			scale_[static_cast<std::size_t>(c)][v] = static_cast<std::uint8_t>((v * max + 127) / 255);
	}

	if (fix_.line_length < var_.xres * bytes_per_pixel_ ||
	    var_.yoffset + var_.yres > var_.yres_virtual)
		throw std::runtime_error("inconsistent framebuffer geometry");
	map_size_ = std::size_t{fix_.line_length} * var_.yres_virtual;
	if (fix_.smem_len && map_size_ > fix_.smem_len)
		throw std::runtime_error("framebuffer smaller than its geometry");

	void *map = ::mmap(nullptr, map_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_.get(), 0);
	if (map == MAP_FAILED)
		fail("mmap framebuffer");
	map_ = static_cast<std::uint8_t *>(map);

	if (bytes_per_pixel_ == 1) {
		auto saved = cmap_of(saved_cmap_);
		restore_cmap_ = ::ioctl(fd_.get(), FBIOGETCMAP, &saved) == 0;

		std::array<std::array<std::uint16_t, 256>, 3> rgb332{};
		for (unsigned i = 0; i < 256; i++) {
			rgb332[0][i] = static_cast<std::uint16_t>(((i >> 5) & 7) * 0xffff / 7);
			rgb332[1][i] = static_cast<std::uint16_t>(((i >> 2) & 7) * 0xffff / 7);
			rgb332[2][i] = static_cast<std::uint16_t>((i & 3) * 0xffff / 3);
		}
		auto map332 = cmap_of(rgb332);
		::ioctl(fd_.get(), FBIOPUTCMAP, &map332);
	}
}

Framebuffer::~Framebuffer()
{
	if (restore_cmap_) {
		auto saved = cmap_of(saved_cmap_);
		::ioctl(fd_.get(), FBIOPUTCMAP, &saved);
	}
	if (map_)
		::munmap(map_, map_size_);
}

std::uint8_t *Framebuffer::row(int y) const
{
	return map_ + (std::size_t{var_.yoffset} + static_cast<std::size_t>(y)) * fix_.line_length;
}

bool Framebuffer::clip(int &x, int &y, int &w, int &h) const
{
	const int x1 = std::min(x + w, width()), y1 = std::min(y + h, height());
	x = std::max(x, 0);
	y = std::max(y, 0);
	w = x1 - x;
	h = y1 - y;
	return w > 0 && h > 0;
}

std::uint32_t Framebuffer::pack(const std::uint8_t *rgb) const
{
	return static_cast<std::uint32_t>(scale_[0][rgb[0]]) << red_.offset |
	       static_cast<std::uint32_t>(scale_[1][rgb[1]]) << green_.offset |
	       static_cast<std::uint32_t>(scale_[2][rgb[2]]) << blue_.offset;
}

void Framebuffer::unpack(std::uint32_t pixel, std::uint8_t *rgb) const
{
	const Channel channels[] = {red_, green_, blue_};
	for (int c = 0; c < 3; c++) {
		const std::uint32_t max = (1u << channels[c].length) - 1;
		rgb[c] = static_cast<std::uint8_t>(((pixel >> channels[c].offset) & max) * 255 / max);
	}
}

void Framebuffer::clear()
{
	for (int y = 0; y < height(); y++)
		std::memset(row(y), 0, std::size_t{var_.xres} * bytes_per_pixel_);
}

std::vector<std::uint8_t> Framebuffer::read(int x, int y, int w, int h) const
{
	std::vector<std::uint8_t> rgb;
	if (!clip(x, y, w, h))
		return rgb;
	rgb.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
	std::uint8_t *out = rgb.data();
	for (int j = 0; j < h; j++) {
		const std::uint8_t *p = row(y + j) + static_cast<std::size_t>(x) * bytes_per_pixel_;
		for (int i = 0; i < w; i++, p += bytes_per_pixel_, out += 3)
			unpack(load(p, bytes_per_pixel_), out);
	}
	return rgb;
}

void Framebuffer::write(int x, int y, int w, int h, std::span<const std::uint8_t> rgb)
{
	if (rgb.size() < static_cast<std::size_t>(std::max(w, 0)) * static_cast<std::size_t>(std::max(h, 0)) * 3)
		throw std::invalid_argument("short pixel buffer");
	const int src_x = x, src_y = y, src_w = w;
	if (!clip(x, y, w, h))
		return;

	std::vector<std::uint8_t> line(static_cast<std::size_t>(w) * bytes_per_pixel_);
	for (int j = 0; j < h; j++) {
		const std::uint8_t *in = &rgb[(static_cast<std::size_t>(y - src_y + j) * static_cast<std::size_t>(src_w) +
					      static_cast<std::size_t>(x - src_x)) * 3];
		std::uint8_t *out = line.data();
		for (int i = 0; i < w; i++, in += 3, out += bytes_per_pixel_)
			store(out, pack(in), bytes_per_pixel_);
		std::memcpy(row(y + j) + static_cast<std::size_t>(x) * bytes_per_pixel_, line.data(), line.size());
	}
}

} // namespace fbvng
