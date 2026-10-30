// SPDX-License-Identifier: GPL-2.0-or-later
#include "decode.hpp"
#include "unique_fd.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace fbvng {

namespace {

bool starts_with(std::span<const std::uint8_t> data,
		 std::initializer_list<std::uint8_t> magic)
{
	return data.size() >= magic.size() &&
	       std::equal(magic.begin(), magic.end(), data.begin());
}

[[noreturn, maybe_unused]] void not_built(const char *format)
{
	throw DecodeError(std::string(format) + " support not built in");
}

} // namespace

std::vector<std::uint8_t> read_file(const std::string &path)
{
	UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
	if (!fd)
		throw DecodeError(std::strerror(errno));

	std::vector<std::uint8_t> data;
	std::uint8_t chunk[65536];
	for (;;) {
		const ssize_t n = ::read(fd.get(), chunk, sizeof(chunk));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			throw DecodeError(std::strerror(errno));
		}
		if (n == 0)
			break;
		if (data.size() + static_cast<std::size_t>(n) > max_file_size)
			throw DecodeError("file too large");
		data.insert(data.end(), chunk, chunk + n);
	}
	return data;
}

Picture decode(std::span<const std::uint8_t> data)
{
	if (starts_with(data, {'G', 'I', 'F', '8'})) {
#ifdef FBVNG_HAVE_GIF
		return decode_gif(data);
#else
		not_built("GIF");
#endif
	}
	if (starts_with(data, {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'})) {
#ifdef FBVNG_HAVE_PNG
		return decode_png(data);
#else
		not_built("PNG");
#endif
	}
	if (starts_with(data, {0xff, 0xd8, 0xff})) {
#ifdef FBVNG_HAVE_JPEG
		return decode_jpeg(data);
#else
		not_built("JPEG");
#endif
	}
	if (starts_with(data, {'B', 'M'})) {
#ifdef FBVNG_HAVE_BMP
		return decode_bmp(data);
#else
		not_built("BMP");
#endif
	}
	throw DecodeError("unknown file format");
}

Picture decode_file(const std::string &path)
{
	const auto data = read_file(path);
	return decode(data);
}

} // namespace fbvng
