// fs_functions.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mfs.h"
#include "fsLow.h"
#include "vcb.h"

extern struct FATEntry* fat; // Add this line
// Global variables
extern struct VolumeControlBlock* vcb; 
extern struct DirectoryEntry *rootDir;  
extern char currentWorkingDirectory[MAX_FILENAME_LENGTH]; 
extern struct DirectoryEntry *loadedCWD; 

// Function prototypes
int parsePath(char * path, struct DirectoryEntry ** retParent, int * index, char ** lastElementName);
int findInDirectory(struct DirectoryEntry* dir, char * name);
void freeDir(struct DirectoryEntry * dir);
struct DirectoryEntry* loadDir(struct DirectoryEntry* entry);
char *collapsePath(const char *path);
int allocateBlocks(int numBlocks, struct VolumeControlBlock vcb);
struct DirectoryEntry* createDirectory(int numEntries, struct DirectoryEntry *parent, struct VolumeControlBlock *vcb);

// ... (Your other functions, including createDirectory, parsePath, etc.) ...
int allocateBlocks(int numBlocks, struct VolumeControlBlock vcb) {
    // 1. Check if enough free blocks are available
    if (vcb.freeBlocks < numBlocks) {
        return -1; // Not enough free blocks
    }

    // 2. Find contiguous free blocks in the FAT
    int firstBlock = -1;
    int currentBlock = vcb.dataStart; 
    int contiguousCount = 0;

    while (currentBlock <= vcb.totalBlocks && contiguousCount < numBlocks) { 
        if (fat[currentBlock].nextBlock == FAT_FREE) { // Accessing the global 'fat'
            if (contiguousCount == 0) {
                firstBlock = currentBlock;
            }
            contiguousCount++;
        } else {
            // Reset if a used block is encountered
            contiguousCount = 0;
            firstBlock = -1;
        }
        currentBlock++;
    }

    if (firstBlock == -1) {
        return -1; // No contiguous blocks found
    }

    // 3. Update the FAT and VCB
    int prevBlock = -1; 
    for (int i = 0; i < numBlocks; i++) {
        if (prevBlock != -1) {
            fat[prevBlock].nextBlock = firstBlock + i; // Accessing the global 'fat'
        }
        prevBlock = firstBlock + i;

        // Mark the last block as end-of-file
        if (i == numBlocks - 1) {
            fat[firstBlock + i].nextBlock = FAT_EOF; // Accessing the global 'fat'
        } else {
            fat[firstBlock + i].nextBlock = firstBlock + i + 1; // Accessing the global 'fat'
        }
    }

    vcb.freeBlocks -= numBlocks;

    return firstBlock;
}

int parsePath(char * path, struct DirectoryEntry ** retParent, int * index, char ** lastElementName) {
    printf("Entering parsePath with path: %s\n", path);

    if (path == NULL) {
        printf("Exiting parsePath: path is NULL\n");
        return -1;
    }
    if (strlen(path) == 0) {
        printf("Exiting parsePath: path is empty\n");
        return -1;
    }

    struct DirectoryEntry *currentDir;
    if (path[0] == '/') {
        currentDir = rootDir;
        printf("Starting at root directory\n");
    } else {
        currentDir = loadedCWD;
        printf("Starting at current working directory\n");
    }

    struct DirectoryEntry *parent = NULL;
    char *token1, *token2, *saveptr;

    token1 = strtok_r(path, "/", &saveptr); // Corrected strtok_r usage
    if (token1 == NULL) {
        *retParent = currentDir;
        *lastElementName = NULL;
        *index = 0;
        printf("Exiting parsePath: Path is /\n");
        return 0;
    }

    token2 = strtok_r(NULL, "/", &saveptr); // Corrected strtok_r usage
    
    while (token2 != NULL) {
        int idx = findInDirectory(currentDir, token1);
        if (idx == -1) {
            freeDir(currentDir);
            return -1;
        }
        if (currentDir[idx].fileType != 1) {
            freeDir(currentDir);
            return -1;
        }
        parent = currentDir;
        currentDir = loadDir(&currentDir[idx]);
        freeDir(parent);
        token1 = token2;
        token2 = strtok_r(NULL, "/", &saveptr); // Corrected strtok_r usage
    }

    // Check if the last token exists
    int idx = findInDirectory(currentDir, token1);
    if (idx == -1) {
        *retParent = currentDir;  // Set retParent to the current directory
        *index = -1;              // Indicate that the last token was not found
        *lastElementName = token1;
        return -2;                // Last token not found
    } else {
        *retParent = currentDir;
        *index = idx;
        *lastElementName = token1;
        return 0;
    } 
}




