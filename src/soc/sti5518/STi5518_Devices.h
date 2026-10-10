#ifndef STI5518_DEVICES_H
#define STI5518_DEVICES_H

#include <cstdint>
#include <vector>

int initSti5518Devices();
void shutdownSti5518Devices();
int pollSti5518Devices();
std::vector<uint8_t> takeSti5518Asc1Transmit();
std::vector<uint8_t> takeSti5518Asc2Transmit();

#endif