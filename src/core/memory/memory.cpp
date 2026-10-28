#include "memory.h"

#include "../../common/compat.h"
#include "../cpu/st20.h"
#include <cinttypes>

// std
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <iterator>
#include <new>
#include <regex>
#include <unordered_map>
#include <vector>

/* this structure holds the state of the memory */
struct MEMBLK {
  uint32_t startAddr;
  unsigned char data[BLKSIZE];
  unsigned char used[BLKSIZE / 8];
  MEMBLK *next{nullptr};
};

/* this will hold the state of the memory */
static MEMBLK *memoryMap = nullptr;

/* direct lookup of every allocated block by address >> BLKSHIFT, so reads
 * don't walk the sorted memoryMap list (which is kept for save/load) */
static MEMBLK *blockIndex[(UINT64_C(1) << 32) >> BLKSHIFT]{};

uint32_t blockVersion[(UINT64_C(1) << 32) >> BLKSHIFT]{};

/* blocks that overlap a memory-mapped device; accesses elsewhere skip the device list */
static bool deviceBlock[(UINT64_C(1) << 32) >> BLKSHIFT]{};

namespace {
std::vector<MemoryDevice> memoryDevices;

/* unwritten bytes in this inclusive range read as zero (SDRAM); disabled while start > end */
uint32_t zeroFillStart = 1;
uint32_t zeroFillEnd = 0;

uint32_t interceptStart = 1;
uint32_t interceptEnd = 0;
WriteInterceptor writeInterceptor = nullptr;
bool intercepting = false;

/* ST20_IOTRACE=N: log the first N accesses to each address of the peripheral region
 * (0x20000000-0x3fffffff), the MPEG registers (below 0x5000) and writes to the flash (0x7f...)
 * on stderr, with access counts per address at exit */
const long ioTraceLimit = [] {
  const char *value = std::getenv("ST20_IOTRACE");
  return value ? std::strtol(value, nullptr, 0) : 0L;
}();
std::unordered_map<uint32_t, long> ioTraceCounts;

void traceIo(bool write, uint32_t address, int width, uint32_t value) {
  bool flashWrite = write && address >= 0x7f000000U && address <= 0x7fffffffU;
  bool mpeg = address < 0x5000U;
  if ((address < 0x20000000U || address > 0x3fffffffU) && !flashWrite && !mpeg) return;
  static bool summary = [] {
    std::atexit([] {
      std::vector<std::pair<uint32_t, long>> counts(ioTraceCounts.begin(), ioTraceCounts.end());
      std::sort(counts.begin(), counts.end());
      for (auto [traced, total] : counts) std::fprintf(stderr, "IOCOUNT %08x %ld\n", traced, total);
    });
    return true;
  }();
  (void)summary;
  long &count = ioTraceCounts[address];
  if (count++ >= ioTraceLimit) return;
  std::fprintf(stderr, "IO %c %08x/%d %08x @%08x\n", write ? 'W' : 'R', address, width, value,
               get_iptr());
}

bool isZeroFillAddress(uint32_t address) {
  return address >= zeroFillStart && address <= zeroFillEnd;
}

int deviceForAccess(uint32_t address, int width, const MemoryDevice **device) {
  *device = nullptr;
  if (width <= 0) return 0;
  uint64_t end = static_cast<uint64_t>(address) + static_cast<uint32_t>(width) - 1U;
  if (!deviceBlock[address >> BLKSHIFT] && !deviceBlock[static_cast<uint32_t>(end) >> BLKSHIFT]) return 0;
  for (const MemoryDevice &candidate : memoryDevices) {
    bool overlaps = (address <= candidate.end && end >= candidate.start) ||
                    (end > UINT32_MAX && static_cast<uint32_t>(end) >= candidate.start);
    if (!overlaps) continue;
    if (address < candidate.start || end > candidate.end) return INVALID_DEVICE_ACCESS;
    *device = &candidate;
    return 0;
  }
  return 0;
}

void indexDeviceBlocks() {
  std::fill(std::begin(deviceBlock), std::end(deviceBlock), false);
  for (const MemoryDevice &device : memoryDevices) {
    for (uint64_t block = device.start >> BLKSHIFT; block <= device.end >> BLKSHIFT; ++block)
      deviceBlock[block] = true;
  }
}

/* reads nBytes (little-endian) that lie in one block and have all been written; false otherwise */
bool readWrittenBytes(uint32_t address, int nBytes, uint32_t *value) {
  uint32_t offset = address & ADDR_IN_BLK_MASK;
  MEMBLK *block = blockIndex[address >> BLKSHIFT];
  if (block == nullptr || offset + static_cast<uint32_t>(nBytes) > BLKSIZE) return false;
  uint32_t word = 0;
  for (uint32_t i = 0; i < static_cast<uint32_t>(nBytes); ++i) {
    uint32_t byte = offset + i;
    if (!(block->used[byte >> 3] & (1U << (byte & 7U)))) return false;
    word |= static_cast<uint32_t>(block->data[byte]) << (8U * i);
  }
  *value = word;
  return true;
}
}

