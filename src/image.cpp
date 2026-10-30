// SPDX-License-Identifier: GPL-2.0-or-later
#include "image.hpp"

namespace fbvng {

std::size_t checked_pixels(std::int64_t width, std::int64_t height)
{
	if (width <= 0 || height <= 0)
		throw DecodeError("invalid image size");
	if (width > max_dimension || height > max_dimension ||
	    width * height > max_pixels)
		throw DecodeError("image too large");
	return static_cast<std::size_t>(width * height);
}

} // namespace fbvng
