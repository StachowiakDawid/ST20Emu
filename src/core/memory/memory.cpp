#include "memory.h"

#include "../../common/compat.h"
#include "../../common/defines.h"
#include "../cpu/st20.h"
#include <cinttypes>

// posix / linux
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// std
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>
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

namespace {
std::vector<MemoryDevice> memoryDevices;

int deviceForAccess(uint32_t address, int width, const MemoryDevice **device) {
  *device = nullptr;
  if (width <= 0) return 0;
  uint64_t end = static_cast<uint64_t>(address) + static_cast<uint32_t>(width) - 1U;
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
}

int registerMemoryDevice(const MemoryDevice &device) {
  if (device.start > device.end || (!device.read && !device.write)) return INVALID_MEMORY_DEVICE;
  for (const MemoryDevice &existing : memoryDevices) {
    if (device.start <= existing.end && device.end >= existing.start) return INVALID_MEMORY_DEVICE;
  }
  memoryDevices.push_back(device);
  return 0;
}

int unregisterMemoryDevice(uint32_t start) {
  for (auto device = memoryDevices.begin(); device != memoryDevices.end(); ++device) {
    if (device->start == start) {
      memoryDevices.erase(device);
      return 0;
    }
  }
  return INVALID_MEMORY_DEVICE;
}

bool isMemoryDeviceAddress(uint32_t address) {
  for (const MemoryDevice &device : memoryDevices) {
    if (address >= device.start && address <= device.end) return true;
  }
  return false;
}

MEMBLK *getMemBlk(uint32_t address, bool create_flag);
int byteUsedBit(MEMBLK *blk, int offset, int set_flag);

int memoryInit() {
  /* the emulator will get into an infinite loop if this initialization isn't done */
  storeBytes(0x3004U, 4, 0U);
  // below for sti5518 ASC1 20005000 base
  storeBytes(0x20005008U, 4, 0x00000000U);
  storeBytes(0x20005014U, 4, 0x00000000U);
  // below for sti5517
  // storeBytes(0x200130f8U, 4, 0x00000001U);

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

int readBytes(uint32_t address, int nBytes, uint32_t *value) {
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
  int deviceResult = deviceForAccess(static_cast<uint32_t>(address), nBytes, &mappedDevice);
  if (deviceResult) return deviceResult;
  if (mappedDevice) {
    MemoryDevice device = *mappedDevice;
    if (!device.read) return DEVICE_ACCESS_UNSUPPORTED;
    uint32_t word = 0;
    deviceResult = device.read(device.context, static_cast<uint32_t>(address), nBytes, &word);
    if (deviceResult) return deviceResult;
    uint32_t mask = nBytes == 4 ? UINT32_MAX : (1U << (nBytes * 8)) - 1U;
    *value = word & mask;
    return 0;
  }

  /* read each of the requested bytes */
  for (int i = 0; i < nBytes; i++) {

    /* get the block that the current byte is in */
    cBlk = getMemBlk(address + static_cast<uint32_t>(i), false);
    if (cBlk == nullptr) {
      return READ_UNUSED_MEM;
    }

    /* get the offset of the byte in the block */
    int offset = static_cast<int>((address + static_cast<uint32_t>(i)) & ADDR_IN_BLK_MASK);

    /* if the byte isn't defined, return error */
    if (!byteUsedBit(cBlk, offset, DONT_ALTER_BIT)) {
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
  *value = static_cast<uint32_t>(result ? word : std::byteswap(static_cast<uint32_t>(word)));
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

  uint32_t cWord = static_cast<uint32_t>(value);

  const MemoryDevice *mappedDevice = nullptr;
  int deviceResult = deviceForAccess(static_cast<uint32_t>(address), nBytes, &mappedDevice);
  if (deviceResult) return deviceResult;
  if (mappedDevice) {
    MemoryDevice device = *mappedDevice;
    if (!device.write) return DEVICE_ACCESS_UNSUPPORTED;
    uint32_t mask = nBytes == 4 ? UINT32_MAX : (1U << (nBytes * 8)) - 1U;
    return device.write(device.context, static_cast<uint32_t>(address), nBytes,
                        static_cast<uint32_t>(value) & mask);
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
    int result = readBytes(static_cast<uint32_t>(source + offset), width, &value);
    if (result) return result;
    result = storeBytes(static_cast<uint32_t>(destination + offset), width, static_cast<uint32_t>(value));
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
  MEMBLK *blk = nullptr;
  MEMBLK **lastBlk = nullptr;

  /* check if the necessary block already exists */
  for (lastBlk = &memoryMap; *lastBlk != nullptr; lastBlk = &(*lastBlk)->next) {
    if ((*lastBlk)->startAddr == (address & ADDR_OF_BLK_MASK)) {
      return *lastBlk;
    } else if ((*lastBlk)->startAddr > (address & ADDR_OF_BLK_MASK)) {
      break;
    }
  }

  /* The requested block wasn't found.
   * Return nullptr if we aren't supposed to create new blocks
   */
  if (!create_flag) {
    return nullptr;
  }

  /* allocate memory for a new memory block */
  blk = static_cast<MEMBLK *>(malloc(sizeof(MEMBLK)));
  if (blk == nullptr) {
    return nullptr;
  }
  memset(blk, 0, sizeof(MEMBLK));

  /* save this block in the list of blocks */
  blk->next = *lastBlk;
  *lastBlk = blk;

  /* store the starting address for this block of memory */
  blk->startAddr = address & ADDR_OF_BLK_MASK;

  return blk;
}

/**************************
 * stores a large number of bytes in memory blocks
 */
int bulkLoadBytes(uint32_t address, const char *byteFile, char *usedFile, uint32_t *totalBytes) {
  int nBytesRead = 0;
  int byteFd = -1;
  int usedFd = -1;
  struct stat st{};

  if (byteFile == nullptr || *byteFile == '\0') {
    compat::println(stderr, "No byte filename specified");
    return INVALID_BYTE_FILENAME;
  }

  if (stat(byteFile, &st) != 0) {
    perror("Memory file stat failed");
    return INVALID_BYTE_FILE;
  }

  if (st.st_size < 0 || static_cast<uint64_t>(st.st_size) > UINT32_MAX) {
    return INVALID_BYTE_FILE;
  }
  uint32_t size = static_cast<uint32_t>(st.st_size);

  if (address == 0) {
    address = WordSignBit - size;
  }
  compat::println(stderr, "Loading {:d} bytes from {} to 0x{:08x}", size, byteFile, address);

  uint32_t nextAddress = address;
  char data[BLKSIZE];
  MEMBLK *cBlk = nullptr;

  *totalBytes = 0;

  /* check if the byte file can be opened properly */
  if ((byteFd = open(byteFile, O_RDONLY)) < 0) {
    perror("Memory file cannot be opened");
    return INVALID_BYTE_FILE;
  }

  /* check the name of the used file */
  if (usedFile == nullptr || *usedFile == '\0') {
    usedFd = -1;
  } else if ((usedFd = open(usedFile, O_RDONLY)) < 0) {
    /* check if the used file can be opened properly */
    perror("Used memory file cannot be opened");
    close(byteFd);
    return INVALID_BYTE_FILE;
  }

  /* read one block of data at a time from the input file */
  do {
    /*
     * since the memory blocks are allocated on BLKSIZE boundaries, we
     * have to fill the block starting at the proper byte address within
     * the block
     */
    uint32_t blockOffset = nextAddress & ADDR_IN_BLK_MASK;
    nBytesRead = static_cast<int>(read(byteFd, data, BLKSIZE - blockOffset));
    if (nBytesRead < 0) {
      close(byteFd);
      if (usedFd >= 0) close(usedFd);
      return INVALID_BYTE_FILE;
    }

    /* if we haven't reached the end of the file's data */
    if (nBytesRead > 0) {
      /* get the memory block to store the next block of bytes in */
      cBlk = getMemBlk(nextAddress, true);

      if (cBlk == nullptr) {
        compat::println(stderr, "Cannot allocate memory for byte file");
        close(byteFd);
        if (usedFd >= 0)
          close(usedFd);
        return FAILED_MALLOC;
      }

      /* copy the data into the memory block */
      memcpy(&(cBlk->data[blockOffset]), data, static_cast<size_t>(nBytesRead));

      /* if there is no used byte file, set all of the bytes to used */
      if (usedFd < 0) {
        /* set the bits in the used array to show which bytes have been stored */
        for (int i = 0; i < nBytesRead; i++) {
          /* set the used bit for the bytes that were read */
          byteUsedBit(cBlk, static_cast<int>(blockOffset + static_cast<uint32_t>(i)), SET_BIT);
        }
      } else {
        /* if there is a used byte file, load it */
        size_t usedBytesToRead =
            (static_cast<size_t>(BLKSIZE) - static_cast<size_t>(blockOffset)) / 8U;
        if (read(usedFd, &(cBlk->used[blockOffset / 8]), usedBytesToRead) <= 0) {
          close(byteFd);
          close(usedFd);
          return INVALID_OUT_FILE;
        }
      }
    } /* end of if nBytesRead > 0 */

    *totalBytes += static_cast<uint32_t>(nBytesRead);
    nextAddress += static_cast<uint32_t>(nBytesRead);

  } while (nBytesRead > 0);

  close(byteFd);
  if (usedFd >= 0) {
    close(usedFd);
  }

  return 0;
}

int saveMemory(const char *dirName) {
  char dataFileName[NAME_SIZE];
  int dataFileFd = 0;
  char usedFileName[NAME_SIZE];
  int usedFileFd = 0;
  uint32_t nextAddress = 0xFFFFFFFFU;
  MEMBLK *cBlk = nullptr;

  /* check the directory name */
  if (dirName == nullptr || *dirName == '\0') {
    compat::println(stderr, "No output filename specified");
    return INVALID_BYTE_FILENAME;
  }

  /* create the directory */
#ifdef _WIN32
  if (mkdir(dirName) < 0) {
#else
  if (mkdir(dirName, S_IRWXU) < 0) {
#endif
    return INVALID_DIR;
  }

  for (cBlk = memoryMap; cBlk != nullptr; cBlk = cBlk->next) {
    auto blkAddr = static_cast<uint32_t>(cBlk->startAddr);

    /* omit the ROM code addresses */
    /*	 if (cBlk->startAddr >= 0x7FF80000 && cBlk->startAddr <= 0x7FFFFFFF) {
        continue;
       }
    */
    /* omit the Wptr addresses */
    /*	 if (cBlk->startAddr >= WPTR_START_ADDR && cBlk->startAddr <= WPTR_END_ADDR) {
        continue;
       }
    */

    /*
     * If this memory block is contiguous with the last, append it to
     * the last file rather than creating a new file.
     * If not, create a new file.
     */
    if (blkAddr != nextAddress) {
      /* close the file if it is already open */
      if (dataFileFd > 0) {
        close(dataFileFd);
      }
      if (usedFileFd > 0) {
        close(usedFileFd);
      }

      /* create the file names */
      if (snprintf(dataFileName, NAME_SIZE, "%s/%08" PRIx32 ".bin", dirName, cBlk->startAddr) < 0) {
        return INVALID_OUT_FILE;
      }
      if (snprintf(usedFileName, NAME_SIZE, "%s/%08" PRIx32 ".use", dirName, cBlk->startAddr) < 0) {
        return INVALID_OUT_FILE;
      }

/* check if the files can be opened properly */
#if defined(S_IREAD) && defined(S_IWRITE)
      if ((dataFileFd = open(dataFileName, O_WRONLY | O_CREAT | O_TRUNC, S_IREAD | S_IWRITE)) < 0) {
        perror("Output memory file cannot be opened");
        return INVALID_OUT_FILE;
      }
      if ((usedFileFd = open(usedFileName, O_WRONLY | O_CREAT | O_TRUNC, S_IREAD | S_IWRITE)) < 0) {
        perror("Output used memory file cannot be opened");
        return INVALID_OUT_FILE;
      }
#else
      if ((dataFileFd = open(dataFileName, O_WRONLY | O_CREAT | O_TRUNC)) < 0) {
        perror("Output memory file cannot be opened");
        return INVALID_OUT_FILE;
      }
      if ((usedFileFd = open(usedFileName, O_WRONLY | O_CREAT | O_TRUNC)) < 0) {
        perror("Output used memory file cannot be opened");
        return INVALID_OUT_FILE;
      }
#endif

      nextAddress = blkAddr;
    }

    if (write(dataFileFd, cBlk->data, static_cast<size_t>(BLKSIZE)) < 0) {
      if (dataFileFd > 0)
        close(dataFileFd);
      if (usedFileFd > 0)
        close(usedFileFd);
      return WRITE_FAILURE;
    }
    if (write(usedFileFd, cBlk->used, static_cast<size_t>(BLKSIZE / 8)) < 0) {
      if (dataFileFd > 0)
        close(dataFileFd);
      if (usedFileFd > 0)
        close(usedFileFd);
      return WRITE_FAILURE;
    }
    nextAddress += static_cast<uint32_t>(BLKSIZE);
  }

  if (dataFileFd > 0) {
    close(dataFileFd);
  }
  if (usedFileFd > 0) {
    close(usedFileFd);
  }

  return 0;
}

int loadMemory(const char *dirName) {
  if (dirName == nullptr || *dirName == '\0') {
    compat::println(stderr, "No directory specified for loadMemory");
    return INVALID_BYTE_FILENAME;
  }

  char fileMask[NAME_SIZE];
  if (snprintf(fileMask, sizeof(fileMask), "%s/???????0.bin", dirName) < 0) {
    compat::println("filemask failed");
    return INVALID_OUT_FILE;
  }
  compat::println("filemask: {}", fileMask);

  DIR *dirBlk = opendir(dirName);
  if (dirBlk == nullptr) {
    compat::println("opendir failed");
    return INVALID_OUT_FILE;
  }
  compat::println("opendir succeeded");

  // exactly 7 chars, then "0.bin"
  std::regex filePattern("^.......0\\.bin$");

  struct dirent *entry = nullptr;
  while ((entry = readdir(dirBlk)) != nullptr) { /* for each file */
    if (!std::regex_match(entry->d_name, filePattern)) {
      continue; // skip non-matching files
    }

    compat::println("d_name: {}", entry->d_name);

    uint32_t address = 0;
    if (sscanf(entry->d_name, "%8" SCNx32, &address) != 1) {
      closedir(dirBlk);
      return INVALID_OUT_FILE;
    }

    /* build the full byte filename */
    char byteFileName[NAME_SIZE];
    if (snprintf(byteFileName, sizeof(byteFileName), "%s/%s", dirName, entry->d_name) < 0) {
      closedir(dirBlk);
      return INVALID_OUT_FILE;
    }

    /* build the used byte filename from the data file name */
    char usedFileName[NAME_SIZE];
    if (snprintf(usedFileName, sizeof(usedFileName), "%s", byteFileName) < 0) {
      closedir(dirBlk);
      return INVALID_OUT_FILE;
    }

    char *dotLoc = strrchr(usedFileName, '.');
    if (dotLoc == nullptr) {
      closedir(dirBlk);
      return INVALID_OUT_FILE;
    }

    /* replace .bin extension with .use */
    if (snprintf(dotLoc, sizeof(usedFileName) - static_cast<size_t>(dotLoc - usedFileName),
                 ".use") < 0) {
      closedir(dirBlk);
      return INVALID_OUT_FILE;
    }

    compat::print(stderr, "{}", byteFileName);
    compat::print(stderr, "   ");
    compat::println(stderr, "{}", usedFileName);

    uint32_t dataLength = 0;
    int result = bulkLoadBytes(address, byteFileName, usedFileName, &dataLength);
    if (result != 0) {
      closedir(dirBlk);
      return result;
    }
  } /* end of for each data file */

  closedir(dirBlk);
  return 0;
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
