#ifndef STI5512_DEVICES_H
#define STI5512_DEVICES_H

#include <cstdint>
#include <vector>

struct I2cRegisterDevice;

int initSti5512Devices(uint8_t revision = 0);
void shutdownSti5512Devices();
int pollSti5512Devices();
/* bytes written to ASC0-3 (port 0-3) since the last call */
std::vector<uint8_t> takeSti5512AscTransmit(int port);
/* connects an I2C slave to the bus of SSC0 or SSC1 (port 0-1); the device must outlive the bus */
int attachSti5512I2cDevice(int port, I2cRegisterDevice *device);
/* called before the SoC peripherals on every poll, so board parts can drive pins */
using BoardPoller = void (*)();
void setSti5512BoardPoller(BoardPoller poller);

#endif
