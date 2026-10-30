// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal assertions for the test programs.
#pragma once

#include <cstdio>
#include <cstdlib>

inline int check_failures = 0;

#define CHECK(cond)                                                                \
	do {                                                                       \
		if (!(cond)) {                                                     \
			std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__,  \
				     __LINE__, #cond);                             \
			check_failures++;                                          \
		}                                                                  \
	} while (0)

#define CHECK_THROWS(type, expr)                                                   \
	do {                                                                       \
		bool thrown = false;                                               \
		try {                                                              \
			(void)(expr);                                              \
		} catch (const type &) {                                           \
			thrown = true;                                             \
		}                                                                  \
		if (!thrown) {                                                     \
			std::fprintf(stderr, "%s:%d: %s did not throw %s\n",        \
				     __FILE__, __LINE__, #expr, #type);            \
			check_failures++;                                          \
		}                                                                  \
	} while (0)

inline int check_result()
{
	if (check_failures)
		std::fprintf(stderr, "%d check(s) failed\n", check_failures);
	return check_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
