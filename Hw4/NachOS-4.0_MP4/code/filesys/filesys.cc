// filesys.cc
//	Routines to manage the overall operation of the file system.
//	Implements routines to map from textual file names to files.
//
//	Each file in the file system has:
//	   A file header, stored in a sector on disk
//		(the size of the file header data structure is arranged
//		to be precisely the size of 1 disk sector)
//	   A number of data blocks
//	   An entry in the file system directory
//
// 	The file system consists of several data structures:
//	   A bitmap of free disk sectors (cf. bitmap.h)
//	   A directory of file names and file headers
//
//      Both the bitmap and the directory are represented as normal
//	files.  Their file headers are located in specific sectors
//	(sector 0 and sector 1), so that the file system can find them
//	on bootup.
//
//	The file system assumes that the bitmap and directory files are
//	kept "open" continuously while Nachos is running.
//
//	For those operations (such as Create, Remove) that modify the
//	directory and/or bitmap, if the operation succeeds, the changes
//	are written immediately back to disk (the two files are kept
//	open during all this time).  If the operation fails, and we have
//	modified part of the directory and/or bitmap, we simply discard
//	the changed version, without writing it back to disk.
//
// 	Our implementation at this point has the following restrictions:
//
//	   there is no synchronization for concurrent accesses
//	   files have a fixed size, set when the file is created
//	   files cannot be bigger than about 3KB in size
//	   there is no hierarchical directory structure, and only a limited
//	     number of files can be added to the system
//	   there is no attempt to make the system robust to failures
//	    (if Nachos exits in the middle of an operation that modifies
//	    the file system, it may corrupt the disk)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.
#ifndef FILESYS_STUB

#include "filesys.h"
#include "copyright.h"
#include "debug.h"
#include "directory.h"
#include "disk.h"
#include "filehdr.h"
#include "pbitmap.h"

// Sectors containing the file headers for the bitmap of free sectors,
// and the directory of files.  These file headers are placed in well-known
// sectors, so that they can be located on boot-up.
#define FreeMapSector 0
#define DirectorySector 1

// Initial file sizes for the bitmap and directory; until the file system
// supports extensible files, the directory size sets the maximum number
// of files that can be loaded onto the disk.
#define FreeMapFileSize (NumSectors / BitsInByte)
#define NumDirEntries 64
#define DirectoryFileSize (sizeof(DirectoryEntry) * NumDirEntries)

//----------------------------------------------------------------------
// FileSystem::FileSystem
// 	Initialize the file system.  If format = TRUE, the disk has
//	nothing on it, and we need to initialize the disk to contain
//	an empty directory, and a bitmap of free sectors (with almost but
//	not all of the sectors marked as free).
//
//	If format = FALSE, we just have to open the files
//	representing the bitmap and the directory.
//
//	"format" -- should we initialize the disk?
//----------------------------------------------------------------------

FileSystem::FileSystem(bool format)
{
    DEBUG(dbgFile, "Initializing the file system.");
    if (format)
    {
        PersistentBitmap *freeMap = new PersistentBitmap(NumSectors);
        Directory *directory = new Directory(NumDirEntries);
        FileHeader *mapHdr = new FileHeader;
        FileHeader *dirHdr = new FileHeader;

        DEBUG(dbgFile, "Formatting the file system.");

        // First, allocate space for FileHeaders for the directory and bitmap
        // (make sure no one else grabs these!)
        freeMap->Mark(FreeMapSector);
        freeMap->Mark(DirectorySector);

        // Second, allocate space for the data blocks containing the contents
        // of the directory and bitmap files.  There better be enough space!

        ASSERT(mapHdr->Allocate(freeMap, FreeMapFileSize));
        ASSERT(dirHdr->Allocate(freeMap, DirectoryFileSize));

        // Flush the bitmap and directory FileHeaders back to disk
        // We need to do this before we can "Open" the file, since open
        // reads the file header off of disk (and currently the disk has garbage
        // on it!).

        DEBUG(dbgFile, "Writing headers back to disk.");
        mapHdr->WriteBack(FreeMapSector);
        dirHdr->WriteBack(DirectorySector);

        // OK to open the bitmap and directory files now
        // The file system operations assume these two files are left open
        // while Nachos is running.

        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);

        // Once we have the files "open", we can write the initial version
        // of each file back to disk.  The directory at this point is completely
        // empty; but the bitmap has been changed to reflect the fact that
        // sectors on the disk have been allocated for the file headers and
        // to hold the file data for the directory and bitmap.

        DEBUG(dbgFile, "Writing bitmap and directory back to disk.");
        freeMap->WriteBack(freeMapFile); // flush changes to disk
        directory->WriteBack(directoryFile);

        if (debug->IsEnabled('f'))
        {
            freeMap->Print();
            directory->Print();
        }
        delete freeMap;
        delete directory;
        delete mapHdr;
        delete dirHdr;
        DEBUG(dbgFile, "-f done");
    }
    else
    {
        // if we are not formatting the disk, just open the files representing
        // the bitmap and directory; these are left open while Nachos is running
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
    }
}

