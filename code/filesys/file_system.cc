/// Routines to manage the overall operation of the file system.  Implements
/// routines to map from textual file names to files.
///
/// Each file in the file system has:
/// * a file header, stored in a sector on disk (the size of the file header
///   data structure is arranged to be precisely the size of 1 disk sector);
/// * a number of data blocks;
/// * an entry in the file system directory.
///
/// The file system consists of several data structures:
/// * A bitmap of free disk sectors (cf. `bitmap.h`).
/// * A directory of file names and file headers.
///
/// Both the bitmap and the directory are represented as normal files.  Their
/// file headers are located in specific sectors (sector 0 and sector 1), so
/// that the file system can find them on bootup.
///
/// The file system assumes that the bitmap and directory files are kept
/// “open” continuously while Nachos is running.
///
/// For those operations (such as `Create`, `Remove`) that modify the
/// directory and/or bitmap, if the operation succeeds, the changes are
/// written immediately back to disk (the two files are kept open during all
/// this time).  If the operation fails, and we have modified part of the
/// directory and/or bitmap, we simply discard the changed version, without
/// writing it back to disk.
///
/// Our implementation at this point has the following restrictions:
///
/// * there is no synchronization for concurrent accesses;
/// * files have a fixed size, set when the file is created;
/// * files cannot be bigger than about 3KB in size;
/// * there is no hierarchical directory structure, and only a limited number
///   of files can be added to the system;
/// * there is no attempt to make the system robust to failures (if Nachos
///   exits in the middle of an operation that modifies the file system, it
///   may corrupt the disk).
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2021 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "file_system.hh"
#include "directory.hh"
#include "file_header.hh"
#include "lib/bitmap.hh"
#include "threads/lock.hh"

#include <stdio.h>
#include <string.h>


/// Sectors containing the file headers for the bitmap of free sectors, and
/// the directory of files.  These file headers are placed in well-known
/// sectors, so that they can be located on boot-up.
static const unsigned FREE_MAP_SECTOR = 0;
static const unsigned DIRECTORY_SECTOR = 1;

/// Initialize the file system.  If `format == true`, the disk has nothing on
/// it, and we need to initialize the disk to contain an empty directory, and
/// a bitmap of free sectors (with almost but not all of the sectors marked
/// as free).
///
/// If `format == false`, we just have to open the files representing the
/// bitmap and the directory.
///
/// * `format` -- should we initialize the disk?
FileSystem::FileSystem(bool format)
{
    DEBUG('f', "Initializing the file system.\n");

    freeMapLock   = new Lock("free map lock");
    directoryLock = new Lock("directory lock");
    openFilesLock = new Lock("open files lock");
    openFilesHead = nullptr;

    if (format) {
        Bitmap     *freeMap = new Bitmap(NUM_SECTORS);
        Directory  *dir     = new Directory(NUM_DIR_ENTRIES);
        FileHeader *mapH    = new FileHeader;
        FileHeader *dirH    = new FileHeader;

        DEBUG('f', "Formatting the file system.\n");

        // First, allocate space for FileHeaders for the directory and bitmap
        // (make sure no one else grabs these!)
        freeMap->Mark(FREE_MAP_SECTOR);
        freeMap->Mark(DIRECTORY_SECTOR);

        // Second, allocate space for the data blocks containing the contents
        // of the directory and bitmap files.  There better be enough space!

        ASSERT(mapH->Allocate(freeMap, FREE_MAP_FILE_SIZE));
        ASSERT(dirH->Allocate(freeMap, DIRECTORY_FILE_SIZE));

        // Flush the bitmap and directory `FileHeader`s back to disk.
        // We need to do this before we can `Open` the file, since open reads
        // the file header off of disk (and currently the disk has garbage on
        // it!).

        DEBUG('f', "Writing headers back to disk.\n");
        mapH->WriteBack(FREE_MAP_SECTOR);
        dirH->WriteBack(DIRECTORY_SECTOR);

        // OK to open the bitmap and directory files now.
        // The file system operations assume these two files are left open
        // while Nachos is running.

        freeMapFile   = new OpenFile(FREE_MAP_SECTOR);
        directoryFile = new OpenFile(DIRECTORY_SECTOR);

        // Once we have the files “open”, we can write the initial version of
        // each file back to disk.  The directory at this point is completely
        // empty; but the bitmap has been changed to reflect the fact that
        // sectors on the disk have been allocated for the file headers and
        // to hold the file data for the directory and bitmap.

        DEBUG('f', "Writing bitmap and directory back to disk.\n");
        freeMap->WriteBack(freeMapFile);     // flush changes to disk
        dir->WriteBack(directoryFile);

        if (debug.IsEnabled('f')) {
            freeMap->Print();
            dir->Print();

            delete freeMap;
            delete dir;
            delete mapH;
            delete dirH;
        }
    } else {
        // If we are not formatting the disk, just open the files
        // representing the bitmap and directory; these are left open while
        // Nachos is running.
        freeMapFile   = new OpenFile(FREE_MAP_SECTOR);
        directoryFile = new OpenFile(DIRECTORY_SECTOR);
    }
}

