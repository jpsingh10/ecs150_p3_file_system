#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

/* TODO: Phase 1 */
#define SUPERBLOCK_INDEX 0
#define BLOCK_SIZE 4096
#define ENTRIES_PER_BLOCK 2048
#define FAT_EOC 0xFFFF
#define SIGNATURE "ECS150FS"

// Define superblock
// source:
// https://www.geeksforgeeks.org/structure-member-alignment-padding-and-data-packing/
struct superblock {
    char signature[8];
    uint16_t totalBlocks;
    uint16_t rootIndex;
    uint16_t dataStart;
    uint16_t dataBlocks;
    uint8_t fatBlocks;
    uint8_t unused[4079];
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
    uint8_t unused[10];
} __attribute__((packed));

// define fd table
struct fileDescriptor {
    uint16_t offet;
    int index;
    bool inUse;
}

// static struct superblock super;
// static struct superblock *superBlockPtr = &super;
static struct superblock *superBlockPtr;
static struct fat *fatArr;
static struct rootDir *rootDirArray;
static struct fileDescriptor *fdTable[FS_OPEN_MAX_COUNT];

int fs_mount(const char *diskname) {
    /* TODO: Phase 1 */
    // OPEN diskfile
    if (block_disk_open(diskname) == -1) {
        return -1;
    }
    // read superblock --
    superBlockPtr = (struct superblock *)malloc(sizeof(struct superblock)); //--
    if (superBlockPtr == NULL) {
        block_disk_close();
        return -1;
    }
    if (block_read(SUPERBLOCK_INDEX, superBlockPtr) == -1) {
        block_disk_close();
        return -1;
    }
    printf("sig %s\n", superBlockPtr->signature);
    // check if the signature is equal to "ECS150FS"
    for (unsigned int i = 0; i < sizeof(superBlockPtr->signature); i++) {
        if (superBlockPtr->signature[i] != SIGNATURE[i]) {
            return -1;
        }
    }

    // read FAT block
    fatArr = (struct fat *)malloc(superBlockPtr->fatBlocks * BLOCK_SIZE);
    if (fatArr == NULL) {
        block_disk_close();
        return -1;
    }

    for (unsigned int i = 1; i <= superBlockPtr->fatBlocks; i++) {
        // printf("i: %d\n", i);
        if (block_read(i, fatArr + (i - 1) * ENTRIES_PER_BLOCK) == -1) {
            block_disk_close();
            return -1;
        } else if (i == superBlockPtr->fatBlocks &&
                   (superBlockPtr->dataBlocks % ENTRIES_PER_BLOCK) != 0) {
            // printf("Last one: %d\n", i);
            if (block_read(i, fatArr + (i - 1) * (superBlockPtr->dataBlocks %
                                                  ENTRIES_PER_BLOCK)) == -1) {
                block_disk_close();
                return -1;
            }
        }
    }
    // printf("First entry 0 %d\n", fatArr[0].content);
    /*
    if(fatArr[0].content == FAT_EOC)
    {
        printf("Yes it is equal to FAT EOC\n");
    }
    */

    // Read root directory
    // printf("Size of root dir: %d\n", sizeof(struct rootDir));
    rootDirArray =
        (struct rootDir *)malloc(FS_FILE_MAX_COUNT * sizeof(struct rootDir));
    if (rootDirArray == NULL) {
        block_disk_close();
        return -1;
    }
    if (block_read(superBlockPtr->rootIndex, rootDirArray) == -1) {
        block_disk_close();
        return -1;
    }
    // printf("root dir first data: %d\n", rootDirArray[1].fileSize);
    /*
    printf("first entry file name: %d\n", rootDirArray[0].fileSize);
    if(strcmp(rootDirArray[0].fileName, "\0") == 0)
    {
        printf("File name is NULL \n");
    }
    */

    return 0;
}

