#include "BitBangI2c.h"

void BitBangI2c::reset() {
  sdaOut_ = sclOut_ = true;
  slaveLow_ = false;
  state_ = State::Idle;
  bit_ = 0;
  shift_ = 0;
  masterAck_ = true;
  selected_ = nullptr;
}

uint8_t BitBangI2c::input(uint8_t levels) const {
  return slaveLow_ ? static_cast<uint8_t>(levels & ~sda_) : levels;
}

/* SCL falls after the 8th bit: the acknowledge clock follows */
void BitBangI2c::byteClocked() {
  switch (state_) {
    case State::Address:
      selected_ = nullptr;
      for (I2cRegisterDevice *slave : slaves_) {
        if (slave->address == (shift_ & 0xfeU)) selected_ = slave;
      }
      slaveLow_ = selected_ != nullptr; // acknowledge the address
      if (!selected_) state_ = State::Ignore;
      break;
    case State::Write:
      i2cSlaveWrite(*selected_, shift_);
      slaveLow_ = true;
      break;
    case State::Read:
      slaveLow_ = false; // the master acknowledges
      break;
    default:
      break;
  }
}

/* SCL falls after the acknowledge clock: the next byte starts */
void BitBangI2c::acknowledgeClocked() {
  bit_ = 0;
  slaveLow_ = false;
  if (state_ == State::Address) {
    if (shift_ & 1U) {
      state_ = State::Read;
      masterAck_ = true;
    } else {
      i2cSlaveBeginWrite(*selected_);
      state_ = State::Write;
    }
  } else if (state_ == State::Read && !masterAck_) {
    state_ = State::Ignore; // NAK: the master reads no more
    return;
  }
  shift_ = 0;
  if (state_ == State::Read) {
    shift_ = i2cSlaveRead(*selected_);
    slaveLow_ = (shift_ & 0x80U) == 0;
  }
}

void BitBangI2c::observe(uint8_t out) {
  bool sda = (out & sda_) != 0;
  bool scl = (out & scl_) != 0;
  bool sdaWas = sdaOut_;
  bool sclWas = sclOut_;
  sdaOut_ = sda;
  sclOut_ = scl;

  if (scl && sclWas && sda != sdaWas) { // SDA changes while SCL is high
    slaveLow_ = false;
    bit_ = 0;
    shift_ = 0;
    if (!sda) {
      state_ = State::Address; // START or repeated START
    } else {
      state_ = State::Idle; // STOP
      selected_ = nullptr;
    }
    return;
  }
  if (state_ == State::Idle) return;

  if (scl && !sclWas) { // rising edge: the receiver samples SDA
    bool line = sda && !slaveLow_;
    if (bit_ < 8) {
      if (state_ == State::Address || state_ == State::Write) shift_ = static_cast<uint8_t>(shift_ << 1 | (line ? 1 : 0));
    } else if (state_ == State::Read) {
      masterAck_ = !sda;
    }
    ++bit_;
    return;
  }
  if (!scl && sclWas) { // falling edge: the transmitter changes SDA
    if (state_ == State::Ignore) return;
    if (bit_ == 8) byteClocked();
    else if (bit_ >= 9) acknowledgeClocked();
    else if (state_ == State::Read && bit_ > 0) slaveLow_ = ((static_cast<unsigned>(shift_) << static_cast<unsigned>(bit_)) & 0x80U) == 0;
  }
}