int registerMemoryDevice(const MemoryDevice &device) {
  if (device.start > device.end || (!device.read && !device.write)) return INVALID_MEMORY_DEVICE;
  for (const MemoryDevice &existing : memoryDevices) {
    if (device.start <= existing.end && device.end >= existing.start) return INVALID_MEMORY_DEVICE;
  }
  memoryDevices.push_back(device);
  indexDeviceBlocks();
  return 0;
}

int unregisterMemoryDevice(uint32_t start) {
  for (auto device = memoryDevices.begin(); device != memoryDevices.end(); ++device) {
    if (device->start == start) {
      memoryDevices.erase(device);
      indexDeviceBlocks();
      return 0;
    }
  }
  return INVALID_MEMORY_DEVICE;
}

bool isMemoryDeviceAddress(uint32_t address) {
  if (!deviceBlock[address >> BLKSHIFT]) return false;
  for (const MemoryDevice &device : memoryDevices) {
    if (address >= device.start && address <= device.end) return true;
  }
  return false;
}

void setWriteInterceptor(uint32_t start, uint32_t end, WriteInterceptor interceptor) {
  interceptStart = start;
  interceptEnd = end;
  writeInterceptor = interceptor;
}

void setZeroFillRange(uint32_t start, uint32_t end) {
  zeroFillStart = start;
  zeroFillEnd = end;
}

MEMBLK *getMemBlk(uint32_t address, bool create_flag);
int byteUsedBit(MEMBLK *blk, int offset, int set_flag);

int memoryInit() {
  /* the emulator will get into an infinite loop if this initialization isn't done */
  storeBytes(0x3004U, 4, 0U);
  // STi5518 ASC2 (base 0x20005000) receive buffer and status
  storeBytes(0x20005008U, 4, 0x00000000U);
  storeBytes(0x20005014U, 4, 0x00000000U);

  return 0;
}

/**************************
 * There is one bit to flag whether a byte in the data array
 * is used or not. used[byte 0, bit 0] indicates whether data[0]
 * is used, used[byte 1, bit 0] indicates whether data[8] is used.
 *
 * returns true if the Used bit was set for the specified byte in
 * the specified memory block before this function was called
 */
int byteUsedBit(MEMBLK *block, int offset, int usedOperation) {
  if (block == nullptr) {
    return 0;
  }

  /* get the address of the byte's used bit */
  int usedByteAddr = static_cast<int>((static_cast<uint32_t>(offset) & ADDR_IN_BLK_MASK) >> 3);
  int usedBitAddr = offset & 7;

  /* get the state of the requested bit */
  unsigned char mask = static_cast<unsigned char>(1U << usedBitAddr);
  unsigned char usedByte = block->used[usedByteAddr];
  bool wasSet = (usedByte & mask) != 0;

  if (usedOperation == SET_BIT) {
    /* mark the bit as used */
    block->used[usedByteAddr] |= mask;
  } else if (usedOperation == CLEAR_BIT) {
    /* clear the used bit */
    block->used[usedByteAddr] &= static_cast<unsigned char>(~mask);
  }

  /* return 1 if the used bit was originally set */
  return wasSet ? 1 : 0;
}

/**************************
 * This function reads from one to four bytes from the proper memory block.
 *
 * Returns 0 if the bytes were read properly.
 */

static int readBytesUntraced(uint32_t address, int nBytes, uint32_t *value);

int readBytes(uint32_t address, int nBytes, uint32_t *value) {
  int result = readBytesUntraced(address, nBytes, value);
  if (ioTraceLimit && value) traceIo(false, address, nBytes, *value);
  return result;
}