FileSystem::~FileSystem()
{
    delete freeMapFile;
    delete directoryFile;

    // Elimino todos los archivos al cerrar Nachos:
    while (openFilesHead != nullptr)
    {
        OpenFileEntry *e = openFilesHead;
        openFilesHead = e->next;
        delete e->accessLock;
        delete e;
    }

    delete freeMapLock;
    delete directoryLock;
    delete openFilesLock;    
}

/// Create a file in the Nachos file system (similar to UNIX `create`).
/// Since we cannot increase the size of files dynamically, we have to give
/// `Create` the initial size of the file.
///
/// The steps to create a file are:
/// 1. Make sure the file does not already exist.
/// 2. Allocate a sector for the file header.
/// 3. Allocate space on disk for the data blocks for the file.
/// 4. Add the name to the directory.
/// 5. Store the new file header on disk.
/// 6. Flush the changes to the bitmap and the directory back to disk.
///
/// Return true if everything goes ok, otherwise, return false.
///
/// Create fails if:
/// * file is already in directory;
/// * no free space for file header;
/// * no free entry for file in directory;
/// * no free space for data blocks for the file.
///
/// Note that this implementation assumes there is no concurrent access to
/// the file system!
///
/// * `name` is the name of file to be created.
/// * `initialSize` is the size of file to be created.
bool
FileSystem::Create(const char *name, unsigned initialSize)
{
    ASSERT(name != nullptr);
    ASSERT(initialSize < MAX_FILE_SIZE);

    DEBUG('f', "Creating file %s, size %u\n", name, initialSize);

    // Tomamos el lock:
    directoryLock->Acquire();

    Directory *dir = new Directory(NUM_DIR_ENTRIES);
    dir->FetchFrom(directoryFile);

    bool success;

    if (dir->Find(name) != -1) {
        success = false;  // File is already in directory.
    } else {
        // Tomamos el lock del bitmap:
        freeMapLock->Acquire();

        Bitmap *freeMap = new Bitmap(NUM_SECTORS);
        freeMap->FetchFrom(freeMapFile);
        int sector = freeMap->Find();
          // Find a sector to hold the file header.
        if (sector == -1) {
            success = false;  // No free block for file header.
        } else if (!dir->Add(name, sector, directoryFile)) {
            success = false;  // No space in directory.
        } else {
            FileHeader *h = new FileHeader;
            success = h->Allocate(freeMap, initialSize);
              // Fails if no space on disk for data.
            if (success) {
                // Everything worked, flush all changes back to disk.
                h->WriteBack(sector);
                dir->WriteBack(directoryFile);
                freeMap->WriteBack(freeMapFile);
            }
            delete h;
        }
        delete freeMap;
        
        // Liberamos el lock del bitmap:
        freeMapLock->Release();

    }
    delete dir;

    //Liberamos el lock del directorio:
    directoryLock->Release();

    return success;
}

/// Open a file for reading and writing.
///
/// To open a file:
/// 1. Find the location of the file's header, using the directory.
/// 2. Bring the header into memory.
///
/// * `name` is the text name of the file to be opened.
OpenFile *
FileSystem::Open(const char *name)
{
    ASSERT(name != nullptr);

    DEBUG('f', "Opening file %s\n", name);

    // Tomamos el lock del directorio:
    directoryLock->Acquire();

    Directory *dir = new Directory(NUM_DIR_ENTRIES);
    dir->FetchFrom(directoryFile);
    int sector = dir->Find(name);
    delete dir;

    // Soltamos el lock:
    directoryLock->Release();

    if (sector < 0) {
        return nullptr;  // Not found.
    }
    return new OpenFile(sector);
}

