/// Routines for managing the disk file header (in UNIX, this would be called
/// the i-node).
///
/// The file header is used to locate where on disk the file's data is
/// stored.  We implement this as a fixed size table of pointers -- each
/// entry in the table points to the disk sector containing that portion of
/// the file data (in other words, there are no indirect or doubly indirect
/// blocks). The table size is chosen so that the file header will be just
/// big enough to fit in one disk sector,
///
/// Unlike in a real system, we do not keep track of file permissions,
/// ownership, last modification date, etc., in the file header.
///
/// A file header can be initialized in two ways:
///
/// * for a new file, by modifying the in-memory data structure to point to
///   the newly allocated data blocks;
/// * for a file already on disk, by reading the file header from disk.
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2021 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "file_header.hh"
#include "threads/system.hh"

#include <ctype.h>
#include <stdio.h>

#include <string.h>

/// Cuántos sectores físicos (datos + índices) hacen falta en total para que
/// un archivo tenga `numDataSectors` sectores de datos, arrancando de cero.
/// Es monótona en `numDataSectors`, así que también sirve para calcular el
/// costo INCREMENTAL de un `Extend`: SectorsNeededFor(nuevo) - SectorsNeededFor(viejo).
static unsigned
SectorsNeededFor(unsigned numDataSectors)
{
    if (numDataSectors <= NUM_DIRECT) {
        return numDataSectors;
    }
    unsigned rest = numDataSectors - NUM_DIRECT;
    unsigned total = numDataSectors + 1;  // datos + 1 sector de ind. simple
    if (rest <= POINTERS_PER_SECTOR) {
        return total;
    }
    rest -= POINTERS_PER_SECTOR;
    unsigned indexBlocks = DivRoundUp(rest, POINTERS_PER_SECTOR);
    total += 1 + indexBlocks;  // + sector de ind. doble + sus índices hoja
    return total;
}

/// Initialize a fresh file header for a newly created file.  Allocate data
/// blocks for the file out of the map of free disk blocks.  Return false if
/// there are not enough free blocks to accomodate the new file.
///
/// * `freeMap` is the bit map of free disk sectors.
/// * `fileSize` is the bit map of free disk sectors.
bool
FileHeader::Allocate(Bitmap *freeMap, unsigned fileSize)
{
    ASSERT(freeMap != nullptr);

    if (fileSize > MAX_FILE_SIZE) {
        return false;
    }

    raw.numBytes = 0;
    raw.numSectors = 0;
    raw.singleIndirect = 0;
    raw.doubleIndirect = 0;

    return Extend(freeMap, fileSize);
}