int findInDirectory(struct DirectoryEntry* dir, char * name) {
    if ((dir == NULL) || (name == NULL)) {
        return -2;
    }

    // Calculate the actual number of entries in the directory
    int numEntries = dir[0].fileSize / sizeof(struct DirectoryEntry); 
    printf("numEntries: %d\n", numEntries);
    for (int i = 0; i < numEntries; i++) {
        if (dir[i].inUse) {
            if (strcmp(dir[i].filename, name) == 0 ) {
                return i; // Return the index of the entry
            }
        }
    }
    return -1;
}

void freeDir(struct DirectoryEntry * dir) {
    if(dir == NULL) {
        return;
    }
    if(dir == rootDir) {
        return;
    }
    // Assuming loadedCWD is a global pointer to the current working directory
    if(dir == loadedCWD) {
        return;
    }
    free(dir);
}


struct DirectoryEntry* loadDir(struct DirectoryEntry* entry) {
    printf("Entering loadDir with entry: %s\n", entry->filename); // Added print statement

    if (entry == NULL) {
        printf("Exiting loadDir: entry is NULL\n"); // Added print statement
        return NULL;
    }

    int blocksNeeded = (entry->fileSize + vcb->blockSize - 1) / vcb->blockSize;
    int bytesNeeded = blocksNeeded * vcb->blockSize;

    printf("Blocks needed: %d, Bytes needed: %d\n", blocksNeeded, bytesNeeded); // Added print statement

    struct DirectoryEntry *new = malloc(bytesNeeded);
    if (new == NULL) {
        fprintf(stderr, "Memory allocation failed in loadDir\n");
        exit(1);
    }

    printf("Loading directory from block %lu\n", entry->firstBlockIndex); // Added print statement
    LBAread(new, blocksNeeded, entry->firstBlockIndex);

    printf("Exiting loadDir: Success\n"); // Added print statement
    return new;
}

char *collapsePath(const char *path) {
    char *pathCopy = strdup(path); // Make a copy to work with
    char *token, *saveptr;
    char **pathTokens = malloc(sizeof(char *) * (strlen(path) / 2 + 2));
    int *tokenIndices = malloc(sizeof(int) * (strlen(path) / 2 + 2));
    int tokenCount = 0;
    int index = 0;

    // Tokenize the path
    token = strtok_r(pathCopy, "/", &saveptr);
    while (token != NULL) {
        pathTokens[tokenCount++] = token;
        token = strtok_r(NULL, "/", &saveptr);
    }

    // Process tokens to collapse /./ and /../
    for (int i = 0; i < tokenCount; i++) {
        if (strcmp(pathTokens[i], ".") == 0) {
            continue; // Ignore /./
        } else if (strcmp(pathTokens[i], "..") == 0) {
            if (index > 0) {
                index--; // Go back one level (unless at root)
            }
        } else {
            tokenIndices[index++] = i;
        }
    }

    // Reconstruct the collapsed path
    char *collapsedPath = malloc(strlen(path) + 1); // Allocate enough memory
    strcpy(collapsedPath, "/");
    for (int i = 0; i < index; i++) {
        strcat(collapsedPath, pathTokens[tokenIndices[i]]);
        strcat(collapsedPath, "/");
    }

    free(pathCopy);
    free(pathTokens);
    free(tokenIndices);
    return collapsedPath;
}

struct DirectoryEntry* createDirectory(int numEntries, struct DirectoryEntry *parent, struct VolumeControlBlock *vcb) {
    int bytesNeeded = numEntries * sizeof(struct DirectoryEntry);
    int blocksNeeded = (bytesNeeded + (vcb->blockSize - 1)) / vcb->blockSize;

    // Allocate blocks for the directory
    int dirLocation = allocateBlocks(blocksNeeded, *vcb); // Assuming you have an allocateBlocks function
    if (dirLocation == -1) {
        perror("Error allocating blocks for directory");
        return NULL; // Failed to allocate blocks
    }

    // Allocate memory for the directory entries
    struct DirectoryEntry *newDir = malloc(blocksNeeded * vcb->blockSize);
    if (newDir == NULL) {
        perror("Error allocating memory for directory");
        return NULL; // Failed to allocate memory
    }

    // Initialize directory entries to a known free state
    for (int i = 0; i < numEntries; i++) {
        memset(&newDir[i], 0, sizeof(struct DirectoryEntry)); // Example: Set all bytes to 0
    }

    // Set up "." entry
    strcpy(newDir[0].filename, ".");
    newDir[0].firstBlockIndex = dirLocation;
    newDir[0].fileType = 1; // Directory type
    newDir[0].fileSize = numEntries * sizeof(struct DirectoryEntry);
    newDir[0].creationTime = time(NULL);
    // ... set other metadata for "." ...

    // Set up ".." entry
    if (parent != NULL) {
        strcpy(newDir[1].filename, "..");
        newDir[1].firstBlockIndex = parent->firstBlockIndex;
        newDir[1].fileType = 1; // Directory type
        newDir[1].fileSize = parent->fileSize;
        newDir[1].creationTime = time(NULL);
        // ... set other metadata for ".." ...
    } else {
        // Root directory case: ".." is the same as "."
        strcpy(newDir[1].filename, "..");
        newDir[1].firstBlockIndex = dirLocation;
        newDir[1].fileType = 1; // Directory type
        newDir[1].fileSize = blocksNeeded * vcb->blockSize;
        newDir[1].creationTime = time(NULL);
        // ... set other metadata for ".." ...
    }

    // Write the directory to disk
    LBAwrite(newDir, blocksNeeded, dirLocation);

    return newDir; 
}


//================END PARSE PATH====================


