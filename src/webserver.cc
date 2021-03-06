// Copyright (c) 2016 Kumina, https://kumina.nl/
//
// This file is distributed under a 2-clause BSD license.
// See the LICENSE file for details.

#include <sys/socket.h>
#include <sys/uio.h>

#include <netinet/in.h>

#include <unistd.h>

#include <cassert>
#include <sstream>
#include <string>

#include "webserver.h"
#include "webserver_request_handler.h"

Webserver::~Webserver() {
  if (fd_ >= 0)
    close(fd_);
}

void Webserver::BindAndListen(std::uint16_t port) {
  // Create socket.
  assert(fd_ == -1 && "Webserver is already listening.");
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd_ >= 0 && "Failed to create socket.");

  // Allow binding if there are still connections in TIME_WAIT.
  {
    int set = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, (void*)&set, sizeof(int));
  }

  // Bind socket.
  {
    struct sockaddr_in sin = {};
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(port);
    int ret = bind(fd_, (struct sockaddr*)&sin, sizeof(sin));
    assert(ret == 0 && "Failed to bind socket.");
  }

  // Listen for incoming connections.
  {
    int ret = listen(fd_, 0);
    assert(ret == 0 && "Failed to listen on socket.");
  }
}

void Webserver::Dispatch() {
  // Accept new incoming connection.
  assert(fd_ >= 0 && "Cannot dispatch on unbound socket.");
  int conn = accept(fd_, nullptr, nullptr);
  assert(conn >= 0 && "Failed to accept incoming connection.");

  // Disable signalling on socket, so we don't get SIGPIPE.
  {
    int set = 1;
#ifdef MSG_NOSIGNAL
    setsockopt(conn, SOL_SOCKET, MSG_NOSIGNAL, (void*)&set, sizeof(int));
#else
    setsockopt(conn, SOL_SOCKET, SO_NOSIGPIPE, (void*)&set, sizeof(int));
#endif
  }

  {
    // Compute response body.
    std::ostringstream body;
    handler_->HandleRequest(&body);
    std::string body_str = body.str();

    // Compute response headers.
    std::ostringstream headers;
    headers << "HTTP/1.1 200 OK\r\n"
            << "Connection: close\r\n"
            << "Content-Length: " << body_str.size() << "\r\n"
            << "Content-Type: text/plain\r\n"
            << "\r\n";
    std::string headers_str = headers.str();

    // Send response headers and body over the socket.
    struct iovec iov[2];
    iov[0].iov_base = (void*)headers_str.data();
    iov[0].iov_len = headers_str.size();
    iov[1].iov_base = (void*)body_str.data();
    iov[1].iov_len = body_str.size();
    writev(conn, iov, 2);
  }

  // Initiate shutdown and drain until the peer has disconnected.
  shutdown(conn, SHUT_WR);
  char discard[1024];
  while (read(conn, discard, sizeof(discard)) > 0) {
  }
  close(conn);
}
