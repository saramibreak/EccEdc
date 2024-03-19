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

#include "ecm.h"
#ifdef __linux__
#pragma GCC diagnostic ignored "-Wconversion"
#endif

////////////////////////////////////////////////////////////////////////////////
//
// Sector types
//
// Mode 1
// -----------------------------------------------------
//        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
// 0000h 00 FF FF FF FF FF FF FF FF FF FF 00 [-ADDR-] 01
// 0010h [---DATA...
// ...
// 0800h                                     ...DATA---]
// 0810h [---EDC---] 00 00 00 00 00 00 00 00 [---ECC...
// ...
// 0920h                                      ...ECC---]
// -----------------------------------------------------
//
// Mode 2 (XA), form 1
// -----------------------------------------------------
//        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
// 0000h 00 FF FF FF FF FF FF FF FF FF FF 00 [-ADDR-] 02
// 0010h [--FLAGS--] [--FLAGS--] [---DATA...
// ...
// 0810h             ...DATA---] [---EDC---] [---ECC...
// ...
// 0920h                                      ...ECC---]
// -----------------------------------------------------
//
// Mode 2 (XA), form 2
// -----------------------------------------------------
//        0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
// 0000h 00 FF FF FF FF FF FF FF FF FF FF 00 [-ADDR-] 02
// 0010h [--FLAGS--] [--FLAGS--] [---DATA...
// ...
// 0920h                         ...DATA---] [---EDC---]
// -----------------------------------------------------
//
// ADDR:  Sector address, encoded as minutes:seconds:frames in BCD
// FLAGS: Used in Mode 2 (XA) sectors describing the type of sector; repeated
//        twice for redundancy
// DATA:  Area of the sector which contains the actual data itself
// EDC:   Error Detection Code
// ECC:   Error Correction Code
//

////////////////////////////////////////////////////////////////////////////////

static uint32_t get32lsb(const uint8_t* src) {
	return
		(((uint32_t)(src[0])) << 0) |
		(((uint32_t)(src[1])) << 8) |
		(((uint32_t)(src[2])) << 16) |
		(((uint32_t)(src[3])) << 24);
}

static void put32lsb(uint8_t* dest, uint32_t value) {
	dest[0] = (uint8_t)(value);
	dest[1] = (uint8_t)(value >> 8);
	dest[2] = (uint8_t)(value >> 16);
	dest[3] = (uint8_t)(value >> 24);
}

////////////////////////////////////////////////////////////////////////////////
//
// LUTs used for computing ECC/EDC
//
static uint8_t  ecc_f_lut[256];
static uint8_t  ecc_b_lut[256];
static uint32_t edc_lut[256];

void eccedc_init(void) {
	size_t i;
	for (i = 0; i < 256; i++) {
		uint32_t edc = i;
		size_t j = (i << 1) ^ (i & 0x80 ? 0x11D : 0);
		ecc_f_lut[i] = j;
		ecc_b_lut[i ^ j] = i;
		for (j = 0; j < 8; j++) {
			edc = (edc >> 1) ^ (edc & 1 ? 0xD8018001 : 0);
		}
		edc_lut[i] = edc;
	}
}

////////////////////////////////////////////////////////////////////////////////
//
// Compute EDC for a block
//
static uint32_t edc_compute(
	uint32_t edc,
	const uint8_t* src,
	size_t size
) {
	for (; size; size--) {
		edc = (edc >> 8) ^ edc_lut[(edc ^ (*src++)) & 0xFF];
	}
	return edc;
}

