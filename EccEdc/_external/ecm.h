////////////////////////////////////////////////////////////////////////////////
//
// #define TITLE "ecm - Encoder/decoder for Error Code Modeler format"
// #define COPYR "Copyright (C) 2002-2011 Neill Corlett"
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "../Enum.h"

#ifdef _WIN32
typedef   signed __int8   int8_t;
typedef unsigned __int8  uint8_t;
typedef   signed __int16  int16_t;
typedef unsigned __int16 uint16_t;
typedef   signed __int32  int32_t;
typedef unsigned __int32 uint32_t;
#else
typedef   signed char   int8_t;
typedef unsigned char  uint8_t;
typedef   signed short  int16_t;
typedef unsigned short uint16_t;
//typedef   signed long   int32_t;
//typedef unsigned long  uint32_t;
#endif

void eccedc_init(void);
SectorType detect_sector(const uint8_t* sector, size_t size_available, TrackMode *trackMode);
bool reconstruct_sector(
	uint8_t* sector, // must point to a full 2352-byte sector
	SectorType type
);
