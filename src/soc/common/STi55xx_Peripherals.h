#ifndef STI55XX_PERIPHERALS_H
#define STI55XX_PERIPHERALS_H

#include "../../core/memory/memory.h"
#include "STi55xx_Asc.h"
#include "STi55xx_Inc.h"
#include "STi55xx_Pio.h"
#include "STi55xx_Pwm.h"
#include "STi55xx_Ssc.h"

#include <array>
#include <cstdint>
#include <vector>

/* called before the SoC peripherals on every poll, so board parts can drive pins */
using BoardPoller = void (*)();

/* PIO ports 0-4 (PIOBaseAddress 0x2000c000 + port * 0x1000) */
inline constexpr int sti55xxPioPorts = 5;

/*
 * Peripherals the STi5512 and STi5518 place at the same addresses and interrupt sources
 * (STi5512 datasheet 7145560 B Table 50, STi5518 datasheet 7170179 D Table 34): ASC0-3
 * (sources 10, 9, 8, 7), SSC0/1 (5, 6), PWM (11), PIO0-4 and the interrupt controller at
 * 0x20000000. Each SoC adds its own devices, the interrupt level controller and its polling.
 * PORT arguments are ASC 0-3, SSC 0-1 or PIO port numbers.
 */
struct Sti55xxPeripherals {
  static constexpr uint32_t pwmSource = 11;

  std::array<AscDevice, 4> asc{{{0x20003000U, 10}, {0x20004000U, 9}, {0x20005000U, 8}, {0x20006000U, 7}}};
  std::array<SscDevice, 2> ssc{{{0x20009000U, 5}, {0x2000a000U, 6}}};
  std::vector<PioPort> pio;
  PwmDevice pwm{0x2000b000U};
  InterruptController controller;
  BoardPoller boardPoller = nullptr;
  std::vector<uint32_t> registeredBases{};

  /* EXTRAPIO follow PIO0-4; CLEARALL is the INC's STi5518 clear behaviour */
  Sti55xxPeripherals(bool clearAll, std::vector<PioPort> extraPio = {});

  /* registers the SoC's DEVICES, then the shared ones; on failure none stays registered */
  int registerDevices(std::vector<MemoryDevice> devices);
  void unregisterDevices();
  void reset();
  /* the ASC and SSC interrupt lines as source bits */
  uint32_t tickSerial();
  /* runs the board poller, then the PWM; returns the PWM source bit */
  uint32_t tickBoardAndPwm();
  /* installs the SoC on the CPU: unwritten shared SDRAM reads as zero, the poller, the
   * interrupt return and reboot observers; clears captured transmit bytes */
  void attach(int (*poller)(), void (*acknowledge)(), int (*reboot)());
  /* removes the SoC and everything boards connected to it */
  void detach();

  /* bytes written to an ASC since the last call; none while a transmit observer is set */
  std::vector<uint8_t> takeAscTransmit(int port);
  void setAscTransmitObserver(int port, void (*observer)(uint8_t byte));
  void feedAscReceive(int port, const std::vector<uint8_t> &bytes);
  /* connects an I2C slave to an SSC's bus; the device must outlive the bus */
  int attachI2cDevice(int port, I2cRegisterDevice *device);
  /* START (true) or STOP (false) that software drove on the pins of an SSC's bus */
  void i2cBusCondition(int port, bool start);
  void setPioObserver(PioOutputObserver observer);
  void setPioInputSource(PioInputSource source);
  uint8_t pioOutput(int port) const;
  /* an edge on PWM pin CaptureIn0-3 */
  void pwmCaptureEdge(int input, bool rising);
};

#endif
