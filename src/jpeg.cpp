// SPDX-License-Identifier: GPL-2.0-or-later
#include "decode.hpp"

#include <csetjmp>
#include <cstdio>
#include <jpeglib.h>

namespace fbvng {

namespace {

struct ErrorManager {
	jpeg_error_mgr pub;
	std::jmp_buf jump;
	char message[JMSG_LENGTH_MAX];
};

[[noreturn]] void on_error(j_common_ptr cinfo)
{
	auto *err = reinterpret_cast<ErrorManager *>(cinfo->err);
	(*cinfo->err->format_message)(cinfo, err->message);
	std::longjmp(err->jump, 1);
}

void on_message(j_common_ptr) {}

struct Decoder {
	jpeg_decompress_struct cinfo{};
	ErrorManager err{};

	Decoder()
	{
		cinfo.err = jpeg_std_error(&err.pub);
		err.pub.error_exit = on_error;
		err.pub.output_message = on_message;
	}
	~Decoder() { jpeg_destroy_decompress(&cinfo); }
	Decoder(const Decoder &) = delete;
	Decoder &operator=(const Decoder &) = delete;
};

// libjpeg reports errors with longjmp. The two functions below are the only
// ones calling setjmp and hold no objects with destructors, so the jump
// never skips one; all state lives in the caller's Decoder.

bool read_header(Decoder &d, std::span<const std::uint8_t> data)
{
	if (setjmp(d.err.jump))
		return false;
	jpeg_create_decompress(&d.cinfo);
	jpeg_mem_src(&d.cinfo, data.data(), static_cast<unsigned long>(data.size()));
	jpeg_read_header(&d.cinfo, TRUE);
	if (d.cinfo.jpeg_color_space == JCS_CMYK || d.cinfo.jpeg_color_space == JCS_YCCK)
		d.cinfo.out_color_space = JCS_CMYK;
	else
		d.cinfo.out_color_space = JCS_RGB;
	jpeg_calc_output_dimensions(&d.cinfo);
	return true;
}

bool read_pixels(Decoder &d, std::uint8_t *rgb, std::uint8_t *cmyk_row)
{
	if (setjmp(d.err.jump))
		return false;

	jpeg_start_decompress(&d.cinfo);
	const JDIMENSION width = d.cinfo.output_width;
	const bool inverted = d.cinfo.saw_Adobe_marker;

	while (d.cinfo.output_scanline < d.cinfo.output_height) {
		std::uint8_t *dst = rgb + std::size_t{d.cinfo.output_scanline} * width * 3;
		JSAMPROW row = cmyk_row ? cmyk_row : dst;
		if (jpeg_read_scanlines(&d.cinfo, &row, 1) != 1)
			return false;
		if (!cmyk_row)
			continue;
		for (JDIMENSION x = 0; x < width; x++) {
			unsigned c = cmyk_row[x * 4], m = cmyk_row[x * 4 + 1];
			unsigned y = cmyk_row[x * 4 + 2], k = cmyk_row[x * 4 + 3];
			if (!inverted) {
				c = 255 - c;
				m = 255 - m;
				y = 255 - y;
				k = 255 - k;
			}
			dst[x * 3] = static_cast<std::uint8_t>(c * k / 255);
			dst[x * 3 + 1] = static_cast<std::uint8_t>(m * k / 255);
			dst[x * 3 + 2] = static_cast<std::uint8_t>(y * k / 255);
		}
	}
	jpeg_finish_decompress(&d.cinfo);
	return true;
}

} // namespace

Picture decode_jpeg(std::span<const std::uint8_t> data)
{
	Decoder d;

	if (!read_header(d, data))
		throw DecodeError(d.err.message);

	const std::size_t pixels = checked_pixels(d.cinfo.output_width, d.cinfo.output_height);
	const bool cmyk = d.cinfo.out_color_space == JCS_CMYK;
	if (d.cinfo.output_components != (cmyk ? 4 : 3))
		throw DecodeError("unsupported JPEG color space");

	Picture pic;
	Image &img = pic.image;
	img.width = static_cast<int>(d.cinfo.output_width);
	img.height = static_cast<int>(d.cinfo.output_height);
	img.rgb.resize(pixels * 3);
	std::vector<std::uint8_t> cmyk_row(cmyk ? std::size_t{d.cinfo.output_width} * 4 : 0);

	if (!read_pixels(d, img.rgb.data(), cmyk ? cmyk_row.data() : nullptr))
		throw DecodeError(d.err.message);
	return pic;
}

} // namespace fbvng
