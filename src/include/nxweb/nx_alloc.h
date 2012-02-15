/*
 * Copyright (c) 2011-2012 Yaroslav Stavnichiy <yarosla@gmail.com>
 * 
 * This file is part of NXWEB.
 * 
 * NXWEB is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 * 
 * NXWEB is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with NXWEB. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NX_ALLOC_H
#define	NX_ALLOC_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <malloc.h>

#define MEM_GUARD 64
#define nx_alloc(size) memalign(MEM_GUARD, (size)+MEM_GUARD)
#define nx_calloc(size) ({void* _pTr=memalign(MEM_GUARD, (size)+MEM_GUARD); memset(_pTr, 0, (size)); _pTr;})
#define nx_free(ptr) free(ptr)


#ifdef	__cplusplus
}
#endif

#endif	/* NX_ALLOC_H */
