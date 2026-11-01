// SPDX-License-Identifier: GPL-2.0-or-later
#include "decode.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <gif_lib.h>
#include <stdexcept>

namespace fbvng {

namespace {

// Total indexed pixel data kept for one animation, and frames per file.
constexpr std::size_t max_frame_bytes = std::size_t{512} << 20;
constexpr std::size_t max_frames = 65536;

struct Frame {
	// Rectangle clipped to the canvas.
	int left = 0, top = 0, width = 0, height = 0;
	std::vector<std::uint8_t> indices;
	std::vector<std::uint8_t> row_valid;  // rows lost in a truncated file are 0
	std::vector<std::uint8_t> palette;    // RGB triplets
	int transparent = -1;
	int disposal = 0;
	int delay = 0;  // 1/100 s
};

class GifAnimation final : public FrameSource {
public:
	GifAnimation(int width, int height, std::array<std::uint8_t, 3> background,
		     std::vector<Frame> frames, unsigned loops, bool alpha)
		: width_(width), height_(height), background_(background),
		  frames_(std::move(frames)), loops_(loops), alpha_(alpha)
	{
		const auto pixels = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
		rgb_.resize(pixels * 3);
		mask_.resize(pixels);
		reset();
		draw(0);
	}

	void current(Image &out) const
	{
		out.width = width_;
		out.height = height_;
		out.rgb = rgb_;
		if (alpha_)
			out.alpha = mask_;
		else
			out.alpha.clear();
	}

	std::chrono::milliseconds delay() const override
	{
		if (frames_.size() < 2)
			return {};
		if (loops_ && loop_ + 1 >= loops_ && index_ + 1 == frames_.size())
			return {};
		return frame_delay();
	}

	std::chrono::milliseconds frame_delay() const override
	{
		const int d = frames_[index_].delay;
		// Like browsers, treat 0 and 1 as 10.
		return std::chrono::milliseconds((d < 2 ? 10 : d) * 10);
	}

	std::size_t frame_count() const override { return frames_.size(); }
	std::size_t index() const override { return index_; }

	void next(Image &out) override
	{
		dispose(index_);
		if (++index_ == frames_.size()) {
			index_ = 0;
			loop_++;
			reset();
		}
		draw(index_);
		current(out);
	}

	void seek(std::size_t frame, Image &out) override
	{
		if (frame >= frames_.size())
			throw std::out_of_range("frame out of range");
		// Frames build on each other, going back replays from the start.
		if (frame < index_) {
			index_ = 0;
			saved_rgb_.clear();
			saved_mask_.clear();
			reset();
			draw(0);
		}
		while (index_ < frame) {
			dispose(index_);
			draw(++index_);
		}
		current(out);
	}

private:
	void fill(int left, int top, int width, int height)
	{
		for (int y = top; y < top + height; y++)
			for (int x = left; x < left + width; x++) {
				const auto p = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) +
					       static_cast<std::size_t>(x);
				std::copy(background_.begin(), background_.end(), rgb_.begin() + static_cast<std::ptrdiff_t>(p * 3));
				mask_[p] = 0;
			}
	}

	void reset() { fill(0, 0, width_, height_); }

	void draw(std::size_t i)
	{
		const Frame &f = frames_[i];
		if (f.disposal == 3) {
			saved_rgb_ = rgb_;
			saved_mask_ = mask_;
		}
		const std::size_t colors = f.palette.size() / 3;
		for (int y = 0; y < f.height; y++) {
			if (!f.row_valid[static_cast<std::size_t>(y)])
				continue;
			const auto *src = &f.indices[static_cast<std::size_t>(y) * static_cast<std::size_t>(f.width)];
			for (int x = 0; x < f.width; x++) {
				const unsigned c = src[x];
				if (static_cast<int>(c) == f.transparent || c >= colors)
					continue;
				const auto p = static_cast<std::size_t>(f.top + y) * static_cast<std::size_t>(width_) +
					       static_cast<std::size_t>(f.left + x);
				std::copy_n(&f.palette[c * 3], 3, &rgb_[p * 3]);
				mask_[p] = 0xff;
			}
		}
	}

	void dispose(std::size_t i)
	{
		const Frame &f = frames_[i];
		if (f.disposal == 2) {
			fill(f.left, f.top, f.width, f.height);
		} else if (f.disposal == 3 && !saved_rgb_.empty()) {
			rgb_ = saved_rgb_;
			mask_ = saved_mask_;
		}
	}

	int width_, height_;
	std::array<std::uint8_t, 3> background_;
	std::vector<Frame> frames_;
	unsigned loops_;  // total plays, 0 = forever
	bool alpha_;
	std::size_t index_ = 0;
	unsigned loop_ = 0;
	std::vector<std::uint8_t> rgb_, mask_, saved_rgb_, saved_mask_;
};

struct Reader {
	std::span<const std::uint8_t> data;
	std::size_t pos = 0;
};

int read_input(GifFileType *gif, GifByteType *buf, int len)
{
	auto *r = static_cast<Reader *>(gif->UserData);
	const std::size_t n = std::min(static_cast<std::size_t>(std::max(len, 0)), r->data.size() - r->pos);
	std::memcpy(buf, r->data.data() + r->pos, n);
	r->pos += n;
	return static_cast<int>(n);
}

struct GifCloser {
	void operator()(GifFileType *gif) const
	{
		int error;
		DGifCloseFile(gif, &error);
	}
};

std::vector<std::uint8_t> palette_of(const ColorMapObject *map)
{
	std::vector<std::uint8_t> palette;
	if (!map || map->ColorCount <= 0 || map->ColorCount > 256)
		return palette;
	for (int i = 0; i < map->ColorCount; i++) {
		palette.push_back(map->Colors[i].Red);
		palette.push_back(map->Colors[i].Green);
		palette.push_back(map->Colors[i].Blue);
	}
	return palette;
}

} // namespace

