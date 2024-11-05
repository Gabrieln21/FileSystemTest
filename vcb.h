#ifndef VCB_H
#define VCB_H
#define FAT_EOF 0xFFFFFFFFFFFFFFFF  // End of file marker in FAT
#define FAT_FREE 0x0000000000000000 // Free block marker in FAT

#include <time.h>
#include <sys/types.h>
#include <stdint.h>
#include "mfs.h"     // Include mfs.h for FATEntry definition

#define FS_SIGNATURE 0xCAFEBABE  // Unique signature for our file system
#define MAX_FILENAME_LENGTH 255
#define BLOCK_SIZE 4096          // 4KB blocks

struct VolumeControlBlock {
    /* Volume Identification */
    char volumeName[MAX_FILENAME_LENGTH];  // Name of the volume
    uint64_t signature;                   // File system signature

    /* FAT */
    uint64_t fatStart;              // Starting block of FAT
    uint64_t fatBlocks;            // Number of blocks used by FAT
    uint64_t dataStart;            // Starting block of data area
    uint64_t fatEntryCount;        // Number of entries in the FAT
    uint64_t firstDataBlock;       // First block available for data

    /* Block Management */
    uint64_t blockSize;            // Size of each block in bytes
    uint64_t totalBlocks;          // Total number of blocks
    uint64_t freeBlocks;           // Number of free blocks

    /* Directory Management */
    uint64_t rootDirectory;        // Root directory location
    uint64_t maxFilenameLength;    // Maximum filename length
    uint64_t maxFileSize;          // Maximum file size supported

    /* Usage Statistics */
    time_t creationTime;           // Volume creation timestamp
    time_t lastMountedTime;        // Last mount timestamp
    time_t lastWriteTime;          // Last write operation
    uint32_t mountCount;           // Number of mounts

    /* Metadata */
    uint64_t metadataLocation;     // Location of additional metadata
    uint32_t fsVersion;            // File system version number
    unsigned char reserved[64];     // Reserved for future use
};
#endif