/// Delete a file from the file system.
///
/// This requires:
/// 1. Remove it from the directory.
/// 2. Delete the space for its header.
/// 3. Delete the space for its data blocks.
/// 4. Write changes to directory, bitmap back to disk.
///
/// Return true if the file was deleted, false if the file was not in the
/// file system.
///
/// * `name` is the text name of the file to be removed.
bool
FileSystem::Remove(const char *name)
{
    ASSERT(name != nullptr);
    
    // Paso 1: eliminar el directorio
    // Tomamos el lock:
    directoryLock->Acquire();
    
    Directory *dir = new Directory(NUM_DIR_ENTRIES);
    dir->FetchFrom(directoryFile);
    int sector = dir->Find(name);
    if (sector == -1) {
       delete dir;
       // Liberamos el lock:
       directoryLock->Release();
       return false;  // file not found
    }
    
    dir->Remove(name);
    dir->WriteBack(directoryFile);
    delete dir;

    // Liberamos el lock:
    directoryLock->Release(); 

    // Paso 2: Si no quedan hilos que lo tengan abierto eliminamos el header:
    
    // Tomamos el lock:
    openFilesLock->Acquire();
    
    OpenFileEntry *e = FindOpenEntry((unsigned) sector);
    
    if (e != nullptr) {
        e->pendingRemove = true;
        openFilesLock->Release();
        return true;
    }

    // Liberamos el lock:
    openFilesLock->Release();

    DeallocateSector((unsigned) sector);
    return true;
}

// Liberamos en disco el header y los datos del archivo en el sector. 
void 
FileSystem::DeallocateSector(unsigned sector)
{
    // Tomamos el lock:
    freeMapLock->Acquire();

    FileHeader *fileH = new FileHeader;
    fileH->FetchFrom(sector);

    Bitmap *freeMap = new Bitmap(NUM_SECTORS);
    freeMap->FetchFrom(freeMapFile);

    fileH->Deallocate(freeMap);  // Remove data blocks.
    freeMap->Clear(sector);      // Remove header block.
    freeMap->WriteBack(freeMapFile);  // Flush to disk.
    
    delete fileH;
    delete freeMap;

    // Liberamos el lock:
    freeMapLock->Release();
}

// Busca una entrada de la tabla de archivos abiertos para el sector dado.
OpenFileEntry *
FileSystem::FindOpenEntry(unsigned sector)
{
    for (OpenFileEntry *e = openFilesHead; e != nullptr; e = e->next) {
        if (e->sector == sector) {
            return e;
        }
    }
    return nullptr;
}

/// Registra una apertura más del 'sector' y devuelve el lock de acceso a datos compartido por todos los
/// OpenFile de ese sector.
Lock *
FileSystem::AcquireOpen(unsigned sector)
{
    openFilesLock->Acquire();

    OpenFileEntry *e = FindOpenEntry(sector);
    if (e == nullptr) {
        e = new OpenFileEntry;
        e->sector = sector;
        e->refCount = 0;
        e->pendingRemove = false;
        e->accessLock = new Lock("file access lock");
        e->next = openFilesHead;
        openFilesHead = e;
    }
    e->refCount++;
    Lock *accessLock = e->accessLock;

    openFilesLock->Release();
    return accessLock;
}

/// Se llama desde el destructor de OpenFile. Si esta era la última
/// referencia y el archivo estaba pendiente de borrado, libera sus
/// sectores recién ahora.
void
FileSystem::ReleaseOpen(unsigned sector)
{
    openFilesLock->Acquire();

    OpenFileEntry *prev = nullptr;
    OpenFileEntry *e = openFilesHead;
    while (e != nullptr && e->sector != sector) {
        prev = e;
        e = e->next;
    }
    ASSERT(e != nullptr);

    e->refCount--;
    bool gone = e->refCount == 0;
    bool shouldDeallocate = gone && e->pendingRemove;
    if (gone) {
        if (prev == nullptr) {
            openFilesHead = e->next;
        } else {
            prev->next = e->next;
        }
    }

    openFilesLock->Release();

    if (gone) {
        if (shouldDeallocate) {
            DeallocateSector(sector);
        }
        delete e->accessLock;
        delete e;
    }
}

