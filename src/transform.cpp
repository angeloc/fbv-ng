// SPDX-License-Identifier: GPL-2.0-or-later
#include "transform.hpp"

#include <algorithm>

namespace fbvng {

namespace {

Image blank(int width, int height, bool alpha)
{
	const std::size_t pixels = checked_pixels(width, height);
	Image img;
	img.width = width;
	img.height = height;
	img.rgb.resize(pixels * 3);
	if (alpha)
		img.alpha.resize(pixels);
	return img;
}

std::size_t at(const Image &img, std::int64_t x, std::int64_t y)
{
	return static_cast<std::size_t>(y * img.width + x);
}

void resize_alpha(const Image &src, Image &dst)
{
	for (std::int64_t y = 0; y < dst.height; y++) {
		const std::int64_t sy = y * src.height / dst.height;
		for (std::int64_t x = 0; x < dst.width; x++)
			dst.alpha[at(dst, x, y)] = src.alpha[at(src, x * src.width / dst.width, sy)];
	}
}

} // namespace

Image resize_nearest(const Image &src, int width, int height)
{
	Image dst = blank(width, height, src.has_alpha());
	for (std::int64_t y = 0; y < height; y++) {
		const std::int64_t sy = y * src.height / height;
		for (std::int64_t x = 0; x < width; x++) {
			const std::size_t s = at(src, x * src.width / width, sy) * 3;
			std::copy_n(&src.rgb[s], 3, &dst.rgb[at(dst, x, y) * 3]);
		}
	}
	if (src.has_alpha())
		resize_alpha(src, dst);
	return dst;
}

Image resize_average(const Image &src, int width, int height)
{
	Image dst = blank(width, height, src.has_alpha());
	for (std::int64_t y = 0; y < height; y++) {
		const std::int64_t ya = y * src.height / height;
		const std::int64_t yb = std::min<std::int64_t>((y + 1) * src.height / height, src.height - 1);
		for (std::int64_t x = 0; x < width; x++) {
			const std::int64_t xa = x * src.width / width;
			const std::int64_t xb = std::min<std::int64_t>((x + 1) * src.width / width, src.width - 1);
			std::uint64_t sum[3] = {}, count = 0;
			for (std::int64_t sy = ya; sy <= yb; sy++)
				for (std::int64_t sx = xa; sx <= xb; sx++, count++)
					for (int c = 0; c < 3; c++)
						sum[c] += src.rgb[at(src, sx, sy) * 3 + static_cast<std::size_t>(c)];
			for (int c = 0; c < 3; c++)
				dst.rgb[at(dst, x, y) * 3 + static_cast<std::size_t>(c)] =
					static_cast<std::uint8_t>(sum[c] / count);
		}
	}
	if (src.has_alpha())
		resize_alpha(src, dst);
	return dst;
}

Image rotate(const Image &src, int quarter_turns)
{
	const int turns = ((quarter_turns % 4) + 4) % 4;
	if (!turns)
		return src;

	const bool swap = turns != 2;
	Image dst = blank(swap ? src.height : src.width, swap ? src.width : src.height, src.has_alpha());
	for (std::int64_t y = 0; y < src.height; y++)
		for (std::int64_t x = 0; x < src.width; x++) {
			std::int64_t dx, dy;
			switch (turns) {
			case 1:
				dx = src.height - 1 - y;
				dy = x;
				break;
			case 2:
				dx = src.width - 1 - x;
				dy = src.height - 1 - y;
				break;
			default:
				dx = y;
				dy = src.width - 1 - x;
				break;
			}
			const std::size_t s = at(src, x, y), d = at(dst, dx, dy);
			std::copy_n(&src.rgb[s * 3], 3, &dst.rgb[d * 3]);
			if (src.has_alpha())
				dst.alpha[d] = src.alpha[s];
		}
	return dst;
}

} // namespace fbvng