// Directory Operations
int fs_mkdir(const char *pathname, mode_t mode) {
    printf("Entering fs_mkdir with pathname: %s\n", pathname); // Debug

    // 1. Parse the path to get the parent directory
    struct DirectoryEntry *parent = NULL;
    int index;
    char *lastElementName;
    int result = parsePath((char *)pathname, &parent, &index, &lastElementName);

    printf("parsePath result: %d\n", result); // Debug
    if (result != -2) {  
        printf("Exiting fs_mkdir: Path not found or invalid\n"); // Debug
        return -1; 
    }

    // 3. Create the new directory 
    printf("Creating new directory...\n"); // Debug

    // Print the parent directory information
    if (parent != NULL) { 
        printf("  Parent directory:\n");  // Debug
        printf("    filename: %s\n", parent->filename);
        printf("    fileSize: %lu\n", parent->fileSize);
        printf("    firstBlockIndex: %lu\n", parent->firstBlockIndex);
        printf("    fileType: %d\n", parent->fileType);
    } else {
        printf("  Parent directory is NULL\n"); // Debug
    }

    struct DirectoryEntry* newDir = createDirectory(51, parent, vcb); 
    if (newDir == NULL) {                                             
        freeDir(parent);
        printf("Exiting fs_mkdir: Failed to create directory\n"); 
        return -1; 
    }

    int newDirLocation = newDir->firstBlockIndex; 
    printf("New directory created at location: %d\n", newDirLocation); 

    // 4. Update the parent directory with the new entry
    if (parent != NULL) { 
        int freeEntryIndex = -1;
        int numEntries = parent[0].fileSize / sizeof(struct DirectoryEntry);
        for (int i = 0; i < numEntries; i++) {
            if (!parent[i].inUse) {
                freeEntryIndex = i;
                break;
            }
        }

        if (freeEntryIndex == -1) {
            free(newDir); // Free newDir if no free entry is found
            freeDir(parent);
            printf("Exiting fs_mkdir: No free entries in parent directory\n"); 
            return -1;
        }

        printf("Updating parent directory...\n"); 
        strcpy(parent[freeEntryIndex].filename, lastElementName);
        parent[freeEntryIndex].fileSize = 0; 
        parent[freeEntryIndex].firstBlockIndex = newDirLocation;
        parent[freeEntryIndex].creationTime = time(NULL);
        parent[freeEntryIndex].lastModifiedTime = parent[freeEntryIndex].creationTime;
        parent[freeEntryIndex].fileType = 1; 
        parent[freeEntryIndex].inUse = 1;

        int blocksNeeded = (parent[0].fileSize + vcb->blockSize - 1) / vcb->blockSize;
        if (LBAwrite(parent, blocksNeeded, parent[0].firstBlockIndex) != blocksNeeded) {
            free(newDir); // Free newDir if LBAwrite fails
            freeDir(parent);
            printf("Exiting fs_mkdir: Failed to write directory\n"); 
            return -1; 
        }
        printf("Success! Directory created!\n"); 

    } else {
        printf("Parent directory is NULL, not updating.\n"); // Debug
    }

    free(newDir); // Free newDir after updating the parent
    freeDir(parent);
    return 0;
}

int fs_rmdir(const char *pathname) {
    // TODO: Implement directory removal
    return 0;
}

fdDir * fs_opendir(const char *pathname) {
    // 1. Parse the path
    struct DirectoryEntry *parent;
    int index;
    char *lastElementName;
    int result = parsePath((char *)pathname, &parent, &index, &lastElementName);

    if (result == -1) {
        return NULL; // Path not found
    }

    // 2. Check if it's a directory
    if (parent[index].fileType != 1) {
        freeDir(parent);
        return NULL; // Not a directory
    }

    // 3. Allocate and initialize the fdDir structure
    fdDir *dirp = malloc(sizeof(fdDir));
    if (dirp == NULL) {
        freeDir(parent);
        return NULL; // Memory allocation error
    }

    // Initialize dirp with appropriate values
    dirp->d_reclen = sizeof(fdDir);
    dirp->dirEntryPosition = 0; 
    dirp->di = malloc(sizeof(struct fs_diriteminfo)); // Allocate memory for di
    if (dirp->di == NULL) {
        free(dirp);
        freeDir(parent);
        return NULL; // Memory allocation error
    }

    // Load the directory contents into memory (using parent)
    dirp->directory = loadDir(&parent[index]); 
    if (dirp->directory  == NULL) {
        free(dirp->di);
        free(dirp);
        freeDir(parent);
        return NULL; // Failed to load directory contents
    }

    freeDir(parent);
    return dirp;
}

struct fs_diriteminfo *fs_readdir(fdDir *dirp) {
    if (dirp == NULL || dirp->di == NULL) {
        return NULL; // Invalid input
    }

    // Access directory contents directly from dirp
    struct DirectoryEntry *dirContents = dirp->directory; 
    if (dirContents == NULL) {
        return NULL; // Directory not open
    }

    int numEntries = dirContents[0].fileSize / sizeof(struct DirectoryEntry);

    // Find the next valid entry
    while (dirp->dirEntryPosition < numEntries && !dirContents[dirp->dirEntryPosition].inUse) {
        dirp->dirEntryPosition++;
    }