/// Extiende el archivo de 'sector' para que entre 'newSize' bytes y persiste header + bitmap.
bool
FileSystem::ExtendFile(FileHeader *hdr, unsigned sector, unsigned newSize)
{
    freeMapLock->Acquire();

    Bitmap *freeMap = new Bitmap(NUM_SECTORS);
    freeMap->FetchFrom(freeMapFile);

    bool ok = hdr->Extend(freeMap, newSize);
    if (ok) {
        hdr->WriteBack(sector);
        freeMap->WriteBack(freeMapFile);
    }

    delete freeMap;
    freeMapLock->Release();
    return ok;
}

/// List all the files in the file system directory.
void
FileSystem::List()
{
    Directory *dir = new Directory(NUM_DIR_ENTRIES);

    dir->FetchFrom(directoryFile);
    dir->List();
    delete dir;
}


static bool
AddToShadowBitmap(unsigned sector, Bitmap *map)
{
    ASSERT(map != nullptr);

    if (map->Test(sector)) {
        DEBUG('f', "Sector %u was already marked.\n", sector);
        return false;
    }
    map->Mark(sector);
    DEBUG('f', "Marked sector %u.\n", sector);
    return true;
}

static bool
CheckForError(bool value, const char *message)
{
    if (!value) {
        DEBUG('f', "Error: %s\n", message);
    }
    return !value;
}

static bool
CheckSector(unsigned sector, Bitmap *shadowMap)
{
    if (CheckForError(sector < NUM_SECTORS,
                      "sector number too big.  Skipping bitmap check.")) {
        return true;
    }
    return CheckForError(AddToShadowBitmap(sector, shadowMap),
                         "sector number already used.");
}

static bool
CheckFileHeader(FileHeader *h, unsigned num, Bitmap *shadowMap)
{
    ASSERT(h != nullptr);

    const RawFileHeader *rh = h->GetRaw();
    bool error = false;

    DEBUG('f', "Checking file header %u.  File size: %u bytes, number of sectors: %u.\n",
          num, rh->numBytes, rh->numSectors);
    error |= CheckForError(rh->numSectors >= DivRoundUp(rh->numBytes,
                                                        SECTOR_SIZE),
                           "sector count not compatible with file size.");
    error |= CheckForError(rh->numBytes <= MAX_FILE_SIZE,
                           "file too big.");
    for (unsigned i = 0; i < rh->numSectors; i++) {
        unsigned s = h->ByteToSector(i * SECTOR_SIZE);
        error |= CheckSector(s, shadowMap);
    }
    return error;
}

static bool
CheckBitmaps(const Bitmap *freeMap, const Bitmap *shadowMap)
{
    bool error = false;
    for (unsigned i = 0; i < NUM_SECTORS; i++) {
        DEBUG('f', "Checking sector %u. Original: %u, shadow: %u.\n",
              i, freeMap->Test(i), shadowMap->Test(i));
        error |= CheckForError(freeMap->Test(i) == shadowMap->Test(i),
                               "inconsistent bitmap.");
    }
    return error;
}

static bool
CheckDirectory(const RawDirectory *rd, Bitmap *shadowMap)
{
    ASSERT(rd != nullptr);
    ASSERT(shadowMap != nullptr);

    bool error = false;
    unsigned nameCount = 0;
    const char **knownNames = new const char *[rd->tableSize];

    for (unsigned i = 0; i < rd->tableSize; i++) {
        DEBUG('f', "Checking direntry: %u.\n", i);
        const DirectoryEntry *e = &rd->table[i];

        if (e->inUse) {
            if (strlen(e->name) > FILE_NAME_MAX_LEN) {
                DEBUG('f', "Filename too long.\n");
                error = true;
            }

            // Check for repeated filenames.
            DEBUG('f', "Checking for repeated names.  Name count: %u.\n",
                  nameCount);
            bool repeated = false;
            for (unsigned j = 0; j < nameCount; j++) {
                DEBUG('f', "Comparing \"%s\" and \"%s\".\n",
                      knownNames[j], e->name);
                if (strcmp(knownNames[j], e->name) == 0) {
                    DEBUG('f', "Repeated filename.\n");
                    repeated = true;
                    error = true;
                }
            }
            if (!repeated) {
                knownNames[nameCount] = e->name;
                DEBUG('f', "Added \"%s\" at %u.\n", e->name, nameCount);
                nameCount++;
            }

            // Check sector.
            error |= CheckSector(e->sector, shadowMap);

            // Check file header.
            FileHeader *h = new FileHeader;
            h->FetchFrom(e->sector);
            error |= CheckFileHeader(h, e->sector, shadowMap);
            delete h;
        }
    }
    delete [] knownNames;
    return error;
}

