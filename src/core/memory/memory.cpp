#include "memory.h"

#include "../../common/compat.h"
#include "../../common/defines.h"
#include "../cpu/st20.h"

// posix / linux
#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <unistd.h>

// std
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* this structure holds the state of the memory */
struct MEMBLK {
  long startAddr;
  unsigned char data[BLKSIZE];
  unsigned char used[BLKSIZE / 8];
  MEMBLK *next{nullptr};
};

/* this will hold the state of the memory */
static MEMBLK *memoryMap = nullptr;

MEMBLK *getMemBlk(long address, bool create_flag);
int byteUsedBit(MEMBLK *blk, int offset, int set_flag);

int memoryInit() {
  /* the emulator will get into an infinite loop if this initialization isn't done */
  storeBytes(0x3004L, 4, 0L);
  // below for sti5518 ASC1 20005000 base
  storeBytes(0x20005014L, 4, 0x00000000L);
  // below for sti5517
  // storeBytes(0x200130f8L, 4, 0x00000001L);

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
  int usedByteAddr = (offset & ADDR_IN_BLK_MASK) >> 3;
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

/* THIS ROUTINE ISN'T NEEDED */
/**************************
 * returns true if the requested byte was defined for the
 * specified block.  *Value is the value of the requested byte.
 */
/*int readBlockByte (MEMBLK *block, int offset, unsigned char *value) {

  *value = (unsigned char) block->data[offset];
*/
/* if the byte was stored, return true */
/*
  return (byteUsedBit(block, offset, (int) DONT_ALTER_BIT));
}
*/

/**************************
 * This function reads from one to four bytes from the proper memory block.
 *
 * Returns 0 if the bytes were read properly.
 */

int readBytes(long address, int nBytes, unsigned long *value) {
  MEMBLK *cBlk = nullptr;
  unsigned char cByte = 0;
  unsigned long cWord = 0;

  if (value == nullptr) {
    return READ_UNUSED_MEM;
  }

  *value = UNDEFINED_WORD_OLD;

  /* this routine won't work with anything longer than a double word */
  if (nBytes > 4) {
    return READ_TOO_LARGE;
  }

  /* read each of the requested bytes */
  for (int i = 0; i < nBytes; i++) {

    /* get the block that the current byte is in */
    cBlk = getMemBlk(address + i, false);
    if (cBlk == nullptr) {
      return READ_UNUSED_MEM;
    }

    /* get the offset of the byte in the block */
    int offset = static_cast<int>((address + i) & ADDR_IN_BLK_MASK);

    /* if the byte isn't defined, return error */
    if (!byteUsedBit(cBlk, offset, DONT_ALTER_BIT)) {
      return READ_UNUSED_MEM;
    }

    /* get the data byte */
    cByte = cBlk->data[offset];

    /* build the return value from its component bytes */
    cWord |= (static_cast<unsigned long>(cByte) << (8U * static_cast<unsigned long>(i)));
  }

  *value = cWord;

  return 0;
}

int readInvBytes(long address, int nBytes, long *value) {
  if (value == nullptr) {
    return READ_UNUSED_MEM;
  }

  *value = UNDEFINED_WORD_OLD;

  /* this routine won't work with anything longer than a double word */
  if (nBytes > 4) {
    return READ_TOO_LARGE;
  }

  unsigned long cWord = 0;

  /* read each of the requested bytes (big-endian order) */
  for (int i = 0; i < nBytes; i++) {

    /* get the block that the current byte is in */
    MEMBLK *cBlk = getMemBlk(address + i, false);
    if (cBlk == nullptr) {
      return READ_UNUSED_MEM;
    }

    /* get the offset of the byte in the block */
    int offset = static_cast<int>((address + i) & ADDR_IN_BLK_MASK);

    /* if the byte isn't defined, return error */
    if (!byteUsedBit(cBlk, offset, DONT_ALTER_BIT)) {
      return READ_UNUSED_MEM;
    }

    /* get the data byte */
    unsigned char cByte = cBlk->data[offset];

    /* build the return value from its component bytes (big-endian shift) */
    cWord |= (static_cast<unsigned long>(cByte) << (8U * static_cast<unsigned long>(3 - i)));
  }

  *value = static_cast<long>(cWord);

  return 0;
}

/**************************
 * This function stores from one to four bytes in the proper memory block.
 *
 * Returns 0 if the bytes were stored properly.
 */
int storeBytes(long address, int nBytes, long value) {
  /* this routine won't work with anything longer than a double word */
  if (nBytes > 4) {
    return 0; // preferably the appropriate non-zero error code
  }

  unsigned long cWord = static_cast<unsigned long>(value);

  for (int i = 0; i < nBytes; i++) {
    /* get the block that the current byte is in */
    /* if the memory block can't be created, return an error */
    MEMBLK *cBlk = getMemBlk(address + i, true);
    if (cBlk == nullptr) {
      return READ_UNUSED_MEM;
    }

    /* get each of the requested bytes (little-endian order) */
    unsigned char cByte = static_cast<unsigned char>(cWord & 0xFFU);
    cWord >>= 8U;

    /* get the offset of the byte in the block */
    int offset = static_cast<int>((address + i) & ADDR_IN_BLK_MASK);

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
int storeByteRange(long srcAddr, long destAddr, int nBytes) {
  for (int i = 0; i < nBytes; i++) {
    unsigned long cWord = 0;

    /* read a single byte from the source address */
    int result = readBytes(srcAddr + i, 1, &cWord);
    if (result != 0) {
      return result;
    }

    /* store the byte to the destination address */
    result = storeBytes(destAddr + i, 1, static_cast<long>(cWord & 0xFFU));
    if (result != 0) {
      return result;
    }
  }

  return 0;
}

int allocBytes(long address, int nBytes) {
  return storeBytes(address, nBytes, UNDEFINED_WORD_OLD);
}

/**************************
 * returns the address of the memory block containing the
 * byte with the specified address.  A new memory block is
 * created if there is no memory block with the address.
 */
MEMBLK *getMemBlk(long address, bool create_flag) {
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

  /* mark the memory block as unused */
  memset(blk->used, 0, static_cast<size_t>(BLKSIZE / 8));

  return blk;
}

/**************************
 * stores a large number of bytes in memory blocks
 */
int bulkLoadBytes(long address, const char *byteFile, char *usedFile, long *totalBytes) {
  int nBytesRead = 0;
  int byteFd = -1;
  int usedFd = -1;
  struct stat st{};

  if (byteFile == nullptr || *byteFile == '\0') {
    fprintf(stderr, "No byte filename specified\n");
    return INVALID_BYTE_FILENAME;
  }

  if (stat(byteFile, &st) != 0) {
    perror("Memory file stat failed");
    return INVALID_BYTE_FILE;
  }

  long size = st.st_size;

  if (address == 0) {
    address = 2147483647L - size + 1L;
  }
  fprintf(stderr, "Loading %ld bytes from %s to 0x%08lx\n", size, byteFile, address);

  long nextAddress = address;
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
    nBytesRead = static_cast<int>(read(byteFd, data, BLKSIZE));

    /* if we haven't reached the end of the file's data */
    if (nBytesRead > 0) {
      /* get the memory block to store the next block of bytes in */
      cBlk = getMemBlk(nextAddress, true);

      if (cBlk == nullptr) {
        fprintf(stderr, "Cannot allocate memory for byte file\n");
        close(byteFd);
        if (usedFd >= 0)
          close(usedFd);
        return FAILED_MALLOC;
      }

      long blockOffset = nextAddress & ADDR_IN_BLK_MASK;

      /* copy the data into the memory block */
      memcpy(&(cBlk->data[blockOffset]), data,
             static_cast<size_t>(BLKSIZE) - static_cast<size_t>(blockOffset));

      /* if there is no used byte file, set all of the bytes to used */
      if (usedFd < 0) {
        /* set the bits in the used array to show which bytes have been stored */
        for (int i = 0; i < nBytesRead; i++) {
          /* set the used bit for the bytes that were read */
          byteUsedBit(cBlk, static_cast<int>(blockOffset + i), static_cast<int>(SET_BIT));
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

    *totalBytes += nBytesRead;
    nextAddress += nBytesRead;

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
  unsigned long nextAddress = 0xFFFFFFFFUL;
  MEMBLK *cBlk = nullptr;

  /* check the directory name */
  if (dirName == nullptr || *dirName == '\0') {
    fprintf(stderr, "No output filename specified\n");
    return INVALID_BYTE_FILENAME;
  }

  /* create the directory */
  if (mkdir(dirName, S_IRWXU) < 0) {
    return INVALID_DIR;
  }

  for (cBlk = memoryMap; cBlk != nullptr; cBlk = cBlk->next) {
    auto blkAddr = static_cast<unsigned long>(cBlk->startAddr);

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
    // TODO: why startAddr is a SIGNED long?
    // this will be a big task to refactor addresses
    if (blkAddr != nextAddress) {
      /* close the file if it is already open */
      if (dataFileFd > 0) {
        close(dataFileFd);
      }
      if (usedFileFd > 0) {
        close(usedFileFd);
      }

      /* create the file names */
      if (snprintf(dataFileName, NAME_SIZE, "%s/%08lx.bin", dirName, cBlk->startAddr) < 0) {
        return INVALID_OUT_FILE;
      }
      if (snprintf(usedFileName, NAME_SIZE, "%s/%08lx.use", dirName, cBlk->startAddr) < 0) {
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
    nextAddress += static_cast<unsigned long>(BLKSIZE);
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

  struct dirent *entry = nullptr;
  while ((entry = readdir(dirBlk)) != nullptr) { /* for each file */
    if (fnmatch("???????0.bin", entry->d_name, 0) != 0) {
      continue; // skip non-matching files
    }

    compat::println("d_name: {}", entry->d_name);

    long address = 0;
    if (sscanf(entry->d_name, "%8lx", &address) != 1) {
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

    long dataLength = 0;
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

  default:
    return "Unknown memory error";
  }
}
