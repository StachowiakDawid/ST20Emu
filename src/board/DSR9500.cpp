#include "DSR9500.h"

#include "../soc/common/STi55xx_Ssc.h"
#include "../soc/sti5512/STi5512_Devices.h"

#include <initializer_list>

/*
 * The firmware's STI2C setup (main application 0x7fe763ff) puts the tuner bus "back" on
 * SSC1 and opens the QPSK demodulator at bus address 0xaa (open parameters at ROM
 * 0x7ff2acd8). qpsk_reset (0x7fe81f3f) accepts the chip when register 0 reads 0x5a.
 * The bus "front" on SSC0 carries the CI controller (0x80, ROM 0x7ff2ace4) and the RF
 * modulator (0xca, ROM 0x7ff2acf0). All three chips are plain register files that acknowledge
 * their address: there is no tuner, signal, lock, CI module or RF output model.
 */
namespace {
constexpr int frontBus = 0;
constexpr int tunerBus = 1;
constexpr uint8_t qpskIdentity = 0x5a;
I2cRegisterDevice qpskDemodulator{0xaa};
I2cRegisterDevice ciController{0x80};
I2cRegisterDevice rfModulator{0xca};

void resetChip(I2cRegisterDevice &chip) {
  chip.registers = {};
  chip.pointer = 0;
  chip.pointerWritten = false;
}
}

int initDsr9500Board() {
  for (I2cRegisterDevice *chip : {&qpskDemodulator, &ciController, &rfModulator}) resetChip(*chip);
  qpskDemodulator.registers[0] = qpskIdentity;
  int result = attachSti5512I2cDevice(tunerBus, &qpskDemodulator);
  if (!result) result = attachSti5512I2cDevice(frontBus, &ciController);
  if (!result) result = attachSti5512I2cDevice(frontBus, &rfModulator);
  return result;
}
