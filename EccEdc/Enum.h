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
	SectorTypeMode1 = 1,
	SectorTypeMode2Form1 = 2,
	SectorTypeMode2Form2 = 3,
	SectorTypeMode2 = 4,
	// Error
	SectorTypeMode1BadEcc = -1,
	SectorTypeMode1ReservedNotZero = -2,
	SectorTypeMode2Form1FlagsNotSame = -3,
	SectorTypeMode2Form2FlagsNotSame = -4,
	SectorTypeMode2FlagsNotSame = -5,
	SectorTypeNonZeroInvalidSync = -6,
	SectorTypeZeroSync = -7,
	SectorTypeUnknownMode = -8,
} SectorType;

typedef enum _TrackMode {
	TrackModeUnknown,
	TrackModeAudio,
	TrackMode1,
	TrackMode2,
} TrackMode;

typedef enum _EXEC_TYPE {
	check,
	checkex,
	fix,
	write
} EXEC_TYPE, *PEXEC_TYPE;