/// Extiende un archivo ya existente para que quepan
/// `newSize` bytes. 
/// Si `newSize <= FileLength()` no hace nada.
/// PRECONDICIÓN: el llamador tiene tomado el lock del freeMap.
/// Devuelve false si no hay espacio en el disco.
bool
FileHeader::Extend(Bitmap *freeMap, unsigned newSize)
{
    ASSERT(freeMap != nullptr);

    if (newSize <= raw.numBytes) {
        return true;  // Nothing to extend.
    }

    if (newSize > MAX_FILE_SIZE) {
        return false;
    }

    unsigned oldNumSectors = raw.numSectors;
    unsigned newNumSectors = DivRoundUp(newSize, SECTOR_SIZE);

    if (newNumSectors == oldNumSectors) {
        raw.numBytes = newSize;
        return true;
    } 

    unsigned needed = SectorsNeededFor(newNumSectors) - SectorsNeededFor(oldNumSectors);
    
    if (freeMap->CountClear() < needed){
        return false; // No entra en el disco
    }

    unsigned index = oldNumSectors; 
    unsigned toAssign = newNumSectors - oldNumSectors;

    // 1. Completamos los punteros directos:
    while (index < NUM_DIRECT && toAssign > 0) {
        raw.dataSectors[index] = freeMap->Find();
        index++;
        toAssign--;
    }

    if (toAssign == 0){
        raw.numSectors = newNumSectors;
        raw.numBytes = newSize;
        return true;
    }
   
    // 2. Completamos los punteros de indirección simple:
    unsigned singleBuf[POINTERS_PER_SECTOR];
    
    // Si es el primer sector de indirección, lo asignamos y lo inicializamos a 0.
    if (raw.singleIndirect == 0) {
        raw.singleIndirect = freeMap->Find();
        memset(singleBuf, 0, sizeof(singleBuf)); // Inicializamos a 0
    }
    // Si no, lo leemos del disco.
    else {
        synchDisk->ReadSector(raw.singleIndirect, (char *) singleBuf);
    }

    unsigned singleLocal = index - NUM_DIRECT;  // La cantidad de bloques de datos ya cubiertos por los punteros directos.
    
    // Mientras nos queden bloques por asignar y tengamos espacio en el sector de indirección simple.
    while (singleLocal < POINTERS_PER_SECTOR && toAssign > 0) {
        singleBuf[singleLocal] = freeMap->Find();
        index++;
        singleLocal++;
        toAssign--;
    }

    // Escribimos el sector de indirección simple modificado en el disco
    synchDisk->WriteSector(raw.singleIndirect, (char *) singleBuf);

    if (toAssign == 0){
        raw.numSectors = newNumSectors;
        raw.numBytes = newSize;
        return true;
    }


    // Si todavía nos quedan bloques por asignar
    // 3. Completamos los punteros de indirección doble:

    unsigned doubleBuf[POINTERS_PER_SECTOR];
    // Si es el primer sector de indirección, lo asignamos y lo inicializamos a 0.
    if (raw.doubleIndirect == 0) {
        raw.doubleIndirect = freeMap->Find();
        memset(doubleBuf, 0, sizeof(doubleBuf)); // Inicializamos a 0
    }
    // Si no, lo leemos del disco.
    else {
        synchDisk->ReadSector(raw.doubleIndirect, (char *) doubleBuf);
    }
    
    unsigned rel = index - NUM_DIRECT - POINTERS_PER_SECTOR;
    unsigned block = rel / POINTERS_PER_SECTOR;
    unsigned within = rel % POINTERS_PER_SECTOR;

    while (toAssign > 0) {
        ASSERT(block < POINTERS_PER_SECTOR);

        unsigned dataBuf[POINTERS_PER_SECTOR];
        if (doubleBuf[block] == 0) {
            doubleBuf[block] = freeMap->Find();
            memset(dataBuf, 0, sizeof(dataBuf)); // Inicializamos a 0
        }
        else {
            synchDisk->ReadSector(doubleBuf[block], (char *) dataBuf);
        }

        while (within < POINTERS_PER_SECTOR && toAssign > 0) {
            dataBuf[within] = freeMap->Find();
            index++;
            within++;
            toAssign--;
        }
        synchDisk->WriteSector(doubleBuf[block], (char *) dataBuf);
        block++;
        within = 0;
    }

    synchDisk->WriteSector(raw.doubleIndirect, (char *) doubleBuf);

    raw.numSectors = newNumSectors;
    raw.numBytes = newSize;
    return true;
}

/// De-allocate all the space allocated for data blocks for this file.
/// 
// PRECONDICIÓN: el llamador tiene tomado el lock del freeMap.
/// * `freeMap` is the bit map of free disk sectors.
void
FileHeader::Deallocate(Bitmap *freeMap)
{
    ASSERT(freeMap != nullptr);

    unsigned remaining = raw.numSectors;

    // 1. Desalojamos punteros directos
    unsigned direct = remaining < NUM_DIRECT ? remaining : NUM_DIRECT;
    
    for (unsigned i = 0; i < direct; i++){
        ASSERT(freeMap->Test(raw.dataSectors[i]));
        freeMap->Clear(raw.dataSectors[i]);
    }
    remaining -= direct; 
    if (remaining == 0) return;

    // 2. Desalojamos punteros de indirección simple
    unsigned singleBuf[POINTERS_PER_SECTOR];
    synchDisk->ReadSector(raw.singleIndirect, (char *) singleBuf);
    
    unsigned singleCount = remaining < POINTERS_PER_SECTOR ? remaining : POINTERS_PER_SECTOR;
    for (unsigned i = 0; i < singleCount; i++){
        freeMap->Clear(singleBuf[i]);
    }
    freeMap->Clear(raw.singleIndirect);
    remaining -= singleCount;
    if (remaining == 0) return;

    // 3. Desalojamos punteros de indirección doble
    unsigned doubleBuf[POINTERS_PER_SECTOR];
    synchDisk->ReadSector(raw.doubleIndirect, (char *) doubleBuf);
    
    unsigned block = 0; 
    while (remaining > 0){
        unsigned blockCount = remaining < POINTERS_PER_SECTOR ? remaining : POINTERS_PER_SECTOR;
        unsigned dataBuf[POINTERS_PER_SECTOR];
        synchDisk->ReadSector(doubleBuf[block], (char *) dataBuf);
        
        for (unsigned i = 0; i < blockCount; i++){
            freeMap->Clear(dataBuf[i]);
        }
        freeMap->Clear(doubleBuf[block]);
        remaining -= blockCount;
        block++;
    }
    
    freeMap->Clear(raw.doubleIndirect);
}

