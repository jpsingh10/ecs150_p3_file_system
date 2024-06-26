#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

/* TODO: Phase 1 */
#define SUPERBLOCK_INDEX 0
#define ENTRIES_PER_BLOCK BLOCK_SIZE / 2
#define FAT_EOC 0xFFFF
#define SIGNATURE "ECS150FS"
#define SIG_LENGTH 8
#define UNUSED_LENGTH_SUPER 4079
#define UNUSED_LENGTH_ROOT 10

// Define superblock
// source:
// https://www.geeksforgeeks.org/structure-member-alignment-padding-and-data-packing/
struct superblock {
    char signature[SIG_LENGTH];
    uint16_t totalBlocks;
    uint16_t rootIndex;
    uint16_t dataStart;
    uint16_t dataBlocks;
    uint8_t fatBlocks;
    uint8_t unused[UNUSED_LENGTH_SUPER];
} __attribute__((packed));

// Define FAT
struct fat {
    uint16_t content;
} __attribute__((packed));

// Define root directory
struct rootDir {
    char fileName[FS_FILENAME_LEN];
    uint32_t fileSize;
    uint16_t firstBlock;
    uint8_t unused[UNUSED_LENGTH_ROOT];
} __attribute__((packed));

// define fd table
struct fileDescriptor {
    uint16_t offset;
    int index;
    int inUse;
} __attribute__((packed));

static struct superblock *superBlockPtr;
static struct fat *fatArr;
static struct rootDir *rootDirArray;
static struct fileDescriptor *fdTable[FS_OPEN_MAX_COUNT];

int checkFileName(const char *filename) {
    // check if it is null terminated
    // check if the length of the filename is longer than FS_FILENAME_LEN
    if (filename[strlen(filename)] != '\0' || strlen(filename) > FS_FILENAME_LEN) {
        return -1;
    }

    return 0;
}

int fs_mount(const char *diskname) {
    /* TODO: Phase 1 */
    // OPEN diskfile
    if (block_disk_open(diskname) == -1) {
        return -1;
    }

    superBlockPtr = (struct superblock *)malloc(sizeof(struct superblock)); 
    if (superBlockPtr == NULL) {
        return -1;
    }
    // read superblock 
    if (block_read(SUPERBLOCK_INDEX, superBlockPtr) == -1) {
        return -1;
    }

    //	check if the signature is equal to "ECS150FS"
    for (unsigned int i = 0; i < sizeof(superBlockPtr->signature); i++) {
        if (superBlockPtr->signature[i] != SIGNATURE[i]) {
            return -1;
        }
    }

    // check if the total number of blocks is equal to what the function
    // block_disk_count() returns
    if (superBlockPtr->totalBlocks != block_disk_count()) {
        return -1;
    }

    fatArr = (struct fat *)malloc(superBlockPtr->fatBlocks * sizeof(struct fat) * ENTRIES_PER_BLOCK);
    rootDirArray = (struct rootDir *)malloc(FS_FILE_MAX_COUNT * sizeof(struct rootDir));
    if (fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    // read FAT block
    int remainingEntries = superBlockPtr->dataBlocks % ENTRIES_PER_BLOCK;
    for (unsigned int i = 1; i <= superBlockPtr->fatBlocks; i++) {
        if (remainingEntries == 0) {
            if (block_read(i, fatArr + ((i - 1) * ENTRIES_PER_BLOCK)) == -1) {
                return -1;
            }
        } else if (remainingEntries != 0) {
            if (i < superBlockPtr->fatBlocks) {
                if (block_read(i, fatArr + ((i - 1) * ENTRIES_PER_BLOCK)) == -1) {
                    return -1;
                }
            } else if (i == superBlockPtr->fatBlocks) {
                if (block_read(i, fatArr + ((i - 1) * remainingEntries)) == -1) {
                    return -1;
                }
            }
        }
    }

    // Read root directory
    if (block_read(superBlockPtr->rootIndex, rootDirArray) == -1) {
        return -1;
    }

    return 0;
}

int fs_umount(void) {
    /* TODO: Phase 1 */
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    if (block_disk_close() == -1) {
        return -1;
    }
    // check if there are still open file descriptors
    for (unsigned int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
        if (fdTable[i] != NULL) {
            return -1;
        }
    }

    free(superBlockPtr);
    free(fatArr);
    free(rootDirArray);

    return 0;
}

int fs_info(void) {
    /* TODO: Phase 1 */
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }
    
    // count free data blocks
    int fatFreeEntriesCount = 0;
    for (unsigned int i = 0; i < superBlockPtr->dataBlocks; i++) {
        if (fatArr[i].content == 0 && fatArr[i].content != FAT_EOC) {
            fatFreeEntriesCount += 1;
        }
    }
    
    // count free blocks in root dir
    int rootDirFreeEntriesCount = 0;
    for (unsigned int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (strcmp(rootDirArray[i].fileName, "\0") == 0) {
            rootDirFreeEntriesCount += 1;
        }
    }

    printf("FS Info:\n");
    printf("total_blk_count=%d\n", superBlockPtr->totalBlocks);
    printf("fat_blk_count=%d\n", superBlockPtr->fatBlocks);
    printf("rdir_blk=%d\n", superBlockPtr->rootIndex);
    printf("data_blk=%d\n", superBlockPtr->dataStart);
    printf("data_blk_count=%d\n", superBlockPtr->dataBlocks);
    printf("fat_free_ratio=%d/%d\n", fatFreeEntriesCount,
           superBlockPtr->dataBlocks);
    printf("rdir_free_ratio=%d/%d\n", rootDirFreeEntriesCount,
           FS_FILE_MAX_COUNT);

    return 0;
}