int fs_umount(void) {
    /* TODO: Phase 1 */

    // printf("Unmount:\n");
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        // printf("fatArr or rootDirArray is NULL\n");
        return -1;
    }

    if (block_disk_close() == -1) {
        // printf("Unmount block_disk_close() == -1");
        return -1;
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
    int fatFreeBlockCount = 0;
    for (unsigned int i = 0; i < superBlockPtr->dataBlocks; i++) {
        if (fatArr[i].content == 0 && fatArr[i].content != FAT_EOC) {
            fatFreeBlockCount += 1;
        }
    }
    // count free blocks in root dir
    int rootDirFreeBlockCount = 0;
    for (unsigned int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (strcmp(rootDirArray[i].fileName, "\0") == 0) {
            rootDirFreeBlockCount += 1;
        }
    }

    printf("FS Info:\n");
    printf("total_blk_count=%d\n", superBlockPtr->totalBlocks);
    printf("fat_blk_count=%d\n", superBlockPtr->fatBlocks);
    printf("rdir_blk=%d\n", superBlockPtr->rootIndex);
    printf("data_blk=%d\n", superBlockPtr->dataStart);
    printf("data_blk_count=%d\n", superBlockPtr->dataBlocks);
    printf("fat_free_ratio=%d/%d\n", fatFreeBlockCount,
           superBlockPtr->dataBlocks);
    printf("rdir_free_ratio=%d/%d\n", rootDirFreeBlockCount, FS_FILE_MAX_COUNT);

    // printf("calling fs_create: %d\n", fs_create("hello"));

    return 0;
}

int fs_create(const char *filename) {
    /* TODO: Phase 2 */
    // check if FS is mounted
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }
    // check if it is null terminated
    for (unsigned int i = 0; i < sizeof(filename); i++) {
        if (filename[i] == '\0') {
            return -1;
        }
    }
    // check if the length of the filename is longer than FS_FILENAME_LEN
    if (strlen(filename) > FS_FILENAME_LEN) {
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
        if (strcmp(rootDirArray[i].fileName, filename) == 0) {
            freeSpace += 1;
        }
    }
    if (freeSpace == 0) {
        return -1;
    }
    // create new file
    for (unsigned int i = 0; i < FS_FILE_MAX_COUNT; i++) {
        if (strcmp(rootDirArray[i].fileName, "\0") == 0) {
            strcpy(rootDirArray[i].fileName, filename);
            rootDirArray[i].fileSize = 0;
            rootDirArray[i].firstBlock = FAT_EOC;
            break;
        }
    }

    return 0;
}

int fs_delete(const char *filename) {
    /* TODO: Phase 2 */
    // check if FS is mounted
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    // check if the file is valid
    // check if it is null terminated
    for (unsigned int i = 0; i < sizeof(filename); i++) {
        if (filename[i] == '\0') {
            return -1;
        }
    }
    // check if the length of the filename is longer than FS_FILENAME_LEN
    if (strlen(filename) > FS_FILENAME_LEN) {
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
    //## TO DO LATER

    // delete the file
    strcpy(rootDirArray[targetIndex].fileName, "\0");
    rootDirArray[targetIndex].fileSize = 0;
    rootDirArray[targetIndex].firstBlock = 0;

    return 0;
}

int fs_ls(void) {
    /* TODO: Phase 2 */
    // check if FS is mounted
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    printf("FS LS:\n");

    for (unsigned int i = 0, i < FS_FILE_MAX_COUNT : i++) {
        if(strcmp(rootDirArray[i].fileName[0], != "\0"){
            printf("file: %s, size: %d, data_blk: %d\n",
                   rootDirArray[i].filename, rootDirArray[i].fileSize,
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
    if (filename == NULL || srtcmp(filename) > FS_FILENAME_LEN)
        return -1;

    // check if the file is in root dir
    int found = 0;
    int targetIndeix = -1;
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

    if (fdIndex == -1) {
        return -1; // No available file descriptor slot
    }
    // Initialize the file descriptor
    fdTable[fdIndex]->offset = 0;
    fdTable[fdIndex]->index = targetIndex;
    fdTable[fdIndex]->inUse = true;

    return fdIndex;
}

int fs_close(int fd) {
    /* TODO: Phase 3 */
    if (superBlockPtr == NULL || fatArr == NULL || rootDirArray == NULL) {
        return -1;
    }

    // Check if the file descriptor is valid
    if (fdTable < 0 || fdTable >= FS_OPEN_MAX_COUNT || !fdTable[fd]->inUse) {
        return -1;
    }

    fdTable[fd]->inUse = false;
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
    if (fdTable < 0 || fdTable >= FS_OPEN_MAX_COUNT || !fdTable[fd]->inUse) {
        return -1;
    }

    int fileSize = rootDirArray[fdTable[fd]->index].fileSize;

    return fileSize;
}

int fs_lseek(int fd, size_t offset) {
    /* TODO: Phase 3 */

    return 0;
}

int fs_write(int fd, void *buf, size_t count) {
    /* TODO: Phase 4 */
    return 0;
}

int fs_read(int fd, void *buf, size_t count) {
    /* TODO: Phase 4 */
    return 0;
}
