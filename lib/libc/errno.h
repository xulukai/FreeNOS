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

#ifndef __LIBC_ERRNO_H
#define __LIBC_ERRNO_H

#include <Macros.h>
#include <Types.h>
#include <Error.h>

/**
 * @defgroup libc libc (ISO C99)
 * @{
 */

/**
 * The lvalue errno is used by many functions to return error values.
 */
extern C int errno;

/**
 * Contains a array of character strings, representing errno values.
 */
extern C char *error_map[];

/**
 * @}
 */

#endif /* __LIBC_ERRNO_H */