int fs_create(const char *filename) {
    /* TODO: Phase 2 */
    // check if FS is mounted
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    // check if filename is valid
    if (checkFileName(filename) == -1) {
        return -1;
    }

    // check if the file already exists
    for (unsigned int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (strcmp(rootDirArray[i].fileName, filename) == 0) {
            return -1;
        }
    }

    // check if root dir already contains FS_FILE_MAX_COUNT files
    int freeSpace = 0;
    for (unsigned int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (strcmp(rootDirArray[i].fileName, "\0") == 0) {
            freeSpace += 1;
        }
    }
    if (freeSpace == 0) {
        return -1;
    }

    // create new file
    for (unsigned int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (rootDirArray[i].fileName[0] == '\0') {
            strcpy(rootDirArray[i].fileName, filename);
            rootDirArray[i].fileSize = 0;
            rootDirArray[i].firstBlock = FAT_EOC;
            break;
        }
    }
    block_write(superBlockPtr->rootIndex, rootDirArray);

    return 0;
}

int fs_delete(const char *filename) {
    /* TODO: Phase 2 */
    // check if FS is mounted
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    // check if filename is valid
    if (checkFileName(filename) == -1) {
        return -1;
    }

    // check if the file is in root dir
    int found = 0;
    int targetIndex = 0;
    for (unsigned int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (strcmp(rootDirArray[i].fileName, filename) == 0) {
            found = 1;
            targetIndex = i;
            break;
        }
    }
    if (found == 0) {
        return -1;
    }

    // check if the file is currently open
    for (unsigned int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
        if (fdTable[i] != NULL && fdTable[i]->index == found &&
            fdTable[i]->inUse) {
            return -1;
        }
    }

    // delete the file
    strcpy(rootDirArray[targetIndex].fileName, "\0");
    rootDirArray[targetIndex].fileSize = 0;
    rootDirArray[targetIndex].firstBlock = 0;
    block_write(superBlockPtr->rootIndex, rootDirArray);

    return 0;
}

int fs_ls(void) {
    /* TODO: Phase 2 */
    // check if FS is mounted
    // we can move this to a function later on
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    printf("FS LS:\n");

    for (unsigned int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (rootDirArray[i].fileName[0] != '\0') {
            printf("file: %s, size: %d, data_blk: %d\n",
                   rootDirArray[i].fileName, rootDirArray[i].fileSize,
                   rootDirArray[i].firstBlock);
        }
    }

    return 0;
}

