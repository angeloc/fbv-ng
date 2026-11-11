// SPDX-License-Identifier: GPL-2.0-or-later
// Drives fbv through its control socket on a fake framebuffer.
// Usage: control_test <fbv> <fakefb.so> <data dir>
#include "check.hpp"
#include "decode.hpp"
#include "fake_fb.hpp"

#include <chrono>
#include <fstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>

using namespace fbvng;
using namespace std::chrono_literals;

namespace {

std::string socket_path;

// Sends one command and returns the reply line, empty when unreachable.
std::string send(const std::string &line)
{
	sockaddr_un addr{};
	addr.sun_family = AF_UNIX;
	std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
	const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return {};
	std::string reply;
	if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
		const std::string msg = line + "\n";
		if (::write(fd, msg.data(), msg.size()) == static_cast<ssize_t>(msg.size()))
			for (char c; ::read(fd, &c, 1) == 1 && c != '\n';)
				reply += c;
	}
	::close(fd);
	return reply;
}

// Polls status until it reports the frame, up to a second.
bool wait_frame(std::size_t frame)
{
	const std::string want = "ok frame=" + std::to_string(frame) + " ";
	for (int i = 0; i < 100; i++) {
		if (send("status").rfind(want, 0) == 0)
			return true;
		std::this_thread::sleep_for(10ms);
	}
	std::fprintf(stderr, "frame %zu not reached, status: %s\n", frame, send("status").c_str());
	return false;
}

// The screen shows frame n of the animation once fbv drew it.
void check_screen(const FakeFramebuffer &fb, const Image &expected, const char *what)
{
	std::size_t bad = 0;
	for (int i = 0; i < 50; i++) {
		if (!(bad = fb.diff(expected, false)))
			return;
		std::this_thread::sleep_for(10ms);
	}
	std::fprintf(stderr, "%s: %zu pixels differ\n", what, bad);
	CHECK(bad == 0);
}

Image frame_of(const std::string &file, std::size_t n)
{
	Picture pic = decode_file(file);
	if (n)
		pic.animation->seek(n, pic.image);
	return pic.image;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 4) {
		std::fprintf(stderr, "usage: %s <fbv> <fakefb.so> <data dir>\n", argv[0]);
		return 2;
	}
	const std::string fbv = argv[1], shim = argv[2], data = argv[3];
	const std::string anim = data + "/anim_full.gif", still = data + "/rgb24.bmp";

	char dir[] = "/tmp/fbv-control-XXXXXX";
	if (!::mkdtemp(dir))
		return 2;
	socket_path = std::string(dir) + "/fbv.sock";

	FakeFramebuffer fb(32);
	auto env = fb.environment(shim);
	const std::string blank_log = std::string(dir) + "/blank";
	env.push_back("FAKEFB_BLANK=" + blank_log);
	const pid_t pid = spawn({fbv, "-y", "-i", "-c", "-u", "--control", socket_path, anim}, env);

	bool up = false;
	for (int i = 0; i < 200 && !up; i++) {
		up = send("ping") == "ok";
		if (!up)
			std::this_thread::sleep_for(10ms);
	}
	CHECK(up);

	struct stat st;
	CHECK(::stat(socket_path.c_str(), &st) == 0 && (st.st_mode & 077) == 0);

	// The display is turned on, also without a framebuffer console.
	{
		std::ifstream in(blank_log);
		const std::string levels{std::istreambuf_iterator<char>(in), {}};
		CHECK(levels == "0\n");
	}

	// A second instance must not take over the socket.
	CHECK(wait_exit(spawn({fbv, "-y", "-i", "-c", "-u", "--control", socket_path, anim}, env)) == 1);
	CHECK(send("ping") == "ok");

	CHECK(send("frame 2") == "ok");
	CHECK(send("status") == "ok frame=2 frames=4 paused=0");
	check_screen(fb, frame_of(anim, 2), "frame 2");

	// Progress steps forward at the animation's pace and holds there.
	CHECK(send("progress 100") == "ok");
	CHECK(wait_frame(3));
	std::this_thread::sleep_for(150ms);
	CHECK(send("status") == "ok frame=3 frames=4 paused=0");
	check_screen(fb, frame_of(anim, 3), "progress 100");

	CHECK(send("progress 0") == "ok");
	CHECK(send("status") == "ok frame=0 frames=4 paused=0");
	check_screen(fb, frame_of(anim, 0), "progress 0");
	CHECK(send("progress 50") == "ok");
	CHECK(wait_frame(2));

	CHECK(send("pause") == "ok");
	CHECK(send("status") == "ok frame=2 frames=4 paused=1");
	CHECK(send("resume") == "ok");
	CHECK(wait_frame(3) && wait_frame(0));

	// A loop later every frame comes from the cache, the decoder skips them.
	CHECK(wait_frame(1) && wait_frame(2) && wait_frame(3) && wait_frame(0) && wait_frame(1));
	CHECK(send("pause") == "ok");
	{
		std::size_t frame = 0;
		CHECK(std::sscanf(send("status").c_str(), "ok frame=%zu", &frame) == 1);
		check_screen(fb, frame_of(anim, frame), "cached frame");
	}
	// Seeking rebuilds the frame the decoder skipped to.
	CHECK(send("frame 2") == "ok");
	check_screen(fb, frame_of(anim, 2), "frame 2 after the cache");
	CHECK(send("pause") == "ok");
	CHECK(send("resume") == "ok");

	CHECK(send("frame 4") == "error frame out of range");
	CHECK(send("frame 1.5").rfind("error ", 0) == 0);
	CHECK(send("progress 101").rfind("error ", 0) == 0);
	CHECK(send("progress").rfind("error ", 0) == 0);
	CHECK(send("bogus") == "error unknown command bogus");
	CHECK(send("load " + data + "/missing.png").rfind("error ", 0) == 0);
	CHECK(send("quit now") == "error quit takes only retain");
	CHECK(send("ping") == "ok");

	CHECK(send("load " + still) == "ok");
	CHECK(send("status") == "ok frame=0 frames=1 paused=0");
	CHECK(send("progress 30") == "ok");
	check_screen(fb, frame_of(still, 0), "load");

	CHECK(send("quit") == "ok");
	CHECK(wait_exit(pid) == 0);
	CHECK(::access(socket_path.c_str(), F_OK) != 0);

	// --paused holds the first frame until a command moves the animation.
	const pid_t paused = spawn({fbv, "-y", "-i", "-c", "-u", "-p", "--control", socket_path, anim}, env);
	up = false;
	for (int i = 0; i < 200 && !up; i++) {
		up = send("ping") == "ok";
		if (!up)
			std::this_thread::sleep_for(10ms);
	}
	CHECK(up);
	std::this_thread::sleep_for(300ms);
	CHECK(send("status") == "ok frame=0 frames=4 paused=1");
	check_screen(fb, frame_of(anim, 0), "paused");
	CHECK(send("progress 100") == "ok");
	CHECK(wait_frame(3));
	CHECK(send("quit") == "ok");
	CHECK(wait_exit(paused) == 0);
	::rmdir(dir);
	return check_result();
}
