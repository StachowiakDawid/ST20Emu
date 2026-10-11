#ifndef STI5512_DEVICES_H
#define STI5512_DEVICES_H

#include <cstdint>

int initSti5512Devices(uint8_t revision = 0);
void shutdownSti5512Devices();

#endif