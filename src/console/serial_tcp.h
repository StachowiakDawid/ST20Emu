#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

/*
 * --serial-tcp PORT:TCP: ASC PORT's serial line as a TCP server on 127.0.0.1:TCP (one client at
 * a time), e.g. for SLIP between the target and a host program. While a client is connected the
 * port's transmit bytes go to it unchanged and the bytes it sends go to the port's receive queue;
 * without a client the port prints its `ASCn>` lines as usual.
 */

namespace serialtcp {

/* parses "PORT:TCP" (ASC port 0-3, TCP port 0-65535; 0 = any free port) */
bool parseSpec(std::string_view spec, int &ascPort, uint16_t &tcpPort);

/* starts listening; returns false (after printing why) on failure */
bool listen(uint16_t tcpPort);
/* the listening TCP port (useful after listen(0)), 0 when not listening */
uint16_t boundPort();
bool connected();

/* sends the port's transmit bytes to the client; returns false (bytes not taken) without one */
bool send(const std::vector<uint8_t> &bytes);
/* accepts a waiting client and appends what it sent to `received`, without blocking */
void poll(std::vector<uint8_t> &received);
void close();

} // namespace serialtcp
