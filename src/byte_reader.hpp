// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "image.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace fbvng {

// Bounds-checked little-endian access to a byte buffer.
class ByteReader {
public:
	explicit ByteReader(std::span<const std::uint8_t> data) : data_(data) {}

	std::size_t size() const { return data_.size(); }

	std::span<const std::uint8_t> bytes(std::size_t offset, std::size_t count) const
	{
		if (offset > data_.size() || count > data_.size() - offset)
			throw DecodeError("truncated file");
		return data_.subspan(offset, count);
	}

	std::uint8_t u8(std::size_t offset) const { return bytes(offset, 1)[0]; }

	std::uint16_t le16(std::size_t offset) const
	{
		const auto b = bytes(offset, 2);
		return static_cast<std::uint16_t>(b[0] | b[1] << 8);
	}

	std::uint32_t le32(std::size_t offset) const
	{
		const auto b = bytes(offset, 4);
		return static_cast<std::uint32_t>(b[0]) |
		       static_cast<std::uint32_t>(b[1]) << 8 |
		       static_cast<std::uint32_t>(b[2]) << 16 |
		       static_cast<std::uint32_t>(b[3]) << 24;
	}

	std::int32_t sle32(std::size_t offset) const
	{
		return static_cast<std::int32_t>(le32(offset));
	}

private:
	std::span<const std::uint8_t> data_;
};

} // namespace fbvng