bool
FileSystem::Check()
{
    DEBUG('f', "Performing filesystem check\n");
    bool error = false;

    Bitmap *shadowMap = new Bitmap(NUM_SECTORS);
    shadowMap->Mark(FREE_MAP_SECTOR);
    shadowMap->Mark(DIRECTORY_SECTOR);

    DEBUG('f', "Checking bitmap's file header.\n");

    FileHeader *bitH = new FileHeader;
    bitH->FetchFrom(FREE_MAP_SECTOR);
    DEBUG('f', "  File size: %u bytes, expected %u bytes.\n"
               "  Number of sectors: %u, expected %u.\n",
          bitH->GetRaw()->numBytes, FREE_MAP_FILE_SIZE,
          bitH->GetRaw()->numSectors, FREE_MAP_FILE_SIZE / SECTOR_SIZE);
    error |= CheckForError(bitH->GetRaw()->numBytes == FREE_MAP_FILE_SIZE,
                           "bad bitmap header: wrong file size.");
    error |= CheckForError(bitH->GetRaw()->numSectors == FREE_MAP_FILE_SIZE / SECTOR_SIZE,
                           "bad bitmap header: wrong number of sectors.");
    error |= CheckFileHeader(bitH, FREE_MAP_SECTOR, shadowMap);
    delete bitH;

    DEBUG('f', "Checking directory.\n");

    FileHeader *dirH = new FileHeader;
    dirH->FetchFrom(DIRECTORY_SECTOR);
    error |= CheckFileHeader(dirH, DIRECTORY_SECTOR, shadowMap);
    delete dirH;

    Bitmap *freeMap = new Bitmap(NUM_SECTORS);
    freeMap->FetchFrom(freeMapFile);
    Directory *dir = new Directory(NUM_DIR_ENTRIES);
    const RawDirectory *rdir = dir->GetRaw();
    dir->FetchFrom(directoryFile);
    error |= CheckDirectory(rdir, shadowMap);
    delete dir;

    // The two bitmaps should match.
    DEBUG('f', "Checking bitmap consistency.\n");
    error |= CheckBitmaps(freeMap, shadowMap);
    delete shadowMap;
    delete freeMap;

    DEBUG('f', error ? "Filesystem check failed.\n"
                     : "Filesystem check succeeded.\n");

    return !error;
}

/// Print everything about the file system:
/// * the contents of the bitmap;
/// * the contents of the directory;
/// * for each file in the directory:
///   * the contents of the file header;
///   * the data in the file.
void
FileSystem::Print()
{
    FileHeader *bitH    = new FileHeader;
    FileHeader *dirH    = new FileHeader;
    Bitmap     *freeMap = new Bitmap(NUM_SECTORS);
    Directory  *dir     = new Directory(NUM_DIR_ENTRIES);

    printf("--------------------------------\n");
    bitH->FetchFrom(FREE_MAP_SECTOR);
    bitH->Print("Bitmap");

    printf("--------------------------------\n");
    dirH->FetchFrom(DIRECTORY_SECTOR);
    dirH->Print("Directory");

    printf("--------------------------------\n");
    freeMap->FetchFrom(freeMapFile);
    freeMap->Print();

    printf("--------------------------------\n");
    dir->FetchFrom(directoryFile);
    dir->Print();
    printf("--------------------------------\n");

    delete bitH;
    delete dirH;
    delete freeMap;
    delete dir;
}
