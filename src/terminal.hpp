// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <chrono>
#include <csignal>
#include <optional>

namespace fbvng {

// Set from signal handlers; quit_requested holds the signal number.
extern volatile std::sig_atomic_t quit_requested;
extern volatile std::sig_atomic_t vt_active;
extern volatile std::sig_atomic_t redraw_requested;

// Console state for the lifetime of the viewer: raw keyboard input, hidden
// cursor and VT switch handling, restored on destruction and on fatal
// signals.
class Terminal {
public:
	explicit Terminal(bool hide_cursor);
	~Terminal();
	Terminal(const Terminal &) = delete;
	Terminal &operator=(const Terminal &) = delete;

	// stdin is a terminal we can read keys from
	bool interactive() const { return interactive_; }

	// Waits for a key up to timeout, forever without one. Returns the
	// key, -1 at end of input, nothing on timeout or signal.
	std::optional<int> read_key(std::optional<std::chrono::milliseconds> timeout);

	// Sleeps up to duration, returns early on signals.
	static void sleep(std::chrono::milliseconds duration);

private:
	bool interactive_;
};

} // namespace fbvng
