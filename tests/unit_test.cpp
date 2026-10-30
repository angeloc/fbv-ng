// SPDX-License-Identifier: GPL-2.0-or-later
// Transforms, size limits and malformed headers.
#include "check.hpp"
#include "decode.hpp"
#include "transform.hpp"

#include <array>

using namespace fbvng;

namespace {

// w x h image whose red channel holds the pixel index and alpha its row
Image numbered(int w, int h)
{
	Image img;
	img.width = w;
	img.height = h;
	for (int i = 0; i < w * h; i++) {
		img.rgb.insert(img.rgb.end(), {static_cast<std::uint8_t>(i), 0, 0});
		img.alpha.push_back(static_cast<std::uint8_t>(i / w));
	}
	return img;
}

std::uint8_t red(const Image &img, int x, int y)
{
	return img.rgb[static_cast<std::size_t>(y * img.width + x) * 3];
}

std::uint8_t alpha(const Image &img, int x, int y)
{
	return img.alpha[static_cast<std::size_t>(y * img.width + x)];
}

void test_rotate()
{
	// 0 1 2
	// 3 4 5
	const Image src = numbered(3, 2);

	const Image cw = rotate(src, 1);
	CHECK(cw.width == 2 && cw.height == 3);
	CHECK(red(cw, 0, 0) == 3 && red(cw, 1, 0) == 0);
	CHECK(red(cw, 0, 2) == 5 && red(cw, 1, 2) == 2);
	CHECK(alpha(cw, 0, 0) == 1 && alpha(cw, 1, 0) == 0);

	const Image half = rotate(src, 2);
	CHECK(half.width == 3 && half.height == 2);
	CHECK(red(half, 0, 0) == 5 && red(half, 2, 1) == 0);
	CHECK(alpha(half, 0, 0) == 1 && alpha(half, 2, 1) == 0);

	const Image ccw = rotate(src, 3);
	CHECK(ccw.width == 2 && ccw.height == 3);
	CHECK(red(ccw, 0, 0) == 2 && red(ccw, 1, 0) == 5);
	CHECK(red(ccw, 0, 2) == 0 && red(ccw, 1, 2) == 3);

	const Image full = rotate(rotate(cw, 1), 2);
	CHECK(full.rgb == src.rgb && full.alpha == src.alpha);
	CHECK(rotate(src, -1).rgb == ccw.rgb);
}

void test_resize()
{
	const Image src = numbered(2, 2);

	const Image up = resize_nearest(src, 4, 4);
	CHECK(up.width == 4 && up.height == 4);
	CHECK(red(up, 0, 0) == 0 && red(up, 1, 1) == 0);
	CHECK(red(up, 3, 0) == 1 && red(up, 0, 3) == 2 && red(up, 3, 3) == 3);
	CHECK(alpha(up, 3, 3) == 1);

	Image four;
	four.width = 4;
	four.height = 1;
	four.rgb = {0, 0, 0, 100, 100, 100, 10, 20, 30, 30, 40, 50};
	const Image avg = resize_average(four, 2, 1);
	CHECK(avg.width == 2 && avg.height == 1 && !avg.has_alpha());
	CHECK(avg.rgb[0] == 36 && avg.rgb[1] == 40 && avg.rgb[2] == 43);
	CHECK(avg.rgb[3] == 20 && avg.rgb[4] == 30 && avg.rgb[5] == 40);

	CHECK_THROWS(DecodeError, resize_nearest(src, 0, 10));
}

void test_limits()
{
	CHECK(checked_pixels(1, 1) == 1);
	CHECK(checked_pixels(8192, 8192) == std::size_t{8192} * 8192);
	CHECK_THROWS(DecodeError, checked_pixels(0, 1));
	CHECK_THROWS(DecodeError, checked_pixels(-5, 10));
	CHECK_THROWS(DecodeError, checked_pixels(max_dimension + 1, 1));
	CHECK_THROWS(DecodeError, checked_pixels(65536, 65536));
}

#ifdef FBVNG_HAVE_BMP
std::vector<std::uint8_t> bmp_header(std::int32_t w, std::int32_t h, std::uint16_t bpp)
{
	std::vector<std::uint8_t> d(54);
	auto put32 = [&](std::size_t o, std::uint32_t v) {
		for (int i = 0; i < 4; i++)
			d[o + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
	};
	d[0] = 'B';
	d[1] = 'M';
	put32(10, 54);
	put32(14, 40);
	put32(18, static_cast<std::uint32_t>(w));
	put32(22, static_cast<std::uint32_t>(h));
	d[26] = 1;
	d[28] = static_cast<std::uint8_t>(bpp);
	return d;
}
#endif

void test_malformed()
{
	CHECK_THROWS(DecodeError, decode(std::vector<std::uint8_t>{}));
	CHECK_THROWS(DecodeError, decode(std::vector<std::uint8_t>{'h', 'e', 'l', 'l', 'o'}));

#ifdef FBVNG_HAVE_BMP
	// Headers claiming huge or missing pixel data must not allocate or read.
	CHECK_THROWS(DecodeError, decode(bmp_header(100000, 100000, 24)));
	CHECK_THROWS(DecodeError, decode(bmp_header(0x7fffffff, -0x7fffffff, 24)));
	CHECK_THROWS(DecodeError, decode(bmp_header(64, 64, 24)));
	CHECK_THROWS(DecodeError, decode(bmp_header(4, 4, 7)));

	auto ok = bmp_header(1, 1, 24);
	ok.insert(ok.end(), {1, 2, 3, 0});
	const Picture pic = decode(ok);
	CHECK(pic.image.width == 1 && pic.image.rgb == (std::vector<std::uint8_t>{3, 2, 1}));
#endif

#ifdef FBVNG_HAVE_PNG
	CHECK_THROWS(DecodeError, decode(std::vector<std::uint8_t>{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0}));
#endif
#ifdef FBVNG_HAVE_JPEG
	CHECK_THROWS(DecodeError, decode(std::vector<std::uint8_t>{0xff, 0xd8, 0xff, 0xe0, 0, 0}));
#endif
#ifdef FBVNG_HAVE_GIF
	CHECK_THROWS(DecodeError, decode(std::vector<std::uint8_t>{'G', 'I', 'F', '8', '9', 'a', 1, 0}));
#endif
}

} // namespace

int main()
{
	test_rotate();
	test_resize();
	test_limits();
	test_malformed();
	return check_result();
}