/// Fetch contents of file header from disk.
///
/// * `sector` is the disk sector containing the file header.
void
FileHeader::FetchFrom(unsigned sector)
{
    synchDisk->ReadSector(sector, (char *) &raw);
}

/// Write the modified contents of the file header back to disk.
///
/// * `sector` is the disk sector to contain the file header.
void
FileHeader::WriteBack(unsigned sector)
{
    synchDisk->WriteSector(sector, (char *) &raw);
}

/// Return which disk sector is storing a particular byte within the file.
/// This is essentially a translation from a virtual address (the offset in
/// the file) to a physical address (the sector where the data at the offset
/// is stored).
///
/// * `offset` is the location within the file of the byte in question.
unsigned
FileHeader::ByteToSector(unsigned offset)
{
    unsigned index = offset / SECTOR_SIZE;
    
    // Si el índice está en los directos
    if (index < NUM_DIRECT){
        return raw.dataSectors[index];
    }

    index -= NUM_DIRECT;
    
    // Si el índice está en los de indirección simple
    if (index < POINTERS_PER_SECTOR){
        unsigned singleBuf[POINTERS_PER_SECTOR];
        synchDisk->ReadSector(raw.singleIndirect, (char *) singleBuf);
        return singleBuf[index];
    } 

    index -= POINTERS_PER_SECTOR;

    // Si el índice está en los de doble indirección
    unsigned block = index / POINTERS_PER_SECTOR;
    unsigned offInBlock = index % POINTERS_PER_SECTOR;

    unsigned doubleBuf[POINTERS_PER_SECTOR];
    synchDisk->ReadSector(raw.doubleIndirect, (char *) doubleBuf);
    
    unsigned dataBuf[POINTERS_PER_SECTOR];
    synchDisk->ReadSector(doubleBuf[block], (char *) dataBuf);
    return dataBuf[offInBlock];
}

/// Return the number of bytes in the file.
unsigned
FileHeader::FileLength() const
{
    return raw.numBytes;
}

/// Print the contents of the file header, and the contents of all the data
/// blocks pointed to by the file header.
void
FileHeader::Print(const char *title)
{
    char *data = new char [SECTOR_SIZE];

    if (title == nullptr) {
        printf("File header:\n");
    } else {
        printf("%s file header:\n", title);
    }

    printf("    size: %u bytes\n"
           "    block indexes: ",
           raw.numBytes);

    for (unsigned i = 0; i < raw.numSectors; i++) {
        printf("%u ", ByteToSector(i * SECTOR_SIZE)); // Para cada bloque, calculamos el sector donde está
    }
    printf("\n");

    for (unsigned i = 0, k = 0; i < raw.numSectors; i++) {
        unsigned sector = ByteToSector(i * SECTOR_SIZE);   
        printf("    contents of block %u:\n", sector);
        synchDisk->ReadSector(sector, data);
        for (unsigned j = 0; j < SECTOR_SIZE && k < raw.numBytes; j++, k++) {
            if (isprint(data[j])) {
                printf("%c", data[j]);
            } else {
                printf("\\%X", (unsigned char) data[j]);
            }
        }
        printf("\n");
    }
    delete [] data;
}

const RawFileHeader *
FileHeader::GetRaw() const
{
    return &raw;
}
