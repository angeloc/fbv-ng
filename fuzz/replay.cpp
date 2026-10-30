// SPDX-License-Identifier: GPL-2.0-or-later
// Runs the fuzz target on the given files, for compilers without libFuzzer.
#include "decode.hpp"

#include <cstdio>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size);

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		try {
			const auto data = fbvng::read_file(argv[i]);
			LLVMFuzzerTestOneInput(data.data(), data.size());
		} catch (const fbvng::DecodeError &e) {
			std::fprintf(stderr, "%s: %s\n", argv[i], e.what());
			return 1;
		}
	}
	return 0;
}
