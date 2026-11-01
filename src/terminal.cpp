// SPDX-License-Identifier: GPL-2.0-or-later
#include "terminal.hpp"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <system_error>
#include <termios.h>
#include <unistd.h>

namespace fbvng {

volatile std::sig_atomic_t quit_requested = 0;
volatile std::sig_atomic_t vt_active = 1;
volatile std::sig_atomic_t redraw_requested = 0;

namespace {

// Saved state, restored by the destructor or a fatal signal handler.
termios saved_termios;
bool termios_saved = false;
vt_mode saved_vt_mode;
bool vt_mode_saved = false;
bool cursor_hidden = false;
int console_fd = -1;
int saved_kd_mode;
bool kd_mode_saved = false;

constexpr char show_cursor[] = "\033[?25h";
constexpr char hide_cursor[] = "\033[?25l";

void write_all(const char *s, std::size_t n)
{
	while (n) {
		const ssize_t w = ::write(STDOUT_FILENO, s, n);
		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return;
		s += w;
		n -= static_cast<std::size_t>(w);
	}
}

// Async-signal-safe.
void restore()
{
	if (vt_mode_saved)
		::ioctl(STDIN_FILENO, VT_SETMODE, &saved_vt_mode);
	if (termios_saved)
		::tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
	if (cursor_hidden)
		write_all(show_cursor, sizeof(show_cursor) - 1);
	if (kd_mode_saved)
		::ioctl(console_fd, KDSETMODE, saved_kd_mode);
}

void on_quit(int sig) { quit_requested = sig; }

void on_fatal(int sig)
{
	restore();
	::signal(sig, SIG_DFL);
	::raise(sig);
}

void on_vt_release(int)
{
	vt_active = 0;
	::ioctl(STDIN_FILENO, VT_RELDISP, 1);
}

void on_vt_acquire(int)
{
	::ioctl(STDIN_FILENO, VT_RELDISP, VT_ACKACQ);
	vt_active = 1;
	redraw_requested = 1;
}

void install(int sig, void (*handler)(int))
{
	struct sigaction sa {};
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	// No SA_RESTART: waits return early so the viewer notices the flags.
	sa.sa_flags = 0;
	::sigaction(sig, &sa, nullptr);
}

} // namespace

Terminal::Terminal(bool hide, bool graphics)
	: interactive_(::isatty(STDIN_FILENO))
{
	// First, so a failure leaves nothing else to undo.
	if (graphics) {
		console_fd = ::open("/dev/tty0", O_RDWR | O_CLOEXEC | O_NOCTTY);
		if (console_fd < 0 || ::ioctl(console_fd, KDGETMODE, &saved_kd_mode) < 0 ||
		    ::ioctl(console_fd, KDSETMODE, KD_GRAPHICS) < 0) {
			const int err = errno;
			if (console_fd >= 0)
				::close(console_fd);
			console_fd = -1;
			throw std::system_error(err, std::generic_category(), "graphics mode on /dev/tty0");
		}
		kd_mode_saved = true;
	}

	for (int sig : {SIGINT, SIGTERM, SIGHUP, SIGQUIT})
		install(sig, on_quit);
	for (int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL})
		install(sig, on_fatal);

	if (interactive_ && ::tcgetattr(STDIN_FILENO, &saved_termios) == 0) {
		termios raw = saved_termios;
		raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON);
		raw.c_cc[VMIN] = 1;
		raw.c_cc[VTIME] = 0;
		termios_saved = ::tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
	}

	// Release and acquire the console on VT switches, so nothing is
	// drawn while another VT is shown.
	vt_mode mode{};
	if (::ioctl(STDIN_FILENO, VT_GETMODE, &mode) == 0) {
		saved_vt_mode = mode;
		install(SIGUSR1, on_vt_release);
		install(SIGUSR2, on_vt_acquire);
		mode.mode = VT_PROCESS;
		mode.relsig = SIGUSR1;
		mode.acqsig = SIGUSR2;
		vt_mode_saved = ::ioctl(STDIN_FILENO, VT_SETMODE, &mode) == 0;
	}

	if (hide) {
		write_all(hide_cursor, sizeof(hide_cursor) - 1);
		cursor_hidden = true;
	}
}

Terminal::~Terminal()
{
	restore();
	vt_mode_saved = termios_saved = cursor_hidden = kd_mode_saved = false;
	if (console_fd >= 0)
		::close(console_fd);
	console_fd = -1;
}

void Terminal::retain_graphics()
{
	kd_mode_saved = false;
}

std::optional<int> Terminal::read_key(std::optional<std::chrono::milliseconds> timeout)
{
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);
	timeval tv{}, *ptv = nullptr;
	if (timeout) {
		const auto ms = std::max<std::chrono::milliseconds::rep>(timeout->count(), 0);
		tv.tv_sec = static_cast<time_t>(ms / 1000);
		tv.tv_usec = static_cast<suseconds_t>(ms % 1000 * 1000);
		ptv = &tv;
	}
	if (::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, ptv) <= 0)
		return std::nullopt;

	unsigned char c;
	const ssize_t n = ::read(STDIN_FILENO, &c, 1);
	if (n == 1)
		return c;
	if (n == 0)
		return -1;
	return std::nullopt;
}

void Terminal::sleep(std::chrono::milliseconds duration)
{
	const auto ms = std::max<std::chrono::milliseconds::rep>(duration.count(), 0);
	timespec ts{static_cast<time_t>(ms / 1000), static_cast<long>(ms % 1000 * 1000000)};
	::nanosleep(&ts, nullptr);
}

} // namespace fbvng