Picture decode_gif(std::span<const std::uint8_t> data)
{
	Reader reader{data};
	int error = 0;
	std::unique_ptr<GifFileType, GifCloser> gif(DGifOpen(&reader, read_input, &error));
	if (!gif)
		throw DecodeError(GifErrorString(error) ? GifErrorString(error) : "invalid GIF");

	const int width = gif->SWidth, height = gif->SHeight;
	checked_pixels(width, height);

	std::array<std::uint8_t, 3> background{};
	const auto global = palette_of(gif->SColorMap);
	if (static_cast<std::size_t>(gif->SBackGroundColor) * 3 + 2 < global.size())
		std::copy_n(&global[static_cast<std::size_t>(gif->SBackGroundColor) * 3], 3, background.begin());

	std::vector<Frame> frames;
	std::size_t frame_bytes = 0;
	unsigned loops = 1;
	int transparent = -1, disposal = 0, delay = 0;
	bool truncated = false;
	std::vector<GifByteType> line;

	for (bool done = false; !done && !truncated;) {
		GifRecordType type;
		if (DGifGetRecordType(gif.get(), &type) == GIF_ERROR) {
			truncated = true;
			break;
		}

		switch (type) {
		case EXTENSION_RECORD_TYPE: {
			int code;
			GifByteType *ext;
			if (DGifGetExtension(gif.get(), &code, &ext) == GIF_ERROR) {
				truncated = true;
				break;
			}
			if (code == GRAPHICS_EXT_FUNC_CODE && ext && ext[0] >= 4) {
				transparent = (ext[1] & 1) ? ext[4] : -1;
				disposal = (ext[1] >> 2) & 7;
				delay = ext[2] | ext[3] << 8;
			} else if (code == APPLICATION_EXT_FUNC_CODE && ext && ext[0] == 11 &&
				   (!std::memcmp(ext + 1, "NETSCAPE2.0", 11) ||
				    !std::memcmp(ext + 1, "ANIMEXTS1.0", 11))) {
				if (DGifGetExtensionNext(gif.get(), &ext) == GIF_ERROR) {
					truncated = true;
					break;
				}
				if (ext && ext[0] >= 3 && ext[1] == 1) {
					const unsigned count = ext[2] | ext[3] << 8;
					loops = count ? count + 1 : 0;
				}
			}
			while (ext)
				if (DGifGetExtensionNext(gif.get(), &ext) == GIF_ERROR) {
					truncated = true;
					break;
				}
			break;
		}

		case IMAGE_DESC_RECORD_TYPE: {
			if (DGifGetImageDesc(gif.get()) == GIF_ERROR) {
				truncated = true;
				break;
			}
			const GifImageDesc &desc = gif->Image;
			if (desc.Width <= 0 || desc.Height <= 0 || desc.Left < 0 || desc.Top < 0 ||
			    desc.Width > max_dimension || desc.Height > max_dimension)
				throw DecodeError("invalid GIF frame");

			Frame f;
			f.left = std::min(desc.Left, width);
			f.top = std::min(desc.Top, height);
			f.width = std::min(desc.Left + desc.Width, width) - f.left;
			f.height = std::min(desc.Top + desc.Height, height) - f.top;
			const auto area = static_cast<std::size_t>(f.width) * static_cast<std::size_t>(f.height);
			if (frames.size() == max_frames || frame_bytes + area > max_frame_bytes) {
				done = true;
				break;
			}
			frame_bytes += area;
			f.indices.resize(area);
			f.row_valid.resize(static_cast<std::size_t>(f.height));
			f.palette = palette_of(desc.ColorMap ? desc.ColorMap : gif->SColorMap);
			f.transparent = transparent;
			f.disposal = disposal;
			f.delay = delay;

			static constexpr int start[] = {0, 4, 2, 1}, step[] = {8, 8, 4, 2};
			const int passes = desc.Interlace ? 4 : 1;
			line.resize(static_cast<std::size_t>(desc.Width));
			for (int pass = 0; pass < passes && !truncated; pass++) {
				const int first = desc.Interlace ? start[pass] : 0;
				const int inc = desc.Interlace ? step[pass] : 1;
				for (int y = first; y < desc.Height; y += inc) {
					if (DGifGetLine(gif.get(), line.data(), desc.Width) == GIF_ERROR) {
						truncated = true;
						break;
					}
					const int row = desc.Top + y - f.top;
					if (row < 0 || row >= f.height || f.width <= 0)
						continue;
					std::copy_n(line.begin() + (f.left - desc.Left), f.width,
						    f.indices.begin() + static_cast<std::ptrdiff_t>(row) * f.width);
					f.row_valid[static_cast<std::size_t>(row)] = 1;
				}
			}
			frames.push_back(std::move(f));
			transparent = -1;
			disposal = 0;
			delay = 0;
			break;
		}

		case TERMINATE_RECORD_TYPE:
			done = true;
			break;

		default:
			break;
		}
	}

	if (frames.empty())
		throw DecodeError("GIF has no images");

	const Frame &first = frames.front();
	const bool alpha = truncated || first.left || first.top || first.width != width ||
			   first.height != height ||
			   std::any_of(frames.begin(), frames.end(), [](const Frame &f) {
				   return f.transparent >= 0 || f.disposal == 2;
			   });

	Picture pic;
	auto anim = std::make_unique<GifAnimation>(width, height, background, std::move(frames), loops, alpha);
	anim->current(pic.image);
	if (anim->delay().count())
		pic.animation = std::move(anim);
	return pic;
}

} // namespace fbvng
