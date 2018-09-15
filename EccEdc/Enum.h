////////////////////////////////////////////////////////////////////////////////
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

typedef enum _SectorType {
	// Correct
	SectorTypeNothing = 0,
	SectorTypeMode0 = 1,
	SectorTypeMode1 = 2,
	SectorTypeMode2Form1 = 3,
	SectorTypeMode2Form2 = 4,
	SectorTypeMode2 = 5,
	// Error or Warning
	SectorTypeMode0NotAllZero = -1,
	SectorTypeMode1BadEcc = -2,
	SectorTypeMode1ReservedNotZero = -3,
	SectorTypeMode2Form1SubheaderNotSame = -4,
	SectorTypeMode2Form2SubheaderNotSame = -5,
	SectorTypeMode2SubheaderNotSame = -6,
	SectorTypeNonZeroInvalidSync = -7,
	SectorTypeZeroSync = -8,
	SectorTypeInvalidMode0 = -9,
	SectorTypeInvalidMode1 = -10,
	SectorTypeInvalidMode2Form1 = -11,
	SectorTypeInvalidMode2Form2 = -12,
	SectorTypeInvalidMode2 = -13,
	SectorTypeUnknownMode = -14,
} SectorType;

typedef enum _TrackMode {
	TrackModeUnknown,
	TrackModeAudio,
	TrackMode0,
	TrackMode1,
	TrackMode2,
} TrackMode;

typedef enum _EXEC_TYPE {
	check,
	checkex,
	fix,
	_write
} EXEC_TYPE, *PEXEC_TYPE;

typedef enum _LOG_TYPE {
	standardOut = 1,
	standardError = 1 << 1,
	file = 1 << 2,
} LOG_TYPE, *PLOG_TYPE;
