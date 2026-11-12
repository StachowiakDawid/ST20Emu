#ifndef DSR9500_FRONTEND_H
#define DSR9500_FRONTEND_H

#include "../soc/common/STi55xx_Ssc.h"

#include <cstdint>
#include <string>
#include <string_view>

/*
 * Samsung DSR9500 satellite front end: the HM1221 QPSK demodulator at I2C 0xaa (SSC1) with
 * the tuner PLL it programs through its 3-wire port (registers 0x6d/0x72), the LNB supply
 * and 22 kHz tone switched by the board latch at 0x50400000, and the transponders "on air"
 * (`--transponder`). A locked demodulator delivers its transponder's transport stream to the
 * STi5512 PTI at the channel's useful bit rate. The register behaviour is documented in
 * Dsr9500Frontend.cpp and src/board/README.md.
 */

/* parses and loads FILE.ts:FREQ_MHZ:SR_KSPS:H|V[:LO|HI][:FEC] (FEC 1/2 ... 7/8, default 3/4);
 * returns an error message or "" */
std::string dsr9500AddTransponder(std::string_view spec);
/* the demodulator's I2C register file with its chip behaviour attached */
I2cRegisterDevice &dsr9500QpskDevice();
/* resets the chip and LNB state and registers the board latches (0x50200000, 0x50400000) */
int initDsr9500Frontend();
/* delivers the transport packets due since the last call (called from the board poller) */
void pollDsr9500Frontend();
/* one-line status: LNB, PLL frequency, symbol rate, lock, delivered packets */
std::string dsr9500FrontendStatus();
/* for tests: lock state as the firmware reads it (register 8 bits 5-6 == 0x60) */
bool dsr9500FrontendLocked();
uint64_t dsr9500FrontendPackets();

#endif
