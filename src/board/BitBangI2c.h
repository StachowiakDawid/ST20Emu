#ifndef BITBANGI2C_H
#define BITBANGI2C_H

#include "../soc/common/STi55xx_Ssc.h"

#include <cstdint>
#include <vector>

/*
 * I2C bus whose master is software toggling two open-drain PIO pins (the Samsung boards'
 * i2c_start/i2c_put_byte/i2c_get_byte, e.g. DSR7000 main application 0x7fc20918). The master
 * releases a line by setting its output bit and pulls it low by clearing it; the line level
 * is that output ANDed with the addressed slave's SDA. Decodes START, STOP, repeated START,
 * address and data bytes with their acknowledges; slaves are I2cRegisterDevice models.
 * Clock stretching and arbitration are not modelled.
 */
class BitBangI2c {
public:
  BitBangI2c(int port, uint8_t sdaBit, uint8_t sclBit) : port_(port), sda_(sdaBit), scl_(sclBit) {}

  void attach(I2cRegisterDevice *slave) { slaves_.push_back(slave); }
  void reset();
  int port() const { return port_; }
  /* the master's new output bits on the port */
  void observe(uint8_t out);
  /* the port's pin levels as the master reads them */
  uint8_t input(uint8_t levels) const;

private:
  enum class State { Idle, Address, Write, Read, Ignore };
  void byteClocked();
  void acknowledgeClocked();

  int port_;
  uint8_t sda_;
  uint8_t scl_;
  std::vector<I2cRegisterDevice *> slaves_;
  bool sdaOut_ = true;
  bool sclOut_ = true;
  bool slaveLow_ = false; // the slave pulls SDA low (ACK or a 0 data bit)
  State state_ = State::Idle;
  int bit_ = 0;           // bits of the current byte clocked so far (9 = acknowledge clock)
  uint8_t shift_ = 0;
  bool masterAck_ = true;
  I2cRegisterDevice *selected_ = nullptr;
};

#endif