//----------------------------------------------------------------------
// FileSystem::Create
// 	Create a file in the Nachos file system (similar to UNIX create).
//	Since we can't increase the size of files dynamically, we have
//	to give Create the initial size of the file.
//
//	The steps to create a file are:
//	  Make sure the file doesn't already exist
//        Allocate a sector for the file header
// 	  Allocate space on disk for the data blocks for the file
//	  Add the name to the directory
//	  Store the new file header on disk
//	  Flush the changes to the bitmap and the directory back to disk
//
//	Return TRUE if everything goes ok, otherwise, return FALSE.
//
// 	Create fails if:
//   		file is already in directory
//	 	no free space for file header
//	 	no free entry for file in directory
//	 	no free space for data blocks for the file
//
// 	Note that this implementation assumes there is no concurrent access
//	to the file system!
//
//	"name" -- name of file to be created
//	"initialSize" -- size of file to be created
//----------------------------------------------------------------------

bool FileSystem::Create(char *name, int initialSize)
{
    Directory *directory;
    PersistentBitmap *freeMap;
    OpenFile *temp;
    OpenFile *prevDirOpenFile = directoryFile;
    FileHeader *hdr;
    int sector, new_sector;
    bool success;
    DEBUG(dbgFile, "creating file " << name);
    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    char *token = strtok(name, "/");
    while (token != NULL)
    {
        sector = directory->FindDir(token);
        if (sector < 0)
            break;
        temp = new OpenFile(sector);
        prevDirOpenFile = temp;
        directory->FetchFrom(temp);
        token = strtok(NULL, "/");
    }
    DEBUG(dbgFile, token);
    sector = directory->Find(token);
    if (sector >= 0)
        success = FALSE; // file is already in directory
    else
    {
        freeMap = new PersistentBitmap(freeMapFile, NumSectors);
        new_sector = freeMap->FindAndSet(); // find a sector to hold the file header
        if (new_sector == -1)
            success = FALSE; // no free block for file header
        else if (!directory->Add(token, new_sector))
            success = FALSE; // no space in directory
        else
        {
            FileHeader *hdr = new FileHeader();
            if (!hdr->Allocate(freeMap, initialSize))
                success = FALSE; // no space on disk for file
            else
            {
                success = TRUE;
                // everthing worked, flush all changes back to disk
                hdr->WriteBack(new_sector);
                directory->WriteBack(prevDirOpenFile);
                freeMap->WriteBack(freeMapFile);
            }
            delete hdr;
        }
        delete freeMap;
    }
    delete directory;
    return success;
}

bool FileSystem::CreateSubDir(char *name)
{
    Directory *directory;
    PersistentBitmap *freeMap;
    FileHeader *hdr;
    OpenFile *temp;
    OpenFile *prevDirOpenFile = directoryFile;
    int sector, new_sector;
    bool success;
    DEBUG(dbgFile, "creating dir " << name);
    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    char *token = strtok(name, "/");
    while (token != NULL)
    {
        sector = directory->FindDir(token);
        if (sector < 0)
            break;
        temp = new OpenFile(sector);
        prevDirOpenFile = temp;
        directory->FetchFrom(temp);
        token = strtok(NULL, "/");
    }
    DEBUG(dbgFile, token);
    if (token == NULL)
        success = FALSE; // dir is already in directory
    else
    {
        freeMap = new PersistentBitmap(freeMapFile, NumSectors);
        new_sector = freeMap->FindAndSet(); // find a sector to hold the dir header
        if (new_sector == -1)
            success = FALSE; // no free block for dir header
        else if (!directory->AddDir(token, new_sector))
            success = FALSE; // no space in directory
        else
        {
            FileHeader *dirhdr = new FileHeader();
            if (!dirhdr->Allocate(freeMap, DirectoryFileSize))
                success = FALSE; // no space on disk for dir
            else
            {
                success = TRUE;
                dirhdr->WriteBack(new_sector);
                Directory *SubDir = new Directory(NumDirEntries);
                OpenFile *NewDirFile = new OpenFile(new_sector);
                SubDir->WriteBack(NewDirFile);
                directory->WriteBack(prevDirOpenFile);
                freeMap->WriteBack(freeMapFile);
            }
            delete dirhdr;
        }
        delete freeMap;
    }
    delete directory;
    return success;
}

//----------------------------------------------------------------------
// FileSystem::Open
// 	Open a file for reading and writing.
//	To open a file:
//	  Find the location of the file's header, using the directory
//	  Bring the header into memory
//
//	"name" -- the text name of the file to be opened
//----------------------------------------------------------------------

