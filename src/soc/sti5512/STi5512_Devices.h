#ifndef STI5512_DEVICES_H
#define STI5512_DEVICES_H

#include "../common/STi55xx_Peripherals.h"

#include <cstdint>

inline constexpr int sti5512PioPorts = sti55xxPioPorts;

int initSti5512Devices(uint8_t revision = 0);
void shutdownSti5512Devices();
int pollSti5512Devices();
/* the shared peripherals, through which board parts connect to the SoC */
Sti55xxPeripherals &sti5512Peripherals();

#endif