static int readBytesUntraced(uint32_t address, int nBytes, uint32_t *value) {
  MEMBLK *cBlk = nullptr;
  unsigned char cByte = 0;
  uint32_t cWord = 0;

  if (value == nullptr) {
    return READ_UNUSED_MEM;
  }

  *value = undefinedWord;

  /* this routine won't work with anything longer than a double word */
  if (nBytes > 4) {
    return READ_TOO_LARGE;
  }

  const MemoryDevice *mappedDevice = nullptr;
  int deviceResult = deviceForAccess(address, nBytes, &mappedDevice);
  if (deviceResult) return deviceResult;
  if (!mappedDevice && readWrittenBytes(address, nBytes, value)) return 0;
  if (mappedDevice) {
    MemoryDevice device = *mappedDevice;
    if (!device.read) return DEVICE_ACCESS_UNSUPPORTED;
    uint32_t word = 0;
    requestPeripheralPoll();
    deviceResult = device.read(device.context, address, nBytes, &word);
    if (deviceResult) return deviceResult;
    uint32_t mask = nBytes == 4 ? UINT32_MAX : (1U << (nBytes * 8)) - 1U;
    *value = word & mask;
    return 0;
  }

  /* read each of the requested bytes */
  for (int i = 0; i < nBytes; i++) {

    /* get the block that the current byte is in */
    uint32_t byteAddr = address + static_cast<uint32_t>(i);
    cBlk = getMemBlk(byteAddr, false);

    /* get the offset of the byte in the block */
    int offset = static_cast<int>(byteAddr & ADDR_IN_BLK_MASK);

    /* if the byte isn't defined, return error (zero-filled memory reads as 0) */
    if (cBlk == nullptr || !byteUsedBit(cBlk, offset, DONT_ALTER_BIT)) {
      if (isZeroFillAddress(byteAddr)) continue;
      return READ_UNUSED_MEM;
    }

    /* get the data byte */
    cByte = cBlk->data[offset];

    /* build the return value from its component bytes */
    cWord |= (static_cast<uint32_t>(cByte) << (8U * static_cast<uint32_t>(i)));
  }

  *value = cWord;

  return 0;
}

int readInvBytes(uint32_t address, int nBytes, uint32_t *value) {
  if (value == nullptr) {
    return READ_UNUSED_MEM;
  }

  uint32_t word = 0;
  int result = readBytes(address, nBytes, &word);
  *value = result ? word : std::byteswap(word);
  return result;
}

/**************************
 * This function stores from one to four bytes in the proper memory block.
 *
 * Returns 0 if the bytes were stored properly.
 */
int storeBytes(uint32_t address, int nBytes, uint32_t value) {
  /* this routine won't work with anything longer than a double word */
  if (nBytes > 4) {
    return 0; // preferably the appropriate non-zero error code
  }

  uint32_t cWord = value;
  if (ioTraceLimit) traceIo(true, address, nBytes, value);
  if (address >= interceptStart && address <= interceptEnd && writeInterceptor && !intercepting) {
    intercepting = true;
    bool consumed = writeInterceptor(address, nBytes, value);
    intercepting = false;
    if (consumed) return 0;
  }

  const MemoryDevice *mappedDevice = nullptr;
  int deviceResult = deviceForAccess(address, nBytes, &mappedDevice);
  if (deviceResult) return deviceResult;
  if (mappedDevice) {
    MemoryDevice device = *mappedDevice;
    if (!device.write) return DEVICE_ACCESS_UNSUPPORTED;
    uint32_t mask = nBytes == 4 ? UINT32_MAX : (1U << (nBytes * 8)) - 1U;
    requestPeripheralPoll();
    return device.write(device.context, address, nBytes, value & mask);
  }

  /* all bytes in one block: look it up once */
  uint32_t blockOffset = address & ADDR_IN_BLK_MASK;
  if (nBytes > 0 && blockOffset + static_cast<uint32_t>(nBytes) <= BLKSIZE) {
    MEMBLK *block = getMemBlk(address, true);
    if (block == nullptr) return READ_UNUSED_MEM;
    for (uint32_t byte = blockOffset; byte < blockOffset + static_cast<uint32_t>(nBytes); ++byte) {
      block->data[byte] = static_cast<unsigned char>(cWord & 0xFFU);
      block->used[byte >> 3] |= static_cast<unsigned char>(1U << (byte & 7U));
      cWord >>= 8U;
    }
    ++blockVersion[address >> BLKSHIFT];
    return 0;
  }

  for (int i = 0; i < nBytes; i++) {
    /* get the block that the current byte is in */
    /* if the memory block can't be created, return an error */
    MEMBLK *cBlk = getMemBlk(address + static_cast<uint32_t>(i), true);
    if (cBlk == nullptr) {
      return READ_UNUSED_MEM;
    }

    /* get each of the requested bytes (little-endian order) */
    unsigned char cByte = static_cast<unsigned char>(cWord & 0xFFU);
    cWord >>= 8U;

    /* get the offset of the byte in the block */
    int offset = static_cast<int>((address + static_cast<uint32_t>(i)) & ADDR_IN_BLK_MASK);

    /* save the byte in the memory block */
    cBlk->data[offset] = cByte;
    ++blockVersion[(address + static_cast<uint32_t>(i)) >> BLKSHIFT];

    /* mark the byte as used */
    byteUsedBit(cBlk, offset, static_cast<int>(SET_BIT));
  }

  return 0;
}

