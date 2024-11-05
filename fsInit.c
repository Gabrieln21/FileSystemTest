// fsInit.c

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "fsLow.h"
#include "mfs.h"
#include "vcb.h"

// Global variables
struct VolumeControlBlock* vcb = NULL;
struct FATEntry* fat = NULL;
struct DirectoryEntry *rootDir = NULL;  // Global root directory pointer
char currentWorkingDirectory[MAX_FILENAME_LENGTH]; // Global current working directory
struct DirectoryEntry *loadedCWD = NULL; // Global loaded CWD


// Function prototypes
int initializeFAT(uint64_t blockSize, uint64_t totalBlocks);
int initializeRootDirectory(uint64_t blockSize);
struct DirectoryEntry* loadDir(struct DirectoryEntry* entry); // Add this prototype
int allocateBlocks(int numBlocks, struct VolumeControlBlock vcb);


int initFileSystem(uint64_t numberOfBlocks, uint64_t blockSize) {
    printf("Initializing File System with %ld blocks with a block size of %ld\n", numberOfBlocks,blockSize);

    // Allocate and initialize VCB
    vcb = (struct VolumeControlBlock*)malloc(blockSize);
    if (vcb == NULL) {
        printf("Error: Unable to allocate memory for VCB\n");
        return -1;
    }

    // Read VCB from disk 
    if (LBAread(vcb, 1, 1) != 1) { 
        printf("Error: Unable to read VCB from disk\n");
        free(vcb);
        return -1;
    }

    if (vcb->signature != FS_SIGNATURE) {
        printf("No valid file system found. Formatting volume...\n");

        // Clear VCB memory
        memset(vcb, 0, blockSize);

        // Initialize VCB fields
        vcb->signature = FS_SIGNATURE;  
        strcpy(vcb->volumeName, "CSC415_FS");
        vcb->blockSize = blockSize;
        vcb->totalBlocks = numberOfBlocks;
        vcb->freeBlocks = numberOfBlocks; // Initialize with all blocks free 
        vcb->creationTime = time(NULL);
        vcb->lastMountedTime = vcb->creationTime;

        // Write the initial VCB to disk (before initializing FAT)
        if (LBAwrite(vcb, 1, 1) != 1) { 
            printf("Error: Unable to write VCB to disk\n");
            free(vcb);
            return -1;
        }

        // Initialize FAT (moved before initializeRootDirectory)
        int fatStart = initializeFAT(blockSize, numberOfBlocks); 
        if (fatStart < 0) {
            printf("Error: Failed to initialize FAT\n");
            free(vcb);
            return -1;
        }
        vcb->fatStart = fatStart;

        // Initialize root directory
        int rootDirStart = initializeRootDirectory(blockSize);
        if (rootDirStart < 0) {
            printf("Error: Failed to initialize root directory\n");
            free(vcb);
            return -1;
        }
        vcb->rootDirectory = rootDirStart;

        // Adjust free blocks in VCB (after initializing FAT and root directory)
        vcb->freeBlocks -= (vcb->fatBlocks + (rootDirStart - vcb->dataStart)); 

        // Write the updated VCB to disk
        if (LBAwrite(vcb, 1, 1) != 1) { 
            printf("Error: Unable to write VCB to disk\n");
            free(vcb);
            return -1;
        }

        printf("File system initialized successfully!\n");
    } else {
        printf("Found existing file system. Loading...\n");
        vcb->lastMountedTime = time(NULL);
        vcb->mountCount++;

        if (LBAwrite(vcb, 1, 1) != 1) { 
            printf("Error: Unable to update VCB on disk\n");
            free(vcb);
            return -1;
        }
    }

    // Load the root directory
    rootDir = loadDir(&(struct DirectoryEntry){.firstBlockIndex = vcb->rootDirectory, .fileSize = 51 * sizeof(struct DirectoryEntry)}); 
    if (rootDir == NULL) {
        printf("Error: Failed to load root directory\n");
        return -1;
    }

    // Initialize other global variables
    strcpy(currentWorkingDirectory, "/");
    loadedCWD = loadDir(&rootDir[0]); 

    // Print VCB values for verification
    printf("\nVCB Values:\n");
    printf("  volumeName: %s\n", vcb->volumeName);
    printf("  signature: 0x%lx\n", vcb->signature);
    printf("  fatStart: %lu\n", vcb->fatStart);
    printf("  fatBlocks: %lu\n", vcb->fatBlocks);
    printf("  dataStart: %lu\n", vcb->dataStart);
    printf("  freeBlocks: %lu\n", vcb->freeBlocks); 
    printf("  rootDirectory: %lu\n", vcb->rootDirectory); 
    // ... print other VCB fields ...

    return 0;
}

