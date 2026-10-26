#ifndef MEMORY_H
#define MEMORY_H

#include "../../common/defines.h"

#include <cstdint>

inline constexpr uint32_t BLKSIZE = 8 * 1024;
inline constexpr uint32_t ADDR_IN_BLK_MASK = BLKSIZE - 1U;
inline constexpr uint32_t ADDR_OF_BLK_MASK = ~ADDR_IN_BLK_MASK;

/* Error codes from memory subsystem */
inline constexpr int MEMORY_ERROR_START = -1000;
inline constexpr int MEMORY_ERROR_END = -1999;

inline constexpr int INVALID_BYTE_FILENAME = -1000;
inline constexpr int INVALID_BYTE_FILE = -1001;
inline constexpr int INVALID_DIR = -1002;
inline constexpr int INVALID_OUT_FILE = -1003;
inline constexpr int WRITE_FAILURE = -1004;
inline constexpr int FAILED_MALLOC = -1010;
inline constexpr int READ_TOO_LARGE = -1011;
inline constexpr int READ_UNUSED_MEM = -1012;
inline constexpr int INVALID_MEMORY_DEVICE = -1013;
inline constexpr int INVALID_DEVICE_ACCESS = -1014;
inline constexpr int DEVICE_ACCESS_UNSUPPORTED = -1015;
inline constexpr int INVALID_DEVICE_MOVE_RANGE = -1016;
inline constexpr int DEVICE_MOVE_PERIPHERAL_UNSUPPORTED = -1017;

struct MemoryDevice {
	uint32_t start;
	uint32_t end;
	void *context;
	int (*read)(void *context, uint32_t address, int width, uint32_t *value);
	int (*write)(void *context, uint32_t address, int width, uint32_t value);
};

int registerMemoryDevice(const MemoryDevice &device);
int unregisterMemoryDevice(uint32_t start);
bool isMemoryDeviceAddress(uint32_t address);

/* Operations to perform on the used bit */
inline constexpr int SET_BIT = 1;
inline constexpr int CLEAR_BIT = 2;
inline constexpr int DONT_ALTER_BIT = 3;

/* routines used to update the state of the memory */
int memoryInit();
void setZeroFillRange(uint32_t start, uint32_t end);
int readBytes(uint32_t address, int nBytes, uint32_t *value);
int readInvBytes(uint32_t address, int nBytes, uint32_t *value);
int storeBytes(uint32_t address, int nBytes, uint32_t value);
int allocBytes(uint32_t address, int nBytes);
int storeByteRange(uint32_t srcAddr, uint32_t destAddr, uint32_t nBytes);
int copyDeviceBytes(uint32_t source, uint32_t destination, uint32_t length);
int validateDeviceMoveRange(uint32_t source, uint32_t destination, uint32_t length);
int bulkLoadBytes(uint32_t address, const char *byteFileName, char *usedFileName, uint32_t *dataLength);
int saveMemory(const char *dirName);
int loadMemory(const char *dirName);
void freeAllMemory();
const char *memoryError(int error);

#endif // MEMORY_H
