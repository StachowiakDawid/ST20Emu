#include "memory.h"

#include "../../common/defines.h"
#include "../cpu/st20.h"

// posix / linux
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fnmatch.h>

// std
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* this structure hold the state of the memory */
typedef struct memblk_struct {
  long startAddr;
  unsigned char data[BLKSIZE];
  unsigned char used[BLKSIZE / 8];
  struct memblk_struct *next;
} MEMBLK;

/* this will hold the state of the memory */
static MEMBLK *memoryMap = NULL;

MEMBLK *getMemBlk(long, bool);
int byteUsedBit(MEMBLK *, int, int);

int memoryInit() {

  /* the emulator will get into an infinite loop if this initialization isn't done */
  storeBytes(0x3004L, 4, 0L);
  // below for sti5518 ASC1 20005000 base
  storeBytes(0x20005014L, 4, 0x00000000L);
  // below for sti5517
  //   storeBytes(0x200130f8L, 4, 0x00000001L);

  return (0);
}

/**************************
 * There is one bit to flag whether a byte in the data array
 * is used or not.  used[byte 0, bit 0] indicates whether data[0]
 * is used, used[byte 1, bit 0] indicates whether data[8] is used.
 *
 * returns true if the Used bit was set for the specified byte in
 * the specified memory block before this function was called
 */
