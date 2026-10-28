#ifndef STI5518_DEVICES_H
#define STI5518_DEVICES_H

#include "../common/STi55xx_Peripherals.h"

/* PIO ports 0-4 (0x2000c000 + port * 0x1000) and PIO5 (0x2000a100) */
inline constexpr int sti5518PioPorts = sti55xxPioPorts + 1;

int initSti5518Devices();
void shutdownSti5518Devices();
int pollSti5518Devices();
/* the shared peripherals, through which board parts connect to the SoC */
Sti55xxPeripherals &sti5518Peripherals();

#endif
