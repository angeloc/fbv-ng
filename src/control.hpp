// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "unique_fd.hpp"

#include <poll.h>
#include <string>
#include <vector>

namespace fbvng {

// One command line received on the control socket.
struct Request {
	int fd;
	std::string verb;
	std::string argument;  // rest of the line, trimmed
};

// Unix stream socket taking one command per line; each command gets one
// reply line. Never blocks: the viewer polls its descriptors.
class ControlServer {
public:
	// Throws std::system_error, or std::runtime_error when another
	// process serves the path.
	explicit ControlServer(std::string path);
	~ControlServer();
	ControlServer(const ControlServer &) = delete;
	ControlServer &operator=(const ControlServer &) = delete;

	void add_fds(std::vector<pollfd> &fds) const;

	// Accepts clients and reads their input after poll().
	std::vector<Request> handle(const std::vector<pollfd> &fds);

	void reply(int fd, const std::string &line);

private:
	struct Client {
		UniqueFd fd;
		std::string buffer;
	};

	void read_client(Client &client, std::vector<Request> &out);

	std::string path_;
	UniqueFd listen_;
	std::vector<Client> clients_;
};

} // namespace fbvng