/**************************
 * This function copies a range of bytes from srcAddr to destAddr.
 *
 * Returns 0 if all bytes were copied properly.
 */
int storeByteRange(uint32_t srcAddr, uint32_t destAddr, uint32_t nBytes) {
  for (uint32_t offset = 0; offset < nBytes;) {
    uint32_t source = srcAddr + offset;
    uint32_t destination = destAddr + offset;
    int width = nBytes - offset >= 4 && ((source | destination) & 3U) == 0 ? 4 : 1;
    uint32_t cWord = 0;

    int result = readBytes(source, width, &cWord);
    if (result != 0) {
      return result;
    }

    result = storeBytes(destination, width, cWord);
    if (result != 0) {
      return result;
    }
    offset += static_cast<uint32_t>(width);
  }

  return 0;
}

int validateDeviceMoveRange(uint32_t source, uint32_t destination, uint32_t length) {
  if (length == 0) return 0;
  uint64_t sourceEnd = static_cast<uint64_t>(source) + length;
  uint64_t destinationEnd = static_cast<uint64_t>(destination) + length;
  constexpr uint64_t addressSpaceSize = uint64_t{1} << 32;
  if (sourceEnd > addressSpaceSize || destinationEnd > addressSpaceSize)
    return INVALID_DEVICE_MOVE_RANGE;
  auto touchesPeripheral = [](uint32_t start, uint64_t end) {
    return start <= 0x3fffffffU && end > 0x20000000U;
  };
  if (touchesPeripheral(source, sourceEnd) || touchesPeripheral(destination, destinationEnd))
    return DEVICE_MOVE_PERIPHERAL_UNSUPPORTED;
  if (source < destinationEnd && destination < sourceEnd) return INVALID_DEVICE_MOVE_RANGE;
  return 0;
}

int copyDeviceBytes(uint32_t source, uint32_t destination, uint32_t length) {
  int validation = validateDeviceMoveRange(source, destination, length);
  if (validation) return validation;
  for (uint32_t offset = 0; offset < length;) {
    uint32_t remaining = length - offset;
    int width = remaining < 4 ? static_cast<int>(remaining) : 4;
    uint32_t value = 0;
    int result = readBytes(source + offset, width, &value);
    if (result) return result;
    result = storeBytes(destination + offset, width, value);
    if (result) return result;
    offset += static_cast<uint32_t>(width);
  }
  return 0;
}

int allocBytes(uint32_t address, int nBytes) {
  return storeBytes(address, nBytes, undefinedWord);
}

/**************************
 * returns the address of the memory block containing the
 * byte with the specified address.  A new memory block is
 * created if there is no memory block with the address.
 */
MEMBLK *getMemBlk(uint32_t address, bool create_flag) {
  MEMBLK *blk = blockIndex[address >> BLKSHIFT];
  MEMBLK **lastBlk = nullptr;

  /* check if the necessary block already exists */
  if (blk != nullptr) {
    return blk;
  }

  /* The requested block wasn't found.
   * Return nullptr if we aren't supposed to create new blocks
   */
  if (!create_flag) {
    return nullptr;
  }

  /* find where the new block goes in the sorted list */
  for (lastBlk = &memoryMap; *lastBlk != nullptr; lastBlk = &(*lastBlk)->next) {
    if ((*lastBlk)->startAddr > (address & ADDR_OF_BLK_MASK)) {
      break;
    }
  }

  /* allocate memory for a new memory block */
  blk = new (std::nothrow) MEMBLK{};
  if (blk == nullptr) {
    return nullptr;
  }

  /* save this block in the list of blocks */
  blk->next = *lastBlk;
  *lastBlk = blk;

  /* store the starting address for this block of memory */
  blk->startAddr = address & ADDR_OF_BLK_MASK;
  blockIndex[address >> BLKSHIFT] = blk;

  return blk;
}

