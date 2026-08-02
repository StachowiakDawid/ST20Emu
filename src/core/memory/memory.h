#ifndef MEMORY_H
#define MEMORY_H

#include "../../common/defines.h"

inline constexpr long BLKSIZE = 8 * 1024;
inline constexpr long ADDR_IN_BLK_MASK = BLKSIZE - 1L;
inline constexpr long ADDR_OF_BLK_MASK = static_cast<long>(0xFFFFFFFFU) - ADDR_IN_BLK_MASK;

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

/* Operations to perform on the used bit */
inline constexpr int SET_BIT = 1;
inline constexpr int CLEAR_BIT = 2;
inline constexpr int DONT_ALTER_BIT = 3;

/* routines used to update the state of the memory */
int memoryInit();
int readBytes(long address, int nBytes, unsigned long *value);
int readInvBytes(long address, int nBytes, long *value);
int storeBytes(long address, int nBytes, long value);
int allocBytes(long address, int nBytes);
int storeByteRange(long srcAddr, long destAddr, int nBytes);
int bulkLoadBytes(long address, const char *byteFileName, char *usedFileName, long *dataLength);
int saveMemory(const char *dirName);
int loadMemory(const char *dirName);
const char *memoryError(int error);

#endif // MEMORY_H
