// SPDX-License-Identifier: GPL-2.0-or-later
#include "byte_reader.hpp"
#include "decode.hpp"

#include <algorithm>
#include <array>
#include <bit>

namespace fbvng {

namespace {

enum : std::uint32_t {
	bi_rgb = 0,
	bi_bitfields = 3,
	bi_alphabitfields = 6,
};

// Extracts one channel described by a BI_BITFIELDS mask, scaled to 8 bit.
class Channel {
public:
	explicit Channel(std::uint32_t mask)
		: mask_(mask), shift_(mask ? std::countr_zero(mask) : 0),
		  max_(mask ? mask >> shift_ : 0)
	{
	}

	bool present() const { return mask_ != 0; }

	std::uint8_t operator()(std::uint32_t pixel) const
	{
		if (!max_)
			return 0;
		const std::uint64_t v = (pixel & mask_) >> shift_;
		return static_cast<std::uint8_t>((v * 255 + max_ / 2) / max_);
	}

private:
	std::uint32_t mask_;
	int shift_;
	std::uint32_t max_;
};

} // namespace

Picture decode_bmp(std::span<const std::uint8_t> data)
{
	const ByteReader r(data);

	if (r.u8(0) != 'B' || r.u8(1) != 'M')
		throw DecodeError("not a BMP file");

	const std::uint32_t pixel_offset = r.le32(10);
	const std::uint32_t header_size = r.le32(14);
	std::int64_t width, height;
	unsigned bpp;
	std::uint32_t compression = bi_rgb;
	std::uint32_t colors_used = 0;
	std::size_t palette_entry = 4;

	if (header_size == 12) {
		// OS/2 BITMAPCOREHEADER
		width = r.le16(18);
		height = static_cast<std::int16_t>(r.le16(20));
		bpp = r.le16(24);
		palette_entry = 3;
	} else if (header_size >= 40) {
		width = r.sle32(18);
		height = r.sle32(22);
		bpp = r.le16(28);
		compression = r.le32(30);
		colors_used = r.le32(46);
	} else {
		throw DecodeError("unsupported BMP header");
	}

	const bool top_down = height < 0;
	if (top_down)
		height = -height;
	const std::size_t pixels = checked_pixels(width, height);

	std::uint32_t masks[4] = {};
	if (compression == bi_bitfields || compression == bi_alphabitfields) {
		if (header_size < 40 || (bpp != 16 && bpp != 32))
			throw DecodeError("invalid BMP bitfields");
		// The masks follow a plain BITMAPINFOHEADER and are part of
		// the larger headers.
		masks[0] = r.le32(54);
		masks[1] = r.le32(58);
		masks[2] = r.le32(62);
		if (compression == bi_alphabitfields || header_size >= 56)
			masks[3] = r.le32(66);
	} else if (compression != bi_rgb) {
		throw DecodeError("compressed BMP is not supported");
	} else if (bpp == 16) {
		masks[0] = 0x7c00;
		masks[1] = 0x03e0;
		masks[2] = 0x001f;
	} else if (bpp == 32) {
		masks[0] = 0x00ff0000;
		masks[1] = 0x0000ff00;
		masks[2] = 0x000000ff;
	}

	std::array<std::array<std::uint8_t, 3>, 256> palette{};
	if (bpp == 1 || bpp == 4 || bpp == 8) {
		const std::size_t offset = 14 + std::size_t{header_size};
		// Writers often store fewer entries than the header implies.
		const std::size_t room = pixel_offset > offset ? (pixel_offset - offset) / palette_entry : 0;
		const std::size_t max_colors = std::size_t{1} << bpp;
		const std::size_t count = std::min(room, colors_used ?
			std::min<std::size_t>(colors_used, max_colors) : max_colors);
		for (std::size_t i = 0; i < count; i++) {
			const auto e = r.bytes(offset + i * palette_entry, 3);
			palette[i] = {e[2], e[1], e[0]};
		}
	} else if (bpp != 16 && bpp != 24 && bpp != 32) {
		throw DecodeError("unsupported BMP bit depth");
	}

	const std::size_t stride = ((static_cast<std::size_t>(width) * bpp + 31) / 32) * 4;
	const std::size_t rows = static_cast<std::size_t>(height);
	const auto raster = r.bytes(pixel_offset, stride * rows);

	const Channel red(masks[0]), green(masks[1]), blue(masks[2]), alpha(masks[3]);

	Picture pic;
	Image &img = pic.image;
	img.width = static_cast<int>(width);
	img.height = static_cast<int>(height);
	img.rgb.resize(pixels * 3);
	if (alpha.present())
		img.alpha.resize(pixels);

	const auto w = static_cast<std::size_t>(width);
	for (std::size_t y = 0; y < rows; y++) {
		const auto src = raster.subspan((top_down ? y : rows - 1 - y) * stride, stride);
		std::uint8_t *dst = img.rgb.data() + y * w * 3;

		for (std::size_t x = 0; x < w; x++, dst += 3) {
			std::uint32_t px;
			switch (bpp) {
			case 1:
			case 4:
			case 8: {
				const std::size_t bit = x * bpp;
				const unsigned shift = 8 - bpp - bit % 8;
				const unsigned index = (src[bit / 8] >> shift) & ((1u << bpp) - 1);
				std::copy_n(palette[index].data(), 3, dst);
				continue;
			}
			case 24:
				dst[0] = src[x * 3 + 2];
				dst[1] = src[x * 3 + 1];
				dst[2] = src[x * 3];
				continue;
			case 16:
				px = static_cast<std::uint32_t>(src[x * 2] | src[x * 2 + 1] << 8);
				break;
			default:
				px = static_cast<std::uint32_t>(src[x * 4]) |
				     static_cast<std::uint32_t>(src[x * 4 + 1]) << 8 |
				     static_cast<std::uint32_t>(src[x * 4 + 2]) << 16 |
				     static_cast<std::uint32_t>(src[x * 4 + 3]) << 24;
				break;
			}
			dst[0] = red(px);
			dst[1] = green(px);
			dst[2] = blue(px);
			if (alpha.present())
				img.alpha[y * w + x] = alpha(px);
		}
	}

	// Many writers store a zero alpha mask channel; treat it as opaque.
	if (img.has_alpha() &&
	    std::all_of(img.alpha.begin(), img.alpha.end(), [](auto a) { return a == 0; }))
		img.alpha.clear();

	return pic;
}

} // namespace fbvng