int fs_open(const char *filename) {
    /* TODO: Phase 3 */
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    // check if file is valid
    if (checkFileName(filename) == -1) {
        return -1;
    }

    // check if the file is in root dir
    int found = 0;
    int targetIndex = -1;
    for (unsigned int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (strcmp(rootDirArray[i].fileName, filename) == 0) {
            found = 1;
            targetIndex = i;
            break;
        }
    }
    if (!found) {
        return -1;
    }

    // check if there already %FS_OPEN_MAX_COUNT files currently open
    int availableFDCount = 0;
    for (unsigned int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
        if (fdTable[i] == NULL) {
            availableFDCount += 1;
        }
    }
    if (availableFDCount == 0) {
        return -1;
    }

    // Find a free file descriptor
    int fdIndex = -1;
    for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
        if (fdTable[i] == NULL) {
            fdTable[i] = malloc(sizeof(struct fileDescriptor));
            if (fdTable[i] == NULL) {
                return -1;
            }
            fdIndex = i;
            break;
        }
    }
    // free one couldn't be found
    if (fdIndex == -1) {
        return -1;
    }

    // Initialize the file descriptor
    fdTable[fdIndex]->offset = 0;
    fdTable[fdIndex]->index = targetIndex;
    fdTable[fdIndex]->inUse = 1;

    return fdIndex;
}

int fs_close(int fd) {
    /* TODO: Phase 3 */
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    // Check if the file descriptor is valid
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT || fdTable[fd] == NULL || !fdTable[fd]->inUse) {
        return -1;
    }

    fdTable[fd]->inUse = 0;
    free(fdTable[fd]);
    fdTable[fd] = NULL;

    return 0;
}

int fs_stat(int fd) {
    /* TODO: Phase 3 */
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    // Check if the file descriptor is valid
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT || fdTable[fd] == NULL || !fdTable[fd]->inUse) {
        return -1;
    }

    int fileSize = rootDirArray[fdTable[fd]->index].fileSize;

    return fileSize;
}

int fs_lseek(int fd, size_t offset) {
    /* TODO: Phase 3 */
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    // Check if the file descriptor is valid
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT || fdTable[fd] == NULL || !fdTable[fd]->inUse) {
        return -1;
    }

    // Check if offset is larger than the current file size
    int fileSize = fs_stat(fd);
    if (offset > (size_t)fileSize) {
        return -1;
    }

    fdTable[fd]->offset = offset;
    return 0;
}


int findDataBlockIndex(int fd) {

    int targetIndex = 0;

    int fileOffset = fdTable[fd]->offset; // current file offset

    int dataBlockNum;
    dataBlockNum = (fileOffset / BLOCK_SIZE) + 1; // find the postion of the data block based on offset

    int startIndex = rootDirArray[fdTable[fd]->index].firstBlock; // first data block index

    int count = 1;
    while (fatArr[startIndex].content != 0 && fatArr[startIndex].content != FAT_EOC) {
        if (count == dataBlockNum) {
            break;
        }
        startIndex = fatArr[startIndex].content;
        count += 1;
    }

    targetIndex = startIndex;

    return targetIndex;
}

int find_empty_FAT_entry(void) {
    for (int i = 0; i < superBlockPtr->dataBlocks; i++) {
        if (fatArr[i].content == 0)
            return i;
    }
    return -1;
}