/**************************
 * stores a large number of bytes in memory blocks
 */
int bulkLoadBytes(uint32_t address, const char *byteFile, char *usedFile, uint32_t *totalBytes) {
  if (byteFile == nullptr || *byteFile == '\0') {
    compat::println(stderr, "No byte filename specified");
    return INVALID_BYTE_FILENAME;
  }

  std::error_code error;
  std::uintmax_t fileSize = std::filesystem::file_size(byteFile, error);
  if (error) {
    compat::println(stderr, "Memory file stat failed: {}", error.message());
    return INVALID_BYTE_FILE;
  }
  if (fileSize > UINT32_MAX) {
    return INVALID_BYTE_FILE;
  }
  uint32_t size = static_cast<uint32_t>(fileSize);

  if (address == 0) {
    address = WordSignBit - size;
  }
  compat::println(stderr, "Loading {:d} bytes from {} to 0x{:08x}", size, byteFile, address);

  uint32_t nextAddress = address;
  char data[BLKSIZE];

  *totalBytes = 0;

  std::ifstream bytes(byteFile, std::ios::binary);
  if (!bytes) {
    compat::println(stderr, "Memory file cannot be opened: {}", byteFile);
    return INVALID_BYTE_FILE;
  }

  /* without a used file every loaded byte is marked as written */
  bool haveUsed = usedFile != nullptr && *usedFile != '\0';
  std::ifstream used;
  if (haveUsed) {
    used.open(usedFile, std::ios::binary);
    if (!used) {
      compat::println(stderr, "Used memory file cannot be opened: {}", usedFile);
      return INVALID_BYTE_FILE;
    }
  }

  /* read one block of data at a time; the first block starts at the load address's offset
   * within its memory block */
  int nBytesRead = 0;
  do {
    uint32_t blockOffset = nextAddress & ADDR_IN_BLK_MASK;
    bytes.read(data, static_cast<std::streamsize>(BLKSIZE - blockOffset));
    nBytesRead = static_cast<int>(bytes.gcount());
    if (bytes.bad()) return INVALID_BYTE_FILE;

    if (nBytesRead > 0) {
      MEMBLK *cBlk = getMemBlk(nextAddress, true);
      if (cBlk == nullptr) {
        compat::println(stderr, "Cannot allocate memory for byte file");
        return FAILED_MALLOC;
      }

      memcpy(&(cBlk->data[blockOffset]), data, static_cast<size_t>(nBytesRead));
      ++blockVersion[cBlk->startAddr >> BLKSHIFT];

      if (!haveUsed) {
        for (int i = 0; i < nBytesRead; i++)
          byteUsedBit(cBlk, static_cast<int>(blockOffset + static_cast<uint32_t>(i)), SET_BIT);
      } else {
        std::streamsize usedBytesToRead =
            static_cast<std::streamsize>((static_cast<size_t>(BLKSIZE) - static_cast<size_t>(blockOffset)) / 8U);
        used.read(reinterpret_cast<char *>(&(cBlk->used[blockOffset / 8])), usedBytesToRead);
        if (used.gcount() <= 0) return INVALID_OUT_FILE;
      }
    }

    *totalBytes += static_cast<uint32_t>(nBytesRead);
    nextAddress += static_cast<uint32_t>(nBytesRead);
  } while (nBytesRead > 0);

  return 0;
}

