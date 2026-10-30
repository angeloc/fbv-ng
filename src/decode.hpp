// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "image.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fbvng {

// All decoders throw DecodeError on malformed or unsupported input.
Picture decode(std::span<const std::uint8_t> data);
Picture decode_file(const std::string &path);

std::vector<std::uint8_t> read_file(const std::string &path);

#ifdef FBVNG_HAVE_BMP
Picture decode_bmp(std::span<const std::uint8_t> data);
#endif
#ifdef FBVNG_HAVE_PNG
Picture decode_png(std::span<const std::uint8_t> data);
#endif
#ifdef FBVNG_HAVE_JPEG
Picture decode_jpeg(std::span<const std::uint8_t> data);
#endif
#ifdef FBVNG_HAVE_GIF
Picture decode_gif(std::span<const std::uint8_t> data);
#endif

} // namespace fbvng
