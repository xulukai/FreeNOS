/*
 * Copyright (C) 2009 Niek Linnenbank
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <BootImage.h>
#include "MemoryServer.h"
#include "MemoryMessage.h"
#include <string.h>

MemoryServer::MemoryServer()
    : IPCServer<MemoryServer, MemoryMessage>(this)
{
    SystemInformation info;
    MemoryRange range;
    BootImage *image;
    BootSymbol *symbol;

    /* Register message handlers. */
    addIPCHandler(CreatePrivate,  &MemoryServer::createPrivate);
    addIPCHandler(ReservePrivate, &MemoryServer::reservePrivate);
    addIPCHandler(ReleasePrivate, &MemoryServer::releasePrivate);
    addIPCHandler(CreateShared,   &MemoryServer::createShared);
    addIPCHandler(SystemMemory,   &MemoryServer::systemMemory);
    
    /* Allocate a user process table. */
    insertShared(SELF, USER_PROCESS_KEY,
		 sizeof(UserProcess) * MAX_PROCS, &range);

    /* Clear it. */
    procs = (UserProcess *) range.virtualAddress;
    memset(procs, 0, sizeof(UserProcess) * MAX_PROCS);

    /* Allocate FileSystemMounts table. */		 
    insertShared(SELF, FILE_SYSTEM_MOUNT_KEY,
		 sizeof(FileSystemMount) * MAX_MOUNTS, &range);
		 
    /* Also Clear it. */
    mounts = (FileSystemMount *) range.virtualAddress;
    memset(mounts, 0, sizeof(FileSystemMount) * MAX_MOUNTS);

    /* We only guarantee that / and /dev are mounted. */
    strlcpy(mounts[0].path, "/dev", PATHLEN);
    strlcpy(mounts[1].path, "/", PATHLEN);
    mounts[0].procID  = DEVSRV_PID;
    mounts[0].options = ZERO;
    mounts[1].procID  = ROOTSRV_PID;
    mounts[1].options = ZERO;

    // Attempt to load the boot image
    range.virtualAddress  = findFreeRange(SELF, PAGESIZE * 2);
    range.physicalAddress = info.bootImageAddress;
    range.access          = Memory::Present | Memory::User | Memory::Readable;

#warning Dangerous value for bytes here?
    range.bytes           = PAGESIZE * 2;
    VMCtl(SELF, Map, &range);
    
    image = (BootImage *) range.virtualAddress;

    /* Loop all embedded programs. */
    for (Size j = 0; j < image->symbolTableCount; j++)
    {
        /* Read out the next program. */
        symbol = (BootSymbol *)(((Address)image) + image->symbolTableOffset);
        symbol += j;

        if (symbol->type != BootProgram)
            continue;

        /* Write commandline. */
        snprintf(procs[j].command, COMMANDLEN,
                "[%s]", symbol->name);

        /* Set current directory. */
        snprintf(procs[j].currentDirectory, PATHLEN, "/");

        /* Set user and group identities. */
        procs[j].userID  = 0;
        procs[j].groupID = 0;
    }
}

Address MemoryServer::findFreeRange(ProcessID procID, Size size)
{
    Address *pageDir, *pageTab, vaddr, vbegin;

    /* Initialize variables. */
    vbegin  = ZERO;
    vaddr   = 1024 * 1024 * 16;
    pageDir = PAGETABADDR_FROM(PAGETABFROM, PAGEUSERFROM);
    pageTab = PAGETABADDR_FROM(vaddr, PAGEUSERFROM);

    /* Map page tables. */
    VMCtl(procID, MapTables);

    /* Scan tables. */
    for (Size inc = PAGESIZE; DIRENTRY(vaddr) < PAGEDIR_MAX ; vaddr += inc)
    {
	/* Is the hole big enough? */
	if (vbegin && vaddr - vbegin >= size)
	{
	    break;
	}
	/* Increment per page table. */
	inc = PAGETAB_MAX * PAGESIZE;
	
	/* Try the current address. */
	if (pageDir[DIRENTRY(vaddr)] & PAGE_RESERVED)
	{
	    vbegin = ZERO; continue;
	}
	else if (pageDir[DIRENTRY(vaddr)] & PAGE_PRESENT)
	{
	    /* Look further into the page table. */
	    inc     = PAGESIZE;
	    pageTab = PAGETABADDR_FROM(vaddr, PAGEUSERFROM);
	
	    if (pageTab[TABENTRY(vaddr)] & PAGE_PRESENT)
	    {
		vbegin = ZERO; continue;
	    }
	}
	/* Reset start address if needed. */
	if (!vbegin)
	{
	    vbegin = vaddr;
	}
    }
    /* Clean up. */
    VMCtl(procID, UnMapTables);
    
    /* Done. */
    return vbegin;
}

Error MemoryServer::insertMapping(ProcessID procID, MemoryRange *range)
{
    MemoryRange tmp;
    Error result;
    
    tmp.virtualAddress = range->virtualAddress;
    tmp.access         = Memory::Present;
    tmp.bytes          = range->bytes;

    /* The given range must be free. */
    if (VMCtl(procID, Access, &tmp))
    {
	return EFAULT;
    }
    /* Perform mapping. */
    if ((result = VMCtl(procID, Map, range)) != 0)
    {
        return result;
    }
    /* Done! */
    return ESUCCESS;
}

SharedMemory * MemoryServer::insertShared(ProcessID procID,
					  char *key, Size size,
					  MemoryRange *range, bool *created)
{
    SharedMemory *obj = findShared(key);
    bool needCreate = obj == ZERO;

    if (needCreate)
        obj = new SharedMemory;

    range->virtualAddress  = findFreeRange(procID, size);
    range->bytes      = size;
    range->access     = Memory::Present | Memory::User | Memory::Readable | Memory::Writable | Memory::Pinned;

    /* Only create a new mapping, if non-existent. */
    if (needCreate)
    {
	range->physicalAddress = ZERO;	
	VMCtl(procID, Map, range);
	
	/* Create new shared memory object. */
	obj->size = size;
	obj->key  = key;
	obj->address = range->physicalAddress;

	/* Insert to the list. */
	shared.append(obj);
	
	/* We created a new mapping, flag that. */
	if (created) *created = true;
    }
    else
    {
	range->physicalAddress = obj->address;
	VMCtl(procID, Map, range);
	
	/* We didn't create a new mapping, flag that. */
	if (created) *created = false;
    }
    /* Done. */
    return obj;
}

SharedMemory * MemoryServer::findShared(char *key)
{
    for (ListIterator<SharedMemory *> i(&shared); i.hasCurrent(); i++)
    {
	if (strcmp(*i.current()->key, key) == 0)
	{
	    return i.current();
	}
    }
    return ZERO;
}