int saveMemory(const char *dirName) {
  if (dirName == nullptr || *dirName == '\0') {
    compat::println(stderr, "No output filename specified");
    return INVALID_BYTE_FILENAME;
  }

  std::error_code error;
  if (!std::filesystem::create_directory(dirName, error)) {
    return INVALID_DIR;
  }

  /* contiguous memory blocks are appended to one file pair, named after the first block's address */
  std::ofstream dataFile;
  std::ofstream usedFile;
  uint32_t nextAddress = 0xFFFFFFFFU;
  for (MEMBLK *cBlk = memoryMap; cBlk != nullptr; cBlk = cBlk->next) {
    if (cBlk->startAddr != nextAddress) {
      dataFile.close();
      usedFile.close();
      std::string base = std::format("{}/{:08x}", dirName, cBlk->startAddr);
      dataFile.open(base + ".bin", std::ios::binary | std::ios::trunc);
      if (!dataFile) {
        compat::println(stderr, "Output memory file cannot be opened: {}.bin", base);
        return INVALID_OUT_FILE;
      }
      usedFile.open(base + ".use", std::ios::binary | std::ios::trunc);
      if (!usedFile) {
        compat::println(stderr, "Output used memory file cannot be opened: {}.use", base);
        return INVALID_OUT_FILE;
      }
      nextAddress = cBlk->startAddr;
    }

    dataFile.write(reinterpret_cast<const char *>(cBlk->data), static_cast<std::streamsize>(BLKSIZE));
    usedFile.write(reinterpret_cast<const char *>(cBlk->used), static_cast<std::streamsize>(BLKSIZE / 8));
    if (!dataFile || !usedFile) {
      return WRITE_FAILURE;
    }
    nextAddress += static_cast<uint32_t>(BLKSIZE);
  }

  dataFile.close();
  usedFile.close();
  return dataFile.fail() || usedFile.fail() ? WRITE_FAILURE : 0;
}

int loadMemory(const char *dirName) {
  if (dirName == nullptr || *dirName == '\0') {
    compat::println(stderr, "No directory specified for loadMemory");
    return INVALID_BYTE_FILENAME;
  }

  std::error_code error;
  std::filesystem::directory_iterator files(dirName, error);
  if (error) {
    compat::println("Cannot open directory {}: {}", dirName, error.message());
    return INVALID_OUT_FILE;
  }

  // data files are named after their 16-byte aligned start address: exactly 7 chars, then "0.bin"
  std::regex filePattern("^.......0\\.bin$");

  for (const auto &entry : files) {
    std::string name = entry.path().filename().string();
    if (!std::regex_match(name, filePattern)) {
      continue;
    }

    uint32_t address = 0;
    if (sscanf(name.c_str(), "%8" SCNx32, &address) != 1) {
      return INVALID_OUT_FILE;
    }

    std::filesystem::path byteFile = entry.path();
    std::filesystem::path usedPath = byteFile;
    usedPath.replace_extension(".use");
    std::string usedName = usedPath.string();
    compat::println(stderr, "{}   {}", byteFile.string(), usedName);

    uint32_t dataLength = 0;
    int result = bulkLoadBytes(address, byteFile.string().c_str(), usedName.data(), &dataLength);
    if (result != 0) {
      return result;
    }
  }
  return 0;
}

void freeAllMemory() {
  MEMBLK* current = memoryMap;
  while (current != nullptr) {
    MEMBLK* next = current->next;
    blockIndex[current->startAddr >> BLKSHIFT] = nullptr;
    ++blockVersion[current->startAddr >> BLKSHIFT];
    delete current;
    current = next;
  }
  memoryMap = nullptr;
}

const char *memoryError(int error) {
  switch (error) {
  case INVALID_BYTE_FILENAME:
    return "The byte filename is invalid";

  case INVALID_BYTE_FILE:
    return "Cannot open byte file";

  case FAILED_MALLOC:
    return "Ran out of room allocating memory for byte file";

  case READ_TOO_LARGE:
    return "Cannot read more than four bytes at one time";

  case READ_UNUSED_MEM:
    return "An uninit memory byte was accessed ";

  case WRITE_FAILURE:
    return "Memory or device write failed";

  case INVALID_MEMORY_DEVICE:
    return "Invalid, overlapping, or unknown memory device region";

  case INVALID_DEVICE_ACCESS:
    return "Memory access crosses a device region boundary";

  case DEVICE_ACCESS_UNSUPPORTED:
    return "Device does not support this access direction";
  case INVALID_DEVICE_MOVE_RANGE:
    return "Device copy ranges overlap or wrap the address space";
  case DEVICE_MOVE_PERIPHERAL_UNSUPPORTED:
    return "Device copy cannot access the on-chip peripheral address range";

  default:
    return "Unknown memory error";
  }
}
