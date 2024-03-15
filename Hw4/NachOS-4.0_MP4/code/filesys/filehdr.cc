// filehdr.cc
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector,
//
//      Unlike in a real system, we do not keep track of file permissions,
//	ownership, last modification date, etc., in the file header.
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "debug.h"
#include "filehdr.h"
#include "main.h"
#include "synchdisk.h"

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool FileHeader::Allocate(PersistentBitmap *freeMap, int fileSize)
{

    numBytes = fileSize;
    numSectors = divRoundUp(fileSize, SectorSize);
    if (freeMap->NumClear() < numSectors)
        return FALSE; // not enough space
    int idx = 0;
    // for (int i = 0; i < numSectors; i++) {
    // dataSectors[i] = freeMap->FindAndSet();
    // // since we checked that there was enough free space,
    // // we expect this to succeed
    // ASSERT(dataSectors[i] >= 0);
    // }

    if (fileSize > bytes_in_level3)
    {
        while (fileSize > 0)
        {
            // DEBUG(dbgFile, fileSize<<" level 3");
            dataSectors[idx] = freeMap->FindAndSet();
            ASSERT(dataSectors[idx] >= 0);
            FileHeader *next_level = new FileHeader();
            if (fileSize >= bytes_in_level3)
                next_level->Allocate(freeMap, bytes_in_level3);
            else
                next_level->Allocate(freeMap, fileSize);
            fileSize -= bytes_in_level3;
            next_level->WriteBack(dataSectors[idx]);
            idx++;
        }
    }
    else if (fileSize > bytes_in_level2)
    {
        while (fileSize > 0)
        {
            // DEBUG(dbgFile, fileSize<<" level 2");
            dataSectors[idx] = freeMap->FindAndSet();
            ASSERT(dataSectors[idx] >= 0);
            FileHeader *next_level = new FileHeader();
            if (fileSize >= bytes_in_level2)
                next_level->Allocate(freeMap, bytes_in_level2);
            else
                next_level->Allocate(freeMap, fileSize);
            fileSize -= bytes_in_level2;
            next_level->WriteBack(dataSectors[idx]);
            idx++;
        }
    }
    else if (fileSize > bytes_in_level1)
    {
        while (fileSize > 0)
        {
            dataSectors[idx] = freeMap->FindAndSet();
            ASSERT(dataSectors[idx] >= 0);
            FileHeader *next_level = new FileHeader();
            if (fileSize >= bytes_in_level1)
                next_level->Allocate(freeMap, bytes_in_level1);
            else
                next_level->Allocate(freeMap, fileSize);
            fileSize -= bytes_in_level1;
            next_level->WriteBack(dataSectors[idx]);
            idx++;
            // DEBUG(dbgFile, fileSize<<" level 1");
        }
    }
    else
    {
        for (int i = 0; i < numSectors; i++)
        {
            // DEBUG(dbgFile, fileSize<< " data "<<i);
            dataSectors[i] = freeMap->FindAndSet();
            // since we checked that there was enough free space,
            // we expect this to succeed
            ASSERT(dataSectors[i] >= 0);
            // char* clean_data = new char[SectorSize]();
            // kernel->synchDisk->WriteSector(dataSectors[i], clean_data);
            // delete clean_data;
        }
    }

    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void FileHeader::Deallocate(PersistentBitmap *freeMap)
{
    // for (int i = 0; i < numSectors; i++)
    // {
    //     ASSERT(freeMap->Test((int)dataSectors[i])); // ought to be marked!
    //     freeMap->Clear((int)dataSectors[i]);
    // }
    int remainBytes = numBytes;
    int idx = 0;
    if (remainBytes > bytes_in_level3)
    {
        while (remainBytes > 0)
        {
            // DEBUG(dbgFile, remainBytes<<" level 3");
            FileHeader *next_level = new FileHeader;
            next_level->FetchFrom(dataSectors[idx]);
            next_level->Deallocate(freeMap);
            remainBytes -= bytes_in_level3;
            idx++;
        }
    }
    else if (remainBytes > bytes_in_level2)
    {
        while (remainBytes > 0)
        {
            // DEBUG(dbgFile, remainBytes<<" level 2");
            FileHeader *next_level = new FileHeader;
            next_level->FetchFrom(dataSectors[idx]);
            next_level->Deallocate(freeMap);
            remainBytes -= bytes_in_level2;
            idx++;
        }
    }
    else if (remainBytes > bytes_in_level1)
    {
        while (remainBytes > 0)
        {
            // DEBUG(dbgFile, remainBytes<<" level 1");
            FileHeader *next_level = new FileHeader;
            next_level->FetchFrom(dataSectors[idx]);
            next_level->Deallocate(freeMap);
            remainBytes -= bytes_in_level1;
            idx++;
        }
    }
    else
    {
        while (remainBytes > 0)
        {
            // DEBUG(dbgFile, remainBytes<<" data");
            ASSERT(freeMap->Test((int)dataSectors[idx]));
            freeMap->Clear((int)dataSectors[idx]);
            remainBytes -= SectorSize;
            idx++;
        }
    }
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk.
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void FileHeader::FetchFrom(int sector)
{
    kernel->synchDisk->ReadSector(sector, (char *)this);
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk.
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void FileHeader::WriteBack(int sector)
{
    kernel->synchDisk->WriteSector(sector, (char *)this);
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int FileHeader::ByteToSector(int offset)
{

    // DEBUG(dbgFile, "hi");
    int idx = 0;
    if (numBytes > bytes_in_level3)
    {
        idx = divRoundDown(offset, bytes_in_level3);
        FileHeader *next_level = new FileHeader;
        next_level->FetchFrom(dataSectors[idx]);
        next_level->ByteToSector(offset - bytes_in_level3 * idx);
    }
    else if (numBytes > bytes_in_level2)
    {
        idx = divRoundDown(offset, bytes_in_level2);
        FileHeader *next_level = new FileHeader;
        next_level->FetchFrom(dataSectors[idx]);
        next_level->ByteToSector(offset - bytes_in_level2 * idx);
    }
    else if (numBytes > bytes_in_level1)
    {
        idx = divRoundDown(offset, bytes_in_level1);
        FileHeader *next_level = new FileHeader;
        next_level->FetchFrom(dataSectors[idx]);
        next_level->ByteToSector(offset - bytes_in_level1 * idx);
    }
    else
        return (dataSectors[offset / SectorSize]);
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int FileHeader::FileLength()
{
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void FileHeader::Print()
{
    int i, j, k;
    char *data = new char[SectorSize];

    printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
    for (i = 0; i < numSectors; i++)
        printf("%d ", dataSectors[i]);
    printf("\nFile contents:\n");

    if (numBytes > bytes_in_level3)
    {
        int dir_count = divRoundUp(numBytes, bytes_in_level3);
        for (int i = 0; i < dir_count; i++)
        {
            OpenFile *openFile = new OpenFile(dataSectors[i]);
            FileHeader *next_level = openFile->get_hdr();
            next_level->Print();
        }
    }
    else if (numBytes > bytes_in_level2)
    {
        int dir_count = divRoundUp(numBytes, bytes_in_level2);
        for (int i = 0; i < dir_count; i++)
        {
            OpenFile *openFile = new OpenFile(dataSectors[i]);
            FileHeader *next_level = openFile->get_hdr();
            next_level->Print();
        }
    }
    else if (numBytes > bytes_in_level1)
    {
        int dir_count = divRoundUp(numBytes, bytes_in_level1);
        for (int i = 0; i < dir_count; i++)
        {
            OpenFile *openFile = new OpenFile(dataSectors[i]);
            FileHeader *next_level = openFile->get_hdr();
            next_level->Print();
        }
    }
    else
    {
        for (i = k = 0; i < numSectors; i++)
        {
            kernel->synchDisk->ReadSector(dataSectors[i], data);
            for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++)
            {
                if ('\040' <= data[j] && data[j] <= '\176') // isprint(data[j])
                    printf("%c", data[j]);
                else
                    printf("\\%x", (unsigned char)data[j]);
            }
            printf("\n");
        }
    }
    delete[] data;
}

int FileHeader::CountHeader()
{
    int remainBytes = numBytes;
    int idx = 0;
    int count = 0;
    DEBUG(dbgFile, "RemainBytes: " << remainBytes);
    if (remainBytes > bytes_in_level3)
    {
        while (remainBytes > 0)
        {
            // DEBUG(dbgFile, remainBytes<<" level 3");
            FileHeader *next_level = new FileHeader;
            next_level->FetchFrom(dataSectors[idx]);
            count += next_level->CountHeader();
            remainBytes -= bytes_in_level3;
            idx++;
        }
        DEBUG(dbgFile, " level 3: " << count);
        return count;
    }
    else if (remainBytes > bytes_in_level2)
    {
        while (remainBytes > 0)
        {
            // DEBUG(dbgFile, remainBytes<<" level 2");
            FileHeader *next_level = new FileHeader;
            next_level->FetchFrom(dataSectors[idx]);
            count += next_level->CountHeader();
            remainBytes -= bytes_in_level2;
            idx++;
        }
        DEBUG(dbgFile, " level 2: " << count);
        return count;
    }
    else if (remainBytes > bytes_in_level1)
    {
        while (remainBytes > 0)
        {
            // DEBUG(dbgFile, remainBytes<<" level 1");
            FileHeader *next_level = new FileHeader;
            next_level->FetchFrom(dataSectors[idx]);
            count += next_level->CountHeader();
            remainBytes -= bytes_in_level1;
            idx++;
        }
        DEBUG(dbgFile, " level 1: " << count);
        return count;
    }
    else
    {
        return 1;
    }
}