int fs_write(int fd, void *buf, size_t count) {
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    // Validate file descriptor
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT || fdTable[fd] == NULL ||
        fdTable[fd]->inUse == 0) {
        return -1;
    }

    if (buf == NULL || count == 0) {
        return -1;
    }

    if (rootDirArray[fdTable[fd]->index].firstBlock == FAT_EOC) {
        int emptyFATIndex = -1;
        for (int i = 0; i < superBlockPtr->dataBlocks; i++) {
            if (fatArr[i].content == 0)
                emptyFATIndex = i;
        }
        if (emptyFATIndex == -1) {
            return 0;
        }

        rootDirArray[fdTable[fd]->index].firstBlock = emptyFATIndex;
        fatArr[emptyFATIndex].content = FAT_EOC;

        if (block_write(superBlockPtr->rootIndex, rootDirArray) == -1) {
            return 0;
        }
    }

    uint16_t currentBlockIndex = rootDirArray[fdTable[fd]->index].firstBlock;
    uint16_t previousBlockIndex = currentBlockIndex;

    // go to the block based on the offest
    for (int i = 0; i < fdTable[fd]->offset / BLOCK_SIZE &&
                    fatArr[currentBlockIndex].content != FAT_EOC;
         i++) {
        previousBlockIndex = currentBlockIndex;
        currentBlockIndex = fatArr[currentBlockIndex].content;
    }

    uint8_t writeBuffer[BLOCK_SIZE];
    int totalWritten = 0;

    while (count > 0) {
        if (currentBlockIndex == FAT_EOC) {
            int newFATIndex = -1;

            for (int i = 0; i < superBlockPtr->dataBlocks; i++) {
                if (fatArr[i].content == 0)
                    newFATIndex = i;
            }
            if (newFATIndex == -1)
                break;

            fatArr[previousBlockIndex].content = newFATIndex;
            currentBlockIndex = newFATIndex;
            fatArr[currentBlockIndex].content = FAT_EOC;
        }

        // Read current block into buffer to handle partial writes
        if (block_read(currentBlockIndex + superBlockPtr->dataStart,
                       writeBuffer) == -1) {
            break;
        }

        // Determine bytes to write in this iteration
        size_t bytesToWriteThisIteration =
            count < (size_t)(BLOCK_SIZE - (fdTable[fd]->offset % BLOCK_SIZE))
                ? count
                : (size_t)(BLOCK_SIZE - (fdTable[fd]->offset % BLOCK_SIZE));

        // write the blocks
        memcpy(writeBuffer + (fdTable[fd]->offset % BLOCK_SIZE),
               buf + totalWritten, bytesToWriteThisIteration);
        if (block_write(currentBlockIndex + superBlockPtr->dataStart,
                        writeBuffer) == -1) {
            break;
        }

        // Update offsets and count
        totalWritten += bytesToWriteThisIteration;
        count -= bytesToWriteThisIteration;
        fdTable[fd]->offset += bytesToWriteThisIteration;
        previousBlockIndex = currentBlockIndex;
        currentBlockIndex = fatArr[previousBlockIndex].content;
    }

    // Update file size in root directory
    rootDirArray[fdTable[fd]->index].fileSize =
        fdTable[fd]->offset > rootDirArray[fdTable[fd]->index].fileSize
            ? fdTable[fd]->offset
            : rootDirArray[fdTable[fd]->index].fileSize;

    // Write root directory and FAT back to disk
    if (block_write(superBlockPtr->rootIndex, rootDirArray) == -1) {
        return -1;
    }

    return totalWritten;
}
int fs_read(int fd, void *buf, size_t count) {
    /* TODO: Phase 4 */

    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    // Check if the file descriptor is valid
    if (fd < 0 || fd >= FS_OPEN_MAX_COUNT || fdTable[fd] == NULL || fdTable[fd]->inUse == 0) {
        return -1;
    }

    if (buf == NULL) {
        return -1;
    }

    int bytesRead = 0;

    int dataBlockIndexArr[superBlockPtr->dataBlocks];
    int startIndex = findDataBlockIndex(fd); 

    int index = 0;
    dataBlockIndexArr[index] = startIndex;
    index += 1;

    while (fatArr[startIndex].content != 0 && fatArr[startIndex].content != FAT_EOC) {
        startIndex = fatArr[startIndex].content;

        dataBlockIndexArr[index] = startIndex;

        index += 1;
    }

    // find the real indexes of data block
    for (int i = 0; i < index; i++) {
        dataBlockIndexArr[i] += superBlockPtr->dataStart;
    }

    char *bounceBuff = malloc(BLOCK_SIZE * (sizeof(dataBlockIndexArr) / sizeof(int)));

    for (int i = 0; i < index; i++) {
        block_read(dataBlockIndexArr[i], bounceBuff + i * BLOCK_SIZE);
    }

    if (count > (size_t)(BLOCK_SIZE * index - (fdTable[fd]->offset % BLOCK_SIZE))) {
        memcpy(buf, bounceBuff + (fdTable[fd]->offset % BLOCK_SIZE), BLOCK_SIZE * index - (fdTable[fd]->offset % BLOCK_SIZE));

        bytesRead = BLOCK_SIZE * index - (fdTable[fd]->offset % BLOCK_SIZE);
    } else if (count <= (size_t)(BLOCK_SIZE * index - (fdTable[fd]->offset % BLOCK_SIZE))) {
        memcpy(buf, bounceBuff + (fdTable[fd]->offset % BLOCK_SIZE), count);

        bytesRead = count;
    }

    // increase the offset
    fdTable[fd]->offset += bytesRead;

    free(bounceBuff);

    return bytesRead; 
}
