// SPDX-License-Identifier: GPL-2.0-or-later
#include "decode.hpp"

#include <csetjmp>
#include <cstring>
#include <png.h>

namespace fbvng {

namespace {

struct Decoder {
	png_structp png = nullptr;
	png_infop info = nullptr;
	std::jmp_buf jump;
	char message[128] = "corrupt PNG";
	std::span<const std::uint8_t> data;
	std::size_t pos = 0;

	~Decoder() { png_destroy_read_struct(&png, &info, nullptr); }
};

[[noreturn]] void on_error(png_structp png, png_const_charp msg)
{
	auto *d = static_cast<Decoder *>(png_get_error_ptr(png));
	std::strncpy(d->message, msg, sizeof(d->message) - 1);
	std::longjmp(d->jump, 1);
}

void on_warning(png_structp, png_const_charp) {}

void on_read(png_structp png, png_bytep out, std::size_t count)
{
	auto *d = static_cast<Decoder *>(png_get_io_ptr(png));
	if (count > d->data.size() - d->pos)
		png_error(png, "truncated file");
	std::memcpy(out, d->data.data() + d->pos, count);
	d->pos += count;
}

// libpng reports errors with longjmp. The two functions below are the only
// ones calling setjmp and hold no objects with destructors, so the jump
// never skips one; all state lives in the caller's Decoder.

bool read_header(Decoder &d)
{
	if (setjmp(d.jump))
		return false;

	png_set_user_limits(d.png, static_cast<png_uint_32>(max_dimension),
			    static_cast<png_uint_32>(max_dimension));
	png_read_info(d.png, d.info);

	const int type = png_get_color_type(d.png, d.info);
	if (type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(d.png);
	if (type == PNG_COLOR_TYPE_GRAY && png_get_bit_depth(d.png, d.info) < 8)
		png_set_expand_gray_1_2_4_to_8(d.png);
	if (png_get_valid(d.png, d.info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(d.png);
	png_set_scale_16(d.png);
	if (type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(d.png);
	png_set_interlace_handling(d.png);
	png_read_update_info(d.png, d.info);
	return true;
}

bool read_rows(Decoder &d, png_bytepp rows)
{
	if (setjmp(d.jump))
		return false;
	png_read_image(d.png, rows);
	png_read_end(d.png, nullptr);
	return true;
}

} // namespace

Picture decode_png(std::span<const std::uint8_t> data)
{
	Decoder d;
	d.data = data;
	d.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &d, on_error, on_warning);
	if (!d.png || !(d.info = png_create_info_struct(d.png)))
		throw DecodeError("out of memory");
	png_set_read_fn(d.png, &d, on_read);

	if (!read_header(d))
		throw DecodeError(d.message);

	const png_uint_32 width = png_get_image_width(d.png, d.info);
	const png_uint_32 height = png_get_image_height(d.png, d.info);
	const std::size_t pixels = checked_pixels(width, height);
	const std::size_t channels = png_get_channels(d.png, d.info);
	if ((channels != 3 && channels != 4) || png_get_bit_depth(d.png, d.info) != 8 ||
	    png_get_rowbytes(d.png, d.info) != std::size_t{width} * channels)
		throw DecodeError("unsupported PNG layout");

	std::vector<std::uint8_t> buffer(pixels * channels);
	std::vector<png_bytep> rows(height);
	for (std::size_t y = 0; y < height; y++)
		rows[y] = buffer.data() + y * width * channels;
	if (!read_rows(d, rows.data()))
		throw DecodeError(d.message);

	Picture pic;
	Image &img = pic.image;
	img.width = static_cast<int>(width);
	img.height = static_cast<int>(height);
	if (channels == 3) {
		img.rgb = std::move(buffer);
		return pic;
	}
	img.rgb.resize(pixels * 3);
	img.alpha.resize(pixels);
	for (std::size_t i = 0; i < pixels; i++) {
		std::memcpy(&img.rgb[i * 3], &buffer[i * 4], 3);
		img.alpha[i] = buffer[i * 4 + 3];
	}
	return pic;
}

} // namespace fbvng
