// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace fbvng {

struct Image {
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> rgb;    // width * height * 3
	std::vector<std::uint8_t> alpha;  // width * height, empty when opaque

	bool has_alpha() const { return !alpha.empty(); }
};

class DecodeError : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

// Limits against decompression bombs and oversized input.
inline constexpr std::size_t max_file_size = std::size_t{256} << 20;
inline constexpr std::int64_t max_dimension = 65536;
inline constexpr std::int64_t max_pixels = std::int64_t{1} << 26;

// Validates the image size and returns the number of pixels.
std::size_t checked_pixels(std::int64_t width, std::int64_t height);

// Further frames of an animated image.
class FrameSource {
public:
	virtual ~FrameSource() = default;

	// How long the current frame stays on screen, zero once the
	// animation is over.
	virtual std::chrono::milliseconds delay() const = 0;

	// The current frame's own duration, also after the animation is over.
	virtual std::chrono::milliseconds frame_delay() const = 0;

	virtual std::size_t frame_count() const = 0;
	virtual std::size_t index() const = 0;

	// Advances to the next frame and stores it in out.
	virtual void next(Image &out) = 0;

	// Advances to the next frame without decoding it, for a caller that
	// kept it from an earlier loop; the next frame decoded is rebuilt.
	virtual void skip() = 0;

	// Moves to the given frame and stores it in out, throws
	// std::out_of_range for a frame past the end.
	virtual void seek(std::size_t frame, Image &out) = 0;
};

struct Picture {
	Image image;
	std::unique_ptr<FrameSource> animation;
};

} // namespace fbvng