////////////////////////////////////////////////////////////////////////////////
//
// Check ECC block (either P or Q)
// Returns true if the ECC data is an exact match
//
static int8_t ecc_checkpq(
	const uint8_t* address,
	const uint8_t* data,
	size_t major_count,
	size_t minor_count,
	size_t major_mult,
	size_t minor_inc,
	const uint8_t* ecc
) {
	size_t size = major_count * minor_count;
	size_t major;
	for (major = 0; major < major_count; major++) {
		size_t index = (major >> 1) * major_mult + (major & 1);
		uint8_t ecc_a = 0;
		uint8_t ecc_b = 0;
		size_t minor;
		for (minor = 0; minor < minor_count; minor++) {
			uint8_t temp;
			if (index < 4) {
				temp = address[index];
			} else {
				temp = data[index - 4];
			}
			index += minor_inc;
			if (index >= size) {
				index -= size;
			}
			ecc_a ^= temp;
			ecc_b ^= temp;
			ecc_a = ecc_f_lut[ecc_a];
		}
		ecc_a = ecc_b_lut[ecc_f_lut[ecc_a] ^ ecc_b];
		if (
			ecc[major] != (ecc_a) ||
			ecc[major + major_count] != (ecc_a ^ ecc_b)
			) {
			return 0;
		}
	}
	return 1;
}

//
// Write ECC block (either P or Q)
//
static void ecc_writepq(
	const uint8_t* address,
	const uint8_t* data,
	size_t major_count,
	size_t minor_count,
	size_t major_mult,
	size_t minor_inc,
	uint8_t* ecc
) {
	size_t size = major_count * minor_count;
	size_t major;
	for (major = 0; major < major_count; major++) {
		size_t index = (major >> 1) * major_mult + (major & 1);
		uint8_t ecc_a = 0;
		uint8_t ecc_b = 0;
		size_t minor;
		for (minor = 0; minor < minor_count; minor++) {
			uint8_t temp;
			if (index < 4) {
				temp = address[index];
			} else {
				temp = data[index - 4];
			}
			index += minor_inc;
			if (index >= size) {
				index -= size;
			}
			ecc_a ^= temp;
			ecc_b ^= temp;
			ecc_a = ecc_f_lut[ecc_a];
		}
		ecc_a = ecc_b_lut[ecc_f_lut[ecc_a] ^ ecc_b];
		ecc[major] = (ecc_a);
		ecc[major + major_count] = (ecc_a ^ ecc_b);
	}
}

//
// Check ECC P and Q codes for a sector
// Returns true if the ECC data is an exact match
//
static int8_t ecc_checksector(
	const uint8_t *address,
	const uint8_t *data,
	const uint8_t *ecc
) {
	return
		ecc_checkpq(address, data, 86, 24, 2, 86, ecc) &&       // P
		ecc_checkpq(address, data, 52, 43, 86, 88, ecc + 0xAC); // Q
}

//
// Write ECC P and Q codes for a sector
//
static void ecc_writesector(
	const uint8_t *address,
	const uint8_t *data,
	uint8_t *ecc
) {
	ecc_writepq(address, data, 86, 24, 2, 86, ecc);         // P
	ecc_writepq(address, data, 52, 43, 86, 88, ecc + 0xAC); // Q
}

////////////////////////////////////////////////////////////////////////////////

static const uint8_t zeroaddress[4] = { 0, 0, 0, 0 };