int initializeFAT(uint64_t blockSize, uint64_t totalBlocks) {
    printf("\n============= FAT Initialization Debug =============\n");

    // Calculate FAT size
    uint64_t fatEntries = totalBlocks;
    uint64_t fatBytes = fatEntries * sizeof(struct FATEntry);
    uint64_t fatBlocks = (fatBytes + blockSize - 1) / blockSize;
    size_t fatSize = fatBlocks * blockSize;

    // Print size calculations
    printf("1. Size Calculations:\n");
    printf("   Total blocks: %lu\n", totalBlocks);
    printf("   FAT entries: %lu\n", fatEntries);
    printf("   FAT bytes: %lu\n", fatBytes);
    printf("   FAT blocks: %lu\n", fatBlocks);
    printf("   Total FAT size: %zu bytes\n", fatSize);
    printf("   sizeof(FATEntry): %zu\n", sizeof(struct FATEntry));
    printf("   FAT_FREE value: 0x%llx\n", (unsigned long long)FAT_FREE);
    printf("   FAT_EOF value: 0x%llx\n", (unsigned long long)FAT_EOF);

    // Allocate memory for FAT
    printf("\n2. Allocating FAT memory...\n");
    fat = (struct FATEntry*)calloc(fatSize, 1); 
    if (fat == NULL) {
        printf("Error: Failed to allocate FAT memory\n");
        return -1;
    }

    printf("   Memory allocated at address: %p\n", (void*)fat);
    printf("   Memory is pre-zeroed by calloc\n");

    // Verify initial state
    printf("\n3. Verifying initial state:\n");
    printf("   First 32 bytes after allocation:\n   ");
    unsigned char* ptr = (unsigned char*)fat;
    for(int i = 0; i < 32; i++) {
        printf("%02x ", ptr[i]);
        if((i + 1) % 16 == 0) printf("\n   ");
    }

    // Mark system blocks as used
    printf("\n4. Setting VCB block (Entry 0):\n");
    fat[0].nextBlock = FAT_EOF;  // VCB
    printf("   Value of fat[0].nextBlock: 0x%llx\n", (unsigned long long)fat[0].nextBlock);

    // Mark the FAT blocks
    printf("\n5. Marking FAT blocks as used:\n");
    for (uint64_t i = 1; i < fatBlocks + 1; i++) {
        fat[i].nextBlock = (i == fatBlocks) ? FAT_EOF : i + 1;
        printf("   fat[%lu].nextBlock = 0x%llx\n", i, (unsigned long long)fat[i].nextBlock);
    }

    // Update VCB fields
    printf("\n6. Updating VCB fields:\n");
    vcb->fatStart = 2; // FAT starts at block 2
    vcb->fatBlocks = fatBlocks;
    vcb->dataStart = vcb->fatStart + fatBlocks; // Data starts after FAT
    vcb->freeBlocks = totalBlocks - (vcb->dataStart); // Adjust free blocks
    vcb->fatEntryCount = fatEntries;
    printf("   vcb->fatStart: %lu\n", vcb->fatStart);
    printf("   vcb->fatBlocks: %lu\n", vcb->fatBlocks);
    printf("   vcb->dataStart: %lu\n", vcb->dataStart);
    printf("   vcb->freeBlocks: %lu\n", vcb->freeBlocks);
    printf("   vcb->fatEntryCount: %lu\n", vcb->fatEntryCount);

    // Write FAT to disk in chunks
    printf("\n7. Writing FAT to disk in chunks:\n");
    const uint64_t CHUNK_SIZE = 8;
    uint64_t blocksWritten = 0;

    while (blocksWritten < fatBlocks) {
        uint64_t blocksToWrite = (fatBlocks - blocksWritten) < CHUNK_SIZE ?
                                 (fatBlocks - blocksWritten) : CHUNK_SIZE;

        printf("   Writing %lu blocks starting at block %lu\n", blocksToWrite, vcb->fatStart + blocksWritten);
        int writeResult = LBAwrite(
                (char*)fat + (blocksWritten * blockSize),
                blocksToWrite,
                vcb->fatStart + blocksWritten
        );

        if (writeResult != blocksToWrite) {
            printf("   Error: Chunk write failed at block %lu\n", blocksWritten);
            free(fat);
            fat = NULL;
            return -1;
        }
        blocksWritten += blocksToWrite;
        printf("   Wrote %lu blocks, total %lu/%lu\n",
               blocksToWrite, blocksWritten, fatBlocks);
    }

    // Verify written blocks
    printf("\n8. Verifying FAT blocks:\n");
    unsigned char* verifyBuffer = malloc(blockSize);
    if (verifyBuffer) {
        if (LBAread(verifyBuffer, 1, vcb->fatStart) == 1) {
            printf("   First block starts with: ");
            for(int i = 0; i < 8; i++) {
                printf("%02x ", verifyBuffer[i]);
            }
            printf("\n");
        }

        if (LBAread(verifyBuffer, 1, vcb->fatStart + (fatBlocks/2)) == 1) {
            printf("   Middle block starts with: ");
            for(int i = 0; i < 8; i++) {
                printf("%02x ", verifyBuffer[i]);
            }
            printf("\n");
        }

        free(verifyBuffer);
    }

    printf("\n============= FAT Initialization Complete =============\n\n");
    return vcb->fatStart;
}

