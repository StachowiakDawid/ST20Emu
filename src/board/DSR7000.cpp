#include "DSR7000.h"
#include "AmdFlash.h"
#include "BitBangI2c.h"
#include "Board.h"
#include "DSR9500.h"

#include "../core/memory/memory.h"
#include "../soc/sti5512/STi5512_Devices.h"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string_view>
#include <utility>

/*
 * Samsung DSR7000 (STi5512, OpenTV EN2). The firmware (dump v1.14, decompiled in
 * Decompilation/DSR7000) is linked for SDRAM at 0x40000000 (static link 0x40001000, the
 * loader's RAM text at 0x403dc4c4), not 0xc0000000 as on the DSR9500.
 *
 * Flash: 4 MB x16 at 0x7fc00000. The loader's flash_detect (0x403deb94) reads only the
 * autoselect manufacturer word and accepts 1 (AMD), 4 (Fujitsu) and 0xec (Samsung) with a
 * 70-sector table, or 0xbf (SST); an AMD Am29LV320DT (0x0001, 0x22f6) is modelled.
 *
 * Front panel and remote (main application front/IR driver 0x7fc33088, decompiled in
 * src/a_7fc33088.c; the loader's fp_* code 0x403de100 drives the same pins): the DSR9500's
 * 16-bit shift register with data on PIO4 bit 0, clock PIO4 bit 1, latch PIO3 bit 5 and the
 * key return line on PIO3 bit 4. front_read_key maps columns 1, 2, 4, 8, 0x80 to keys 3, 8,
 * 6, 5, 7, and front_key those to codes 0x301, 0x300, 0x501, 0x500 and 0x100 (volume up/down,
 * channel up/down, power: the control task 0x7fc14084 dispatches them). The IR
 * receiver is on PIO3 bit 3 and PWM CaptureIn0 (ir_gpio_init/ir_init); ir_pulse accepts NEC
 * frames with customer code 0x0e86 ("Rimocon Custom Code" in the RCU command) and ir_key maps
 * commands to UI key codes, named by the KEY* test commands (0x7fc3467c) and the control
 * task's handler (f_7fc150d4: "ch menu" 0x602, "Ch Guide" 0x600/0x801, channel 0x501/0x500,
 * volume 0x301/0x300, mute 0x302): 11 up (0xb), 12 down (0xa), 27 left (8), 28 right (9),
 * 21 ok (0xd), 20 exit (0xc), 17 menu (0x602), 18 power (0x100), 19 guide (0x600),
 * 10 guide2 (0x801), 16/26 channel up/down, 13/14 volume up/down, 31 mute, 30 info (2).
 *
 * System clock: ir_init sets the capture prescaler to 29 and ir_pulse's windows (NEC header
 * 0x51a7-0x6993 ticks for 13.5 ms, bit threshold 0xb15 between 1.12 and 2.25 ms) fit a
 * 50 MHz system clock (1.724 ticks/us); at the emulated 60.75 MHz every header is rejected.
 * The capture counter is therefore clocked at 50 MHz.
 */
namespace {
constexpr uint32_t sdramStart = 0x40000000U;
constexpr uint32_t sdramEnd = 0x407fffffU;
constexpr uint64_t systemClockHz = 50000000;

/* ASCII 0x20-0x7f -> segments, from the firmware's digit table g_4835 (ROM 0x7fd4d754):
 * 0-9 A b C d E F - blank U L P n n u o */
constexpr std::array<uint8_t, 96> segments{
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xbf, 0xff, 0xff,
    0xc0, 0xf9, 0xa4, 0xb0, 0x99, 0x92, 0x82, 0xf8, 0x80, 0x90, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x88, 0x83, 0xc6, 0xa1, 0x86, 0x8e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc7, 0xff, 0xc8, 0xff,
    0x8c, 0xff, 0xff, 0xff, 0xff, 0xc1, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xab, 0xa3,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xe3, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

constexpr std::pair<std::string_view, int> irKeys[] = {
    {"0", 0}, {"1", 1}, {"2", 2}, {"3", 3}, {"4", 4}, {"5", 5}, {"6", 6}, {"7", 7}, {"8", 8}, {"9", 9},
    {"guide2", 10}, {"up", 11}, {"down", 12}, {"volup", 13}, {"voldown", 14}, {"chup", 16}, {"menu", 17},
    {"power", 18}, {"guide", 19}, {"exit", 20}, {"ok", 21}, {"chdown", 26}, {"left", 27}, {"right", 28},
    {"info", 30}, {"mute", 31}};

constexpr std::pair<std::string_view, int> panelKeys[] = {
    {"volup", 0x01}, {"voldown", 0x02}, {"chup", 0x04}, {"chdown", 0x08}, {"power", 0x80}};

/* I2C: no SSC; the main application's driver (0x7fc20918) and the loader's bit-bang two
 * open-drain PIO lines, SDA bit 0 and SCL bit 2, of PIO1 for bus 0 ("front") and PIO3 for bus
 * 1 (the tuner's; open parameters at ROM 0x7fd3b770/0x7fd3b78c: SSC base, PIO port, ...). Bus
 * 0 carries the 16 KB serial EEPROM (24C128 at 0xa2, 2-byte offsets; the NVM layer
 * 0x7fc21178 rejects offsets from 0x3fc1) and the RF modulator (0xca, 0x7fc3299c); bus 1 the
 * LSI 64724 QPSK link IC (0x1c, 0x7fc5c9dc) and the HM1221 demodulator (0xaa, 0x7fc55efc).
 * Without the EEPROM the control task aborts ("CTRLTASK: Could not access EEPROM file
 * system"). The EEPROM starts erased (0xff) and is not saved. */
constexpr std::size_t eepromSize = 0x4000;
I2cRegisterDevice eeprom{0xa2};
I2cRegisterDevice rfModulator{0xca};
I2cRegisterDevice linkIc{0x1c};
I2cRegisterDevice demodulator{0xaa};
BitBangI2c frontBus = [] {
  BitBangI2c bus{1, 0x01, 0x04};
  bus.attach(&eeprom);
  bus.attach(&rfModulator);
  return bus;
}();
BitBangI2c tunerBus = [] {
  BitBangI2c bus{3, 0x01, 0x04};
  bus.attach(&linkIc);
  bus.attach(&demodulator);
  return bus;
}();

void observeI2c(int port, uint8_t out) {
  if (port == frontBus.port()) frontBus.observe(out);
  if (port == tunerBus.port()) tunerBus.observe(out);
}

uint8_t inputI2c(int port, uint8_t levels) {
  if (port == frontBus.port()) levels = frontBus.input(levels);
  if (port == tunerBus.port()) levels = tunerBus.input(levels);
  return levels;
}

const SamsungPanelConfig panel{
    .latchPort = 3, .latchBit = 0x20,
    .shiftPort = 4, .dataBit = 0x01, .clockBit = 0x02,
    .keyReturnPort = 3, .keyReturnBit = 0x10,
    .irPort = 3, .irBit = 0x08,
    .necCustomer = 0x0e86,
    .segments = segments,
    .irKeys = irKeys,
    .panelKeys = panelKeys,
    .observeExtra = observeI2c,
    .inputExtra = inputI2c,
};
}

int initDsr7000Board() {
  /* unwritten SDRAM reads as zero: the loader's mailbox_take (0x403dde6b) reads the
   * warm-restart mailbox at 0x40000000 before anything has written it */
  setZeroFillRange(sdramStart, sdramEnd);
  initSamsungPanel(panel);
  sti5512Peripherals().pwm.systemClockHz = systemClockHz;
  setActiveBoard(Board::Dsr7000);
  for (I2cRegisterDevice *chip : {&eeprom, &rfModulator, &linkIc, &demodulator}) resetI2cDevice(*chip);
  eeprom.memory.assign(eepromSize, 0xff);
  frontBus.reset();
  tunerBus.reset();
  return initAmdFlash({0x7fc00000U, 0x400000U, 0x0001, 0x22f6});
}