////////////////////////////////////////////////////////////////////////////////
//
// Check if this is a sector we can compress
//
// Sector types:
//   0: Literal bytes (not a sector)
//   1: 2352 mode 1         predict sync, mode, reserved, edc, ecc
//   2: 2336 mode 2 form 1  predict redundant flags, edc, ecc
//   3: 2336 mode 2 form 2  predict redundant flags, edc
//
SectorType detect_sector(const uint8_t* sector, size_t size_available, TrackMode *trackMode) {
	if (size_available >= 2352) {
		if (sector[0x000] == 0x00 && sector[0x001] == 0xFF && sector[0x002] == 0xFF && sector[0x003] == 0xFF &&
			sector[0x004] == 0xFF && sector[0x005] == 0xFF && sector[0x006] == 0xFF && sector[0x007] == 0xFF &&
			sector[0x008] == 0xFF && sector[0x009] == 0xFF && sector[0x00A] == 0xFF && sector[0x00B] == 0x00) { // sync (12 bytes)
			if ((sector[0x00F] & 0x0f) == 0x00) { // mode (1 byte)
				if (trackMode) {
					*trackMode = TrackMode0;
				}
				int zeroCnt = 0;
				for (int i = 0x10; i < 0x930; i++) {
					if (sector[i] == 0) {
						zeroCnt++;
					}
				}
				if (zeroCnt == 0x920) {
					if (sector[0x00F] == 0x00) {
						return Mode0; // Mode 0
					}
					else if (sector[0x00F] & 0xE0 && (sector[0x00F] & 0x1C) == 0 && (sector[0x00F] & 0x03) == 0) {
						return Mode0WithBlockIndicators;
					}
					else {
						return InvalidMode0;
					}
				}
				else {
					return Mode0NotAllZero;
				}
			}
			else if ((sector[0x00F] & 0x0f) == 0x01) { // mode (1 byte)
				if (trackMode) {
					*trackMode = TrackMode1;
				}
				if (ecc_checksector(sector + 0xC, sector + 0x10, sector + 0x81C) &&
					edc_compute(0, sector, 0x810) == get32lsb(sector + 0x810)) {
					if (sector[0x814] == 0x00 && sector[0x815] == 0x00 && sector[0x816] == 0x00 && sector[0x817] == 0x00 &&
						sector[0x818] == 0x00 && sector[0x819] == 0x00 && sector[0x81A] == 0x00 && sector[0x81B] == 0x00) { // reserved (8 bytes)
						//
						// Might be Mode 1
						//
						if (sector[0x00F] == 0x01) {
							return Mode1; // Mode 1
						}
						else if (sector[0x00F] & 0xE0 && (sector[0x00F] & 0x1C) == 0 && (sector[0x00F] & 0x03) == 0x01) {
							return Mode1WithBlockIndicators;
						}
						else {
							return InvalidMode1;
						}
					}
					else {
						return Mode1ReservedNotZero; // Mode 1 but 0x814-81B isn't zero
					}
				}
				else {
					return Mode1BadEcc; // Mode 1 probably protect (safedisc etc)
				}
			}
			else if ((sector[0x0F] & 0x0f) == 0x02) { // mode (1 byte)
				if (trackMode) {
					*trackMode = TrackMode2;
				}
				//
				// Might be Mode 2, Form 1
				//
				if (ecc_checksector(zeroaddress, sector + 0x10, sector + 0x10 + 0x80C) &&
					edc_compute(0, sector + 0x10, 0x808) == get32lsb(sector + 0x10 + 0x808)) {
					if (sector[0x10] == sector[0x14] && sector[0x11] == sector[0x15] &&
						sector[0x12] == sector[0x16] && sector[0x13] == sector[0x17]) { // flags (4 bytes) versus redundant copy
						if (sector[0x00F] == 0x02) {
							return Mode2Form1; // Mode 2, Form 1
						}
						else if (sector[0x00F] & 0xE0 && (sector[0x00F] & 0x1C) == 0 && (sector[0x00F] & 0x03) == 0x02) {
							return Mode2WithBlockIndicators;
						}
						else {
							return InvalidMode2Form1;
						}
					}
					else {
						return Mode2Form1SubheaderNotSame;
					}
				}
				//
				// Might be Mode 2, Form 2
				//
				else if (edc_compute(0, sector + 0x10, 0x91C) == get32lsb(sector + 0x10 + 0x91C)) {
					if (sector[0x10] == sector[0x14] && sector[0x11] == sector[0x15] &&
						sector[0x12] == sector[0x16] && sector[0x13] == sector[0x17]) { // flags (4 bytes) versus redundant copy
						if (sector[0x00F] == 0x02) {
							return Mode2Form2; // Mode 2, Form 2
						}
						else if (sector[0x00F] & 0xE0 && (sector[0x00F] & 0x1C) == 0 && (sector[0x00F] & 0x03) == 0x02) {
							return Mode2WithBlockIndicators;
						}
						else {
							return InvalidMode2Form2;
						}
					}
					else {
						return Mode2Form2SubheaderNotSame;
					}
				}
				else {
					if (sector[0x10] == sector[0x14] && sector[0x11] == sector[0x15] &&
						sector[0x12] == sector[0x16] && sector[0x13] == sector[0x17]) { // flags (4 bytes) versus redundant copy
						if (sector[0x00F] == 0x02) {
							return Mode2; // Mode 2, No EDC (for PlayStation)
						}
						else if (sector[0x00F] & 0xE0 && (sector[0x00F] & 0x1C) == 0 && (sector[0x00F] & 0x03) == 0x02) {
							return Mode2WithBlockIndicators;
						}
						else {
							return InvalidMode2;
						}
					}
					else {
						return Mode2SubheaderNotSame;
					}
				}
			}
			else {
				if (trackMode) {
					*trackMode = TrackModeUnknown;
				}
				return UnknownMode;
			}
		}
		else if (sector[0x000] || sector[0x001] || sector[0x002] || sector[0x003] ||
			sector[0x004] || sector[0x005] || sector[0x006] || sector[0x007] ||
			sector[0x008] || sector[0x009] || sector[0x00A] || sector[0x00B] ||
			sector[0x00C] || sector[0x00D] || sector[0x00E] || sector[0x00F]) { // Fix for invalid scrambled sector in data track
			return NonZeroInvalidSync;
		}
		else {
			return ZeroSync;
		}
	}

	//
	// Nothing
	//
	return Nothing;
}