OpenFile *
FileSystem::Open(char *name)
{
    Directory *directory = new Directory(NumDirEntries);
    OpenFile *openFile = NULL;
    OpenFile *temp;
    int sector;
    char *token;
    DEBUG(dbgFile, "Opening file" << name);
    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    token = strtok(name, "/");
    while (token != NULL)
    {
        sector = directory->FindDir(token);
        if (sector < 0)
            break;
        temp = new OpenFile(sector);
        directory->FetchFrom(temp);
        token = strtok(NULL, "/");
    }
    sector = directory->Find(token);

    if (sector >= 0)
    {
        openFile = new OpenFile(sector); // name was found in directory
        // MP4
        openFile->id = sector;
        this->openFile = openFile;
        // MP4
    }
    delete directory;
    return openFile; // return NULL if not found
}

//----------------------------------------------------------------------
// FileSystem::Remove
// 	Delete a file from the file system.  This requires:
//	    Remove it from the directory
//	    Delete the space for its header
//	    Delete the space for its data blocks
//	    Write changes to directory, bitmap back to disk
//
//	Return TRUE if the file was deleted, FALSE if the file wasn't
//	in the file system.
//
//	"name" -- the text name of the file to be removed
//----------------------------------------------------------------------

bool FileSystem::Remove(char *name)
{
    Directory *directory;
    PersistentBitmap *freeMap;
    FileHeader *fileHdr;
    OpenFile *temp;
    OpenFile *prevDirOpenFile;
    int sector;
    char *token;
    prevDirOpenFile = directoryFile;
    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    token = strtok(name, "/");
    while (token != NULL)
    {
        sector = directory->FindDir(token);
        if (sector < 0)
            break;
        temp = new OpenFile(sector);
        prevDirOpenFile = temp;
        directory->FetchFrom(temp);
        token = strtok(NULL, "/");
    }
    sector = directory->Find(token);

    if (sector == -1)
    {
        delete directory;
        return FALSE; // file not found
    }
    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector);

    freeMap = new PersistentBitmap(freeMapFile, NumSectors);

    fileHdr->Deallocate(freeMap); // remove data blocks
    freeMap->Clear(sector);       // remove header block
    directory->Remove(token);

    freeMap->WriteBack(freeMapFile);       // flush to disk
    directory->WriteBack(prevDirOpenFile); // flush to disk
    delete fileHdr;
    delete directory;
    delete freeMap;
    return TRUE;
}

bool FileSystem::RemoveDir(char *name)
{
    Directory *directory;
    PersistentBitmap *freeMap;
    FileHeader *fileHdr;
    OpenFile *temp = NULL;
    OpenFile *prevDirOpenFile;
    int sector;
    char *token;
    char *pre_token; // save the previous token
    prevDirOpenFile = directoryFile;
    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    DEBUG(dbgFile, "in RemoveDir " << name);
    token = strtok(name, "/");
    while (token != NULL)
    {
        prevDirOpenFile = (temp == NULL) ? directoryFile : temp;
        sector = directory->FindDir(token);
        if (sector < 0)
            break;
        temp = new OpenFile(sector);
        directory->FetchFrom(temp);
        pre_token = token;
        token = strtok(NULL, "/");
    }

    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector);

    freeMap = new PersistentBitmap(freeMapFile, NumSectors);

    fileHdr->Deallocate(freeMap);          // remove data blocks
    freeMap->Clear(sector);                // remove header block
    directory->FetchFrom(prevDirOpenFile); // move back to the previous dir
    DEBUG(dbgFile, pre_token);
    directory->Remove(pre_token);

    freeMap->WriteBack(freeMapFile);       // flush to disk
    directory->WriteBack(prevDirOpenFile); // flush to disk
    delete fileHdr;
    delete directory;
    delete freeMap;
    return TRUE;
}

