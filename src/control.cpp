// SPDX-License-Identifier: GPL-2.0-or-later
#include "control.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>

namespace fbvng {

namespace {

constexpr std::size_t max_clients = 8;
constexpr std::size_t max_line = 4096;

[[noreturn]] void fail(const std::string &what)
{
	throw std::system_error(errno, std::generic_category(), what);
}

sockaddr_un address(const std::string &path)
{
	sockaddr_un addr{};
	addr.sun_family = AF_UNIX;
	if (path.empty() || path.size() >= sizeof(addr.sun_path))
		throw std::runtime_error("invalid control socket path: " + path);
	std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
	return addr;
}

std::string trim(const std::string &s)
{
	const auto first = s.find_first_not_of(" \t\r");
	if (first == std::string::npos)
		return {};
	return s.substr(first, s.find_last_not_of(" \t\r") - first + 1);
}

} // namespace

ControlServer::ControlServer(std::string path) : path_(std::move(path))
{
	const sockaddr_un addr = address(path_);
	const auto *sa = reinterpret_cast<const sockaddr *>(&addr);

	// Replace a stale socket, refuse one somebody still serves.
	struct stat st;
	if (::lstat(path_.c_str(), &st) == 0) {
		if (!S_ISSOCK(st.st_mode))
			throw std::runtime_error(path_ + " exists and is not a socket");
		UniqueFd probe(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
		if (probe && ::connect(probe.get(), sa, sizeof(addr)) == 0)
			throw std::runtime_error(path_ + " is in use by another process");
		::unlink(path_.c_str());
	}

	listen_ = UniqueFd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
	if (!listen_)
		fail("control socket");
	// Only the owner may control the display.
	const mode_t old = ::umask(077);
	const int rc = ::bind(listen_.get(), sa, sizeof(addr));
	::umask(old);
	if (rc < 0)
		fail("bind " + path_);
	if (::listen(listen_.get(), 4) < 0)
		fail("listen " + path_);
}

ControlServer::~ControlServer()
{
	::unlink(path_.c_str());
}

void ControlServer::add_fds(std::vector<pollfd> &fds) const
{
	fds.push_back({listen_.get(), POLLIN, 0});
	for (const auto &c : clients_)
		fds.push_back({c.fd.get(), POLLIN, 0});
}

std::vector<Request> ControlServer::handle(const std::vector<pollfd> &fds)
{
	std::vector<Request> out;
	for (const auto &p : fds) {
		if (!p.revents)
			continue;
		if (p.fd == listen_.get()) {
			UniqueFd fd(::accept4(listen_.get(), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK));
			if (fd && clients_.size() < max_clients)
				clients_.push_back({std::move(fd), {}});
			continue;
		}
		const auto it = std::find_if(clients_.begin(), clients_.end(),
					     [&](const Client &c) { return c.fd.get() == p.fd; });
		if (it != clients_.end())
			read_client(*it, out);
	}
	clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [](const Client &c) { return !c.fd; }),
		       clients_.end());
	return out;
}

void ControlServer::read_client(Client &client, std::vector<Request> &out)
{
	char chunk[1024];
	const ssize_t n = ::read(client.fd.get(), chunk, sizeof(chunk));
	if (n < 0 && (errno == EAGAIN || errno == EINTR))
		return;
	if (n <= 0) {
		client.fd.reset();
		return;
	}
	client.buffer.append(chunk, static_cast<std::size_t>(n));

	for (std::size_t eol; (eol = client.buffer.find('\n')) != std::string::npos;) {
		const std::string line = trim(client.buffer.substr(0, eol));
		client.buffer.erase(0, eol + 1);
		if (line.empty())
			continue;
		const auto space = line.find_first_of(" \t");
		Request req{client.fd.get(), line.substr(0, space), {}};
		if (space != std::string::npos)
			req.argument = trim(line.substr(space));
		out.push_back(std::move(req));
	}
	if (client.buffer.size() > max_line) {
		reply(client.fd.get(), "error line too long");
		client.fd.reset();
	}
}

void ControlServer::reply(int fd, const std::string &line)
{
	const std::string msg = line + "\n";
	// Replies are short; a client that does not read them loses them.
	(void)::send(fd, msg.data(), msg.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
}

} // namespace fbvng
