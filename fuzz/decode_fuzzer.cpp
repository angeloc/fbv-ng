// SPDX-License-Identifier: GPL-2.0-or-later
#include "decode.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
	try {
		auto pic = fbvng::decode({data, size});
		// Play a few frames to exercise compositing and disposal.
		for (int i = 0; i < 64 && pic.animation && pic.animation->delay().count(); i++)
			pic.animation->next(pic.image);
	} catch (const fbvng::DecodeError &) {
	}
	return 0;
}