int initializeRootDirectory(uint64_t blockSize) {
    printf("\n============= Root Directory Initialization =============\n");

    const int NUM_DE = 50;
    uint64_t dirSize = sizeof(struct DirectoryEntry) * NUM_DE;
    uint64_t dirBlocks = (dirSize + blockSize - 1) / blockSize;

    printf("Directory size: %lu bytes, Directory blocks: %lu\n", dirSize, dirBlocks); // Debug

    // Allocate memory for root directory entries
    struct DirectoryEntry* rootDirEntries = malloc(dirBlocks * blockSize); 
    if (rootDirEntries == NULL) {
        printf("Error: Failed to allocate root directory memory\n");
        return -1;
    }
    printf("Allocated memory for root directory entries at: %p\n", rootDirEntries); // Debug

    // Clear memory
    memset(rootDirEntries, 0, dirBlocks * blockSize);

    // Get blocks for directory from FAT
    printf("Allocating blocks for root directory...\n"); // Debug
    int startBlock = allocateBlocks(dirBlocks, *vcb); 
    if (startBlock == -1) {
        printf("Error: Failed to allocate blocks for root directory\n");
        free(rootDirEntries);
        return -1;
    }
    printf("Allocated blocks for root directory starting at block: %d\n", startBlock); // Debug

    // Initialize "." entry
    strcpy(rootDirEntries[0].filename, ".");
    rootDirEntries[0].fileSize = dirSize; 
    rootDirEntries[0].firstBlockIndex = startBlock;
    rootDirEntries[0].creationTime = time(NULL);
    rootDirEntries[0].lastModifiedTime = rootDirEntries[0].creationTime;
    rootDirEntries[0].fileType = 1;  // Directory
    rootDirEntries[0].inUse = 1;

    // Initialize ".." entry (same as "." for root)
    memcpy(&rootDirEntries[1], &rootDirEntries[0], sizeof(struct DirectoryEntry)); 
    strcpy(rootDirEntries[1].filename, "..");

    // Write directory to disk
    printf("Writing root directory to disk...\n"); 
    printf("  rootDirEntries: %p\n", (void *)rootDirEntries); // Corrected print statement
    printf("  dirBlocks: %lu\n", dirBlocks);          // Corrected print statement
    printf("  startBlock: %d\n", startBlock);        // Corrected print statement
    if (LBAwrite(rootDirEntries, dirBlocks, startBlock) != dirBlocks) { 
        printf("Error: Failed to write root directory\n");
        free(rootDirEntries);
        return -1;
    }
    printf("Root directory written to disk successfully.\n"); // Debug

    // Update FAT
    printf("Updating FAT...\n"); // Debug
    for (uint64_t i = 0; i < dirBlocks - 1; i++) {
        fat[startBlock + i].nextBlock = startBlock + i + 1;
    }
    fat[startBlock + dirBlocks - 1].nextBlock = FAT_EOF;
    

    // Write updated FAT
    if (LBAwrite(fat, vcb->fatBlocks, vcb->fatStart) != vcb->fatBlocks) {
        printf("Error: Failed to write updated FAT\n");
        free(rootDirEntries); // Free the correct pointer
        return -1;
    }
    printf("FAT updated successfully.\n"); // Debug

    free(rootDirEntries); // Free the correct pointer
    printf("Exiting initializeRootDirectory\n"); // Debug
    return startBlock;
}

void exitFileSystem() {
    if (vcb != NULL) {
        free(vcb);
        vcb = NULL;
    }
    if (fat != NULL) {
        free(fat);
        fat = NULL;
    }
}
