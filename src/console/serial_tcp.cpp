#include "serial_tcp.h"

#include "../common/compat.h"

#include <charconv>
#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace serialtcp {

namespace {

int listener = -1;
int client = -1;
uint16_t listenPort = 0;

void dropClient() {
  if (client >= 0) {
    ::close(client);
    client = -1;
    compat::println(stderr, "serial-tcp: client disconnected");
  }
}

} // namespace

bool parseSpec(std::string_view spec, int &ascPort, uint16_t &tcpPort) {
  if (spec.size() < 3 || spec[0] < '0' || spec[0] > '3' || spec[1] != ':') return false;
  std::string_view number = spec.substr(2);
  unsigned value = 0;
  auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), value);
  if (error != std::errc{} || end != number.data() + number.size() || value > 65535) return false;
  ascPort = spec[0] - '0';
  tcpPort = static_cast<uint16_t>(value);
  return true;
}

bool listen(uint16_t tcpPort) {
  listener = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) {
    compat::println(stderr, "serial-tcp: socket: {}", std::strerror(errno));
    return false;
  }
  int one = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(tcpPort);
  socklen_t length = sizeof(address);
  if (::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
      ::listen(listener, 1) != 0 ||
      getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
    compat::println(stderr, "serial-tcp: port {}: {}", tcpPort, std::strerror(errno));
    ::close(listener);
    listener = -1;
    return false;
  }
  fcntl(listener, F_SETFL, fcntl(listener, F_GETFL) | O_NONBLOCK);
  listenPort = ntohs(address.sin_port);
  return true;
}

uint16_t boundPort() { return listener >= 0 ? listenPort : 0; }

bool connected() { return client >= 0; }

bool send(const std::vector<uint8_t> &bytes) {
  if (client < 0) return false;
  std::size_t done = 0;
  while (done < bytes.size()) {
    #ifdef MSG_NOSIGNAL
    ssize_t count = ::send(client, bytes.data() + done, bytes.size() - done, MSG_NOSIGNAL);
#else
    ssize_t count = ::send(client, bytes.data() + done, bytes.size() - done, 0);
#endif
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) {
      dropClient(); // the rest is lost, as on a line whose far end went away
      return true;
    }
    done += static_cast<std::size_t>(count);
  }
  return true;
}

void poll(std::vector<uint8_t> &received) {
  if (listener < 0) return;
  if (client < 0) {
    client = ::accept(listener, nullptr, nullptr);
    if (client < 0) return;
    int one = 1;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
    setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    compat::println(stderr, "serial-tcp: client connected on port {}", listenPort);
  }
  for (;;) {
    pollfd descriptor{client, POLLIN, 0};
    if (::poll(&descriptor, 1, 0) <= 0) return;
    uint8_t buffer[4096];
    ssize_t count = ::recv(client, buffer, sizeof(buffer), 0);
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) return;
    if (count <= 0) {
      dropClient();
      return;
    }
    received.insert(received.end(), buffer, buffer + count);
  }
}

void close() {
  dropClient();
  if (listener >= 0) ::close(listener);
  listener = -1;
}

} // namespace serialtcp