int byteUsedBit(MEMBLK *block, int offset, int usedOperation) {
  int usedByteAddr = 0;
  int usedBitAddr = 0;
  int usedByte = 0;
  int usedBit = 0;

  /* get the address of the byte's used bit */
  usedByteAddr = (offset & ADDR_IN_BLK_MASK) >> 3;
  usedBitAddr = (offset & 7);

  /* get the state of the requested bit */
  usedByte = block->used[usedByteAddr];
  usedBit = usedByte & (1 << usedBitAddr);

  if (usedOperation == SET_BIT) {
    /* mark the bit as used */
    block->used[usedByteAddr] |= (1 << usedBitAddr);
  } else if (usedOperation == CLEAR_BIT) {
    /* clear the used bit */
    block->used[usedByteAddr] &= 0xFF - (1 << usedBitAddr);
  }

  /* return 1 if the used bit was originally set */
  return (usedBit != 0);
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
 * Returns TRUE if the bytes were read properly.
 */

int readBytes(long address, int nBytes, unsigned long *value) {
  int i;
  MEMBLK *cBlk;
  unsigned char cByte;
  unsigned long cWord = 0;
  int offset = 0;

  *value = UNDEFINED_WORD_OLD;

  /* this routine won't work with anything longer than a double word */
  if (nBytes > 4) {
    return (READ_TOO_LARGE);
  }

  /* read each of the requested bytes */
  for (i = 0; i < nBytes; i++) {

    /* get the block that the current byte is in */
    if ((cBlk = getMemBlk(address + i, false)) == NULL) {
      return (READ_UNUSED_MEM);
    }

    /* get the offset of the byte in the block */
    offset = (address + i) & ADDR_IN_BLK_MASK;

    /* if the byte isn't defined, return error */
    if (!byteUsedBit(cBlk, offset, DONT_ALTER_BIT)) {
      return (READ_UNUSED_MEM);
    }

    /* get the data byte */
    cByte = cBlk->data[offset] & 0xFF;

    /* build the return value from its component bytes */
    cWord += static_cast<unsigned long>(cByte) << (8 * i);
  }

  *value = cWord;

  return (0);
}

int readInvBytes(long address, int nBytes, long *value) {
  MEMBLK *cBlk = nullptr;
  unsigned char cByte = 0;
  unsigned long cWord = 0;
  int offset = 0;

  *value = UNDEFINED_WORD_OLD;

  /* this routine won't work with anything longer than a double word */
  if (nBytes > 4) {
    return READ_TOO_LARGE;
  }

  /* read each of the requested bytes */
  for (int i = 0; i < nBytes; i++) {

    /* get the block that the current byte is in */
    if ((cBlk = getMemBlk(address + i, false)) == nullptr) {
      return READ_UNUSED_MEM;
    }

    /* get the offset of the byte in the block */
    offset = static_cast<int>((address + i) & ADDR_IN_BLK_MASK);

    /* if the byte isn't defined, return error */
    if (!byteUsedBit(cBlk, offset, DONT_ALTER_BIT)) {
      return READ_UNUSED_MEM;
    }

    /* get the data byte */
    cByte = cBlk->data[offset] & 0xFFU;

    /* build the return value from its component bytes */
    cWord += static_cast<unsigned long>(cByte) << (8 * (3 - i));
  }

  *value = static_cast<long>(cWord);

  return 0;
}

/**************************
 * This function stores from one to four bytes in the proper memory block.
 *
 * Returns TRUE if the bytes were stored properly.
 */
int storeBytes(long address, int nBytes, long value) {
  MEMBLK *cBlk = nullptr;
  unsigned char cByte = 0;
  unsigned long cWord = static_cast<unsigned long>(value);
  int offset = 0;

  /* this routine won't work with anything longer than a double word */
  if (nBytes > 4) {
    return 0;
  }

  for (int i = 0; i < nBytes; i++) {

    /* get the block that the current byte is in */
    /* if the memory block can't be created, return an error */
    if ((cBlk = getMemBlk(address + i, true)) == nullptr) {
      return READ_UNUSED_MEM;
    }

    /* get each of the requested bytes */
    cByte = static_cast<unsigned char>(cWord & 0xFFU);
    cWord >>= 8;

    /* get the offset of the byte in the block */
    offset = static_cast<int>((address + i) & ADDR_IN_BLK_MASK);

    /* save the byte in the memory block */
    cBlk->data[offset] = cByte;

    /* mark the byte as used */
    byteUsedBit(cBlk, offset, static_cast<int>(SET_BIT));
  }

  return 0;
}

/**************************
 * This function stores from one to four bytes in the proper memory block.
 *
 * Returns TRUE if the bytes were stored properly.
 */
int storeByteRange(long srcAddr, long destAddr, int nBytes) {
  int result = 0;
  unsigned long cWord = 0;

  for (int i = 0; i < nBytes; i++) {

    /* get the block that the current byte is in */
    /* if the memory block can't be created, return an error */
    if ((result = readBytes(srcAddr + i, 1, &cWord)) != 0) {
      return result;
    }

    cWord &= 0xFFU;

    if ((result = storeBytes(destAddr + i, 1, static_cast<long>(cWord))) != 0) {
      return result;
    }
  }

  return 0;
}

int allocBytes(long address, int nBytes) {
  return (storeBytes(address, nBytes, UNDEFINED_WORD_OLD));
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
  DIR *dirBlk;
  struct dirent *entry;
  // int doneDir;
  char fileMask[NAME_SIZE];
  char byteFileName[NAME_SIZE];
  char usedFileName[NAME_SIZE];
  char *dotLoc;
  char addressCh[NAME_SIZE];
  long address;
  long dataLength = 0;
  int result = 0;

  if (sprintf(fileMask, "%s/???????0.bin", dirName) == EOF) {
    fprintf(stdout, "filemask failed\n");
    return (INVALID_OUT_FILE);
  }
  fprintf(stdout, "filemask: %s\n", fileMask);

  // TODO: this will fail because file mask is not actually a mask, just a string
  dirBlk = opendir(dirName);
  if (dirBlk == NULL) {
    fprintf(stdout, "opendir failed\n");
    return (INVALID_OUT_FILE);
  }
  fprintf(stdout, "opendir successed\n");

  while ((entry = readdir(dirBlk)) != NULL) { /* for each file */
    if (fnmatch("???????0.bin", entry->d_name, 0) != 0) {
      continue; // skip non-matching files
    }
    /* the name contains the address to load the file at */
    fprintf(stdout, "d_name: %s\n", entry->d_name);
    strcpy(addressCh, entry->d_name);
    fprintf(stdout, "address: %s\n", addressCh);
    if (sscanf(addressCh, "%8lx.bin", &address) != 1) {
      return (INVALID_OUT_FILE);
    }
    // fprintf (stderr, "%s   \n",dirBlk.name);
    /* add the directory name to the file name */
    strcpy(byteFileName, dirName);
    strcat(byteFileName, "/");
    strcat(byteFileName, entry->d_name);

    /* build the used byte filename from the data file name */
    strcpy(usedFileName, byteFileName);
    dotLoc = strrchr(usedFileName, '.');
    if (dotLoc == NULL) {
      return (INVALID_OUT_FILE);
    }
    /* put a null at the location of the period in the filename */
    *dotLoc = '\0';

    /* add the suffix for the used filename */
    strcat(usedFileName, ".use");
    fprintf(stderr, "%s   %s\n", byteFileName, usedFileName);
    result = bulkLoadBytes(address, byteFileName, usedFileName, &dataLength);
    if (result) {
      closedir(dirBlk);
      return (result);
    }

  } /* end of for each data file */
  closedir(dirBlk);
  return (0);
}

const char *memoryError(int error) {
  switch (error) {

  case INVALID_BYTE_FILENAME:
    return ("The byte filename is invalid");
    break;

  case INVALID_BYTE_FILE:
    return ("Cannot open byte file");
    break;

  case FAILED_MALLOC:
    return ("Ran out of room allocating memory for byte file");
    break;

  case READ_TOO_LARGE:
    return ("Cannot read more than four bytes at one time");
    break;

  case READ_UNUSED_MEM:
    return ("An uninit memory byte was accessed ");
    break;

  default:
    return ("Unknown memory error");
    break;
  }

  return (NULL);
}
