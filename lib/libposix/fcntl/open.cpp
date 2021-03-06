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

#include <FreeNOS/API.h>
#include <FileSystemMessage.h>
#include "Runtime.h"
#include <errno.h>
#include "fcntl.h"

int open(const char *path, int oflag, ...)
{
    FileSystemMessage msg;
    ProcessID mnt = findMount(path);
    
    /* Fill message. */
    msg.action = OpenFile;
    msg.buffer = (char *) path;
    
    /* Ask the FileSystem. */
    if (mnt)
    {
	IPCMessage(mnt, SendReceive, &msg, sizeof(msg));
    
	/* Set errno. */
	errno = msg.result;
    }
    else
	errno = ENOENT;
    
    /* Success. */
    return msg.result == ESUCCESS ? msg.fd : -1;
}