bool FileSystem::RecurRemove(char *name)
{
    int sector;
    OpenFile *temp;
    OpenFile *prevDirOpenFile;
    Directory *directory = new Directory(NumDirEntries);
    char backup_name[270] = {'/0'};
    strcpy(backup_name, name);
    prevDirOpenFile = directoryFile;
    directory->FetchFrom(directoryFile);
    char *token = strtok(name, "/");
    while (token != NULL)
    {
        sector = directory->FindDir(token);
        if (sector < 0)
            break;
        temp = new OpenFile(sector);
        prevDirOpenFile = temp;
        directory->FetchFrom(temp);
        token = strtok(NULL, "/");
    }
    Directory *prevDirectory = new Directory(NumDirEntries);
    prevDirectory->FetchFrom(prevDirOpenFile);
    if (token == NULL)
    { // directory是我們要刪除的那個dir
        int tableSize = directory->getTableSize();
        DirectoryEntry *table = directory->getTable();
        char recur_name[270] = {'/0'};
        for (int i = 0; i < tableSize; i++)
        {
            if (table[i].inUse)
            {
                if (table[i].isdir)
                {
                    strcpy(recur_name, backup_name);
                    strcat(recur_name, table[i].name);
                    strcat(recur_name, "/");
                    DEBUG(dbgFile, "deleteing enrty " << recur_name << " in dir " << backup_name);
                    RecurRemove(recur_name);
                }
                else
                {
                    strcpy(recur_name, backup_name);
                    strcat(recur_name, table[i].name);
                    DEBUG(dbgFile, "deleting file " << recur_name << " in dir " << backup_name);
                    Remove(recur_name);
                }
                DEBUG(dbgFile, "all files in dir are deleted");
            }
        }
        DEBUG(dbgFile, "deleting self dir");
        strcpy(recur_name, backup_name);
        RemoveDir(recur_name);
    }
    else if (prevDirectory->Find(token) != -1)
    { // prevDirectory是我們要刪除的file所在的dir
        strcpy(name, backup_name);
        DEBUG(dbgFile, "deleting file " << name);
        Remove(name);
    }
    delete directory;
    return TRUE;
}

//----------------------------------------------------------------------
// FileSystem::List
// 	List all the files in the file system directory.
//----------------------------------------------------------------------

void FileSystem::List(char *name)
{
    int sector;
    OpenFile *temp;
    Directory *directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    char *token = strtok(name, "/");
    while (token != NULL)
    {
        sector = directory->FindDir(token);
        if (sector < 0)
            break;
        temp = new OpenFile(sector);
        directory->FetchFrom(temp);
        token = strtok(NULL, "/");
    }
    directory->List();
    delete directory;
}

void FileSystem::RecurList(char *name)
{
    int sector;
    OpenFile *temp;
    Directory *directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    char *token = strtok(name, "/");
    while (token != NULL)
    {
        sector = directory->FindDir(token);
        if (sector < 0)
            break;
        temp = new OpenFile(sector);
        directory->FetchFrom(temp);
        token = strtok(NULL, "/");
    }
    directory->RecurList(0);
    delete directory;
}

//----------------------------------------------------------------------
// FileSystem::Print
// 	Print everything about the file system:
//	  the contents of the bitmap
//	  the contents of the directory
//	  for each file in the directory,
//	      the contents of the file header
//	      the data in the file
//----------------------------------------------------------------------

void FileSystem::Print()
{
    FileHeader *bitHdr = new FileHeader;
    FileHeader *dirHdr = new FileHeader;
    PersistentBitmap *freeMap = new PersistentBitmap(freeMapFile, NumSectors);
    Directory *directory = new Directory(NumDirEntries);

    printf("Bit map file header:\n");
    bitHdr->FetchFrom(FreeMapSector);
    bitHdr->Print();

    printf("Directory file header:\n");
    dirHdr->FetchFrom(DirectorySector);
    dirHdr->Print();

    freeMap->Print();

    directory->FetchFrom(directoryFile);
    directory->Print();

    delete bitHdr;
    delete dirHdr;
    delete freeMap;
    delete directory;
}

int FileSystem::Read(char *buf, int size, OpenFileId id)
{
    return this->openFile->Read(buf, size);
}

int FileSystem::Write(char *buf, int size, OpenFileId id)
{
    return this->openFile->Write(buf, size);
}

int FileSystem::Close(OpenFileId id)
{
    delete this->openFile;
    return 1;
}

void FileSystem::CountHeaderSize(char *name)
{
    Directory *directory;
    FileHeader *fileHdr;
    OpenFile *temp;
    OpenFile *prevDirOpenFile;
    int sector;
    int headerSize;
    char *token;
    prevDirOpenFile = directoryFile;
    directory = new Directory(NumDirEntries);
    directory->FetchFrom(directoryFile);
    token = strtok(name, "/");
    while (token != NULL)
    {
        sector = directory->FindDir(token);
        if (sector < 0)
            break;
        temp = new OpenFile(sector);
        prevDirOpenFile = temp;
        directory->FetchFrom(temp);
        token = strtok(NULL, "/");
    }
    sector = directory->Find(token);

    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector);

    std::cout << "File " << token << " size: " << fileHdr->FileLength() << "bytes"<< std::endl;


    headerSize = fileHdr->CountHeader();
    DEBUG(dbgFile, "Count header: " << headerSize);

    headerSize = headerSize * SectorSize;

    std::cout << "File Header of " << token << " has: " << headerSize << "bytes" << std::endl;

    delete fileHdr;
    delete directory;
}

#endif // FILESYS_STUB