////////////////////////////////////////////////////////////////////////////////
//
// Reconstruct a sector based on type
//
bool reconstruct_sector(
	uint8_t* sector, // must point to a full 2352-byte sector
	SectorType type
) {
	//
	// Sync
	//
	sector[0x000] = 0x00;
	sector[0x001] = 0xFF;
	sector[0x002] = 0xFF;
	sector[0x003] = 0xFF;
	sector[0x004] = 0xFF;
	sector[0x005] = 0xFF;
	sector[0x006] = 0xFF;
	sector[0x007] = 0xFF;
	sector[0x008] = 0xFF;
	sector[0x009] = 0xFF;
	sector[0x00A] = 0xFF;
	sector[0x00B] = 0x00;

	if (type == Mode1) {
		//
		// Mode
		//
		sector[0x00F] = 0x01;
		//
		// Reserved
		//
		sector[0x814] = 0x00;
		sector[0x815] = 0x00;
		sector[0x816] = 0x00;
		sector[0x817] = 0x00;
		sector[0x818] = 0x00;
		sector[0x819] = 0x00;
		sector[0x81A] = 0x00;
		sector[0x81B] = 0x00;
	}
	else if (type == Mode2Form1 || type == Mode2Form2) {
		//
		// Mode
		//
		sector[0x00F] = 0x02;
		//
		// Flags
		//
		sector[0x010] = sector[0x014];
		sector[0x011] = sector[0x015];
		sector[0x012] = sector[0x016];
		sector[0x013] = sector[0x017];
	}
	else {
		return false;
	}

	//
	// Compute EDC
	//
	if (type == Mode1) {
		put32lsb(sector + 0x810, edc_compute(0, sector, 0x810));
	}
	else if (type == Mode2Form1) {
		put32lsb(sector + 0x818, edc_compute(0, sector + 0x10, 0x808));
	}
	else if (type == Mode2Form2) {
		put32lsb(sector + 0x92C, edc_compute(0, sector + 0x10, 0x91C));
	}
	else {
		return false;
	}

	//
	// Compute ECC
	//
	if (type == Mode1) {
		ecc_writesector(sector + 0xC, sector + 0x10, sector + 0x81C);
	} 
	else if (type == Mode2Form1) {
		ecc_writesector(zeroaddress, sector + 0x10, sector + 0x81C);
	} 
	else {
		return false;
	}

	//
	// Done
	//
	return true;
}
