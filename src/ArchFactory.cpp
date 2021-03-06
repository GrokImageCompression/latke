/*
 * Copyright 2016-2020 Grok Image Compression Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "ArchFactory.h"

namespace ltk {

IArch* ArchFactory::getArchitecture(cl_uint vendorId){
	switch (vendorId){
	case vendorIdAMD:
		return new ArchAMD();
	case vendorIdNVD:
		return new ArchNVD();
    case vendorIdINTL:
        return new ArchINTL();
    case vendorIdXILINX:
        return new ArchXILINX();
	default:
		return nullptr;
	}
}

}