    if (dirp->dirEntryPosition >= numEntries) {
        return NULL; // End of directory
    }

    // Populate dirp->di with the directory entry information
    dirp->di->d_reclen = sizeof(struct fs_diriteminfo);
    dirp->di->fileType = dirContents[dirp->dirEntryPosition].fileType;
    strcpy(dirp->di->d_name, dirContents[dirp->dirEntryPosition].filename);

    dirp->dirEntryPosition++;
    return dirp->di;
}

int fs_closedir(fdDir *dirp) {
    if (dirp == NULL) {
        return 0; // Nothing to do
    }

    // Free any memory allocated for dirp->di and dirp->directory
    if (dirp->di != NULL) {
        free(dirp->di);
    }
    if (dirp->directory != NULL) {
        free(dirp->directory);
    }

    free(dirp);
    return 0;
}

// Path Operations
char * fs_getcwd(char *pathname, size_t size) {
    if (pathname == NULL || size == 0) {
        return NULL;
    }
    strncpy(pathname, currentWorkingDirectory, size);
    pathname[size - 1] = '\0'; // Ensure null-termination
    return pathname;
}

int fs_setcwd(char *pathname) {
    if (pathname == NULL || strlen(pathname) == 0) {
        return -1;
    }

    // 1. Parse the path
    struct DirectoryEntry *parent;
    int index;
    char *lastElementName;
    int result = parsePath(pathname, &parent, &index, &lastElementName);

    if (result == -1 || index == -1) {
        freeDir(parent); // Free parent if parsePath fails
        return -1; // Path not found or invalid
    }

    // 2. Check if the last element is a directory
    if (parent[index].fileType != 1) {
        freeDir(parent);
        return -1; // Not a directory
    }

    // 3. Load the new directory
    struct DirectoryEntry *temp = loadDir(&parent[index]);
    if (loadedCWD != rootDir) {
        freeDir(loadedCWD);
    }
    loadedCWD = temp;
    freeDir(parent); // Free parent after loading the new directory

    // 4. Update the CWD string
    if (pathname[0] == '/') {
        // Absolute path
        strcpy(currentWorkingDirectory, pathname);
    } else {
        // Relative path
        char *tempPath = malloc(strlen(currentWorkingDirectory) + strlen(pathname) + 1);
        strcpy(tempPath, currentWorkingDirectory);
        strcat(tempPath, pathname);
        strcpy(currentWorkingDirectory, tempPath);
        free(tempPath);
    }

    // 5. Add trailing slash if needed
    if (currentWorkingDirectory[strlen(currentWorkingDirectory) - 1] != '/') {
        strcat(currentWorkingDirectory, "/");
    }

    // 6. Collapse the path (handle /./ and /../)
    char *collapsedPath = collapsePath(currentWorkingDirectory);
    strcpy(currentWorkingDirectory, collapsedPath);
    free(collapsedPath);

    return 0;
}
// File Operations
int fs_isFile(char * filename) {
    // 1. Parse the path
    struct DirectoryEntry *parent;
    int index;
    char *lastElementName;
    int result = parsePath(filename, &parent, &index, &lastElementName);

    if (result == -1) {
        return 0; // Path not found
    }

    // 2. Check if it's a file
    int isFile = (parent[index].fileType == 0);
    freeDir(parent);
    return isFile;
}

int fs_isDir(char * pathname) {
    // 1. Parse the path
    struct DirectoryEntry *parent;
    int index;
    char *lastElementName;
    int result = parsePath(pathname, &parent, &index, &lastElementName);

    if (result == -1) {
        return 0; // Path not found
    }

    // 2. Check if it's a directory
    int isDir = (parent[index].fileType == 1);
    freeDir(parent);
    return isDir;
}

int fs_delete(char* filename) {
    // TODO: Implement file deletion
    return 0;
}

// File Stats
int fs_stat(const char *path, struct fs_stat *buf) {
    // TODO: Implement file stats
    return 0;
}