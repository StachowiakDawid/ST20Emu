#include "STi55xx_Ssc.h"
#include "../../core/memory/memory.h"

/*
 * STi5512 Register Manual (7145562 B) chapter 24; the STi5518's SSC is the same. I2C master
 * transfers: a TBUF write while STRTG is set sends a START and the address frame; data frames
 * follow until STOPG releases the bus. Outside I2C mode a TBUF write in master mode shifts
 * the frame out and RBUF receives the open-drain bus level: what was sent, ANDed with the
 * acknowledge or data of a slave addressed after a START that software generated on the pins
 * (sscBusCondition); with no slave selected that is the frame itself (a NACK). Slave mode, arbitration,
 * clock stretching and 10-bit addressing are not modelled.
 */
namespace {
enum Register { BRG, TBUF, RBUF, CON, IEN, STAT, I2C, SLAD };

constexpr uint32_t statRir = 0x001;
constexpr uint32_t statTir = 0x002;
constexpr uint32_t statStop = 0x080;
constexpr uint32_t statBusy = 0x200;
constexpr uint32_t conSoftReset = 0x080;
constexpr uint32_t conMaster = 0x100;
constexpr uint32_t conEnable = 0x200;
constexpr uint32_t i2cMode = 0x01;
constexpr uint32_t i2cStart = 0x02;
constexpr uint32_t i2cStop = 0x04;
constexpr uint32_t i2cAcknowledge = 0x08;
constexpr uint32_t frameCycles = 2;

int checkAccess(uint32_t address, int width) {
  return (width != 2 && width != 4) || (address & 3U) ? INVALID_DEVICE_ACCESS : 0;
}

void abortTransfer(SscDevice &device) {
  device.busActive = false;
  device.selected = nullptr;
  device.frameDelay = 0;
}

uint32_t transferFrame(SscDevice &device, uint32_t frame) {
  uint8_t byte = static_cast<uint8_t>(frame >> 1);
  if (device.addressFrame) {
    device.addressFrame = false;
    device.selected = nullptr;
    for (I2cRegisterDevice *slave : device.slaves) {
      if (slave->address == (byte & 0xfeU)) device.selected = slave;
    }
    device.reading = (byte & 1U) != 0;
    if (device.selected && !device.reading) device.selected->pointerWritten = false;
    return static_cast<uint32_t>(byte) << 1 | (device.selected ? 0U : 1U);
  }
  I2cRegisterDevice *slave = device.selected;
  if (device.reading) {
    uint8_t data = slave ? slave->registers[slave->pointer++] : 0xff;
    uint32_t acknowledge = device.registers[I2C] & i2cAcknowledge ? 0U : 1U;
    return static_cast<uint32_t>(data) << 1 | acknowledge;
  }
  if (!slave) return static_cast<uint32_t>(byte) << 1 | 1U;
  if (!slave->pointerWritten) {
    slave->pointer = byte;
    slave->pointerWritten = true;
  } else {
    slave->registers[slave->pointer++] = byte;
  }
  return static_cast<uint32_t>(byte) << 1;
}

/* a 9-bit frame on a software-started bus: the master releases the lines it reads */
uint32_t transferRawFrame(SscDevice &device, uint32_t frame) {
  if (device.addressFrame) return transferFrame(device, frame);
  if (device.reading) {
    I2cRegisterDevice *slave = device.selected;
    uint8_t data = slave ? slave->registers[slave->pointer++] : 0xff;
    return static_cast<uint32_t>(data) << 1 | (frame & 1U);
  }
  return transferFrame(device, frame);
}

void writeTransmit(SscDevice &device, uint32_t frame) {
  auto &ssc = device.registers;
  if ((ssc[CON] & (conSoftReset | conEnable)) != conEnable) return;
  if (!(ssc[I2C] & i2cMode)) {
    if (!(ssc[CON] & conMaster)) return;
    ssc[STAT] &= ~statTir;
    device.pendingReceive = device.busActive ? transferRawFrame(device, frame & 0x1ffU) : frame & 0xffffU;
    device.frameDelay = frameCycles;
    return;
  }
  if (ssc[I2C] & i2cStart) {
    device.busActive = true;
    device.addressFrame = true;
  }
  if (!device.busActive) return;
  ssc[STAT] &= ~statTir;
  device.pendingReceive = transferFrame(device, frame & 0x1ffU);
  device.frameDelay = frameCycles;
}
}

int readSsc(void *context, uint32_t address, int width, uint32_t *value) {
  int result = checkAccess(address, width);
  if (result) return result;
  auto &device = *static_cast<SscDevice *>(context);
  auto &ssc = device.registers;
  uint32_t index = (address - device.base) / 4;
  if (index == TBUF || index == SLAD) return DEVICE_ACCESS_UNSUPPORTED;
  *value = index == STAT ? ssc[STAT] | (device.busActive ? statBusy : 0U) : ssc[index];
  if (index == RBUF) ssc[STAT] &= ~statRir;
  if (index == STAT) ssc[STAT] &= ~statStop;
  return 0;
}

int writeSsc(void *context, uint32_t address, int width, uint32_t value) {
  int result = checkAccess(address, width);
  if (result) return result;
  auto &device = *static_cast<SscDevice *>(context);
  auto &ssc = device.registers;
  uint32_t index = (address - device.base) / 4;
  switch (index) {
    case TBUF: writeTransmit(device, value); break;
    case RBUF:
    case STAT: return DEVICE_ACCESS_UNSUPPORTED;
    case CON:
      ssc[CON] = value & 0x7ffU;
      if (value & conSoftReset) {
        abortTransfer(device);
        ssc[RBUF] = 0;
        ssc[STAT] = statTir;
      }
      break;
    case IEN: ssc[IEN] = value & 0x1ffU; break;
    case I2C:
      ssc[I2C] = value & 0x1fU;
      if ((value & i2cStop) && device.busActive) {
        abortTransfer(device);
        ssc[STAT] |= statStop;
      }
      break;
    default: ssc[index] = value & 0xffffU; break;
  }
  return 0;
}

void sscBusCondition(SscDevice &device, bool start) {
  if (device.registers[I2C] & i2cMode) return;
  if (start) {
    device.busActive = true;
    device.addressFrame = true;
  } else {
    device.busActive = false;
    device.selected = nullptr;
  }
}

void resetSsc(SscDevice &device) {
  device.registers = {};
  device.registers[BRG] = 1;
  device.registers[STAT] = statTir;
  abortTransfer(device);
  device.addressFrame = false;
  device.reading = false;
  device.pendingReceive = 0;
}

bool tickSsc(SscDevice &device) {
  auto &ssc = device.registers;
  if (device.frameDelay && --device.frameDelay == 0) {
    ssc[RBUF] = device.pendingReceive;
    ssc[STAT] |= statRir | statTir;
  }
  return (ssc[STAT] & ssc[IEN]) != 0;
}
