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
	Nothing = 0,
	Mode0 = 1,
	Mode1 = 2,
	Mode2Form1 = 3,
	Mode2Form2 = 4,
	Mode2 = 5,
	// Error or Warning
	Mode0NotAllZero = -1,
	Mode1BadEcc = -2,
	Mode1ReservedNotZero = -3,
	Mode2Form1SubheaderNotSame = -4,
	Mode2Form2SubheaderNotSame = -5,
	Mode2SubheaderNotSame = -6,
	NonZeroInvalidSync = -7,
	ZeroSync = -8,
	InvalidMode0 = -9,
	InvalidMode1 = -10,
	InvalidMode2Form1 = -11,
	InvalidMode2Form2 = -12,
	InvalidMode2 = -13,
	UnknownMode = -14,
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
