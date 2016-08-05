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

// #include "common.h"
// #include "banner.h"
#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable:4668 4710 4711 4820)
#include <stdio.h>
#include <windows.h>

typedef   signed __int8   int8_t;
typedef unsigned __int8  uint8_t;
typedef   signed __int16  int16_t;
typedef unsigned __int16 uint16_t;
typedef   signed __int32  int32_t;
typedef unsigned __int32 uint32_t;

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

static void eccedc_init(void) {
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
			}
			else {
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
			}
			else {
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
		ecc_checkpq(address, data, 86, 24, 2, 86, ecc) &&      // P
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
	ecc_writepq(address, data, 86, 24, 2, 86, ecc);        // P
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
static int8_t detect_sector(const uint8_t* sector, size_t size_available) {
	if (
		size_available >= 2352 &&
		sector[0x000] == 0x00 && // sync (12 bytes)
		sector[0x001] == 0xFF &&
		sector[0x002] == 0xFF &&
		sector[0x003] == 0xFF &&
		sector[0x004] == 0xFF &&
		sector[0x005] == 0xFF &&
		sector[0x006] == 0xFF &&
		sector[0x007] == 0xFF &&
		sector[0x008] == 0xFF &&
		sector[0x009] == 0xFF &&
		sector[0x00A] == 0xFF &&
		sector[0x00B] == 0x00
		) {
		if (
			sector[0x00F] == 0x01 // mode (1 byte)
			) {
			if(
				sector[0x814] == 0x00 && // reserved (8 bytes)
				sector[0x815] == 0x00 &&
				sector[0x816] == 0x00 &&
				sector[0x817] == 0x00 &&
				sector[0x818] == 0x00 &&
				sector[0x819] == 0x00 &&
				sector[0x81A] == 0x00 &&
				sector[0x81B] == 0x00
				) {
				//
				// Might be Mode 1
				//
				if (
					ecc_checksector(
					sector + 0xC,
					sector + 0x10,
					sector + 0x81C
					) &&
					edc_compute(0, sector, 0x810) == get32lsb(sector + 0x810)
					) {
					return 1; // Mode 1
				}
				else {
					return -1; // Mode 1 probably protect(safedisc etc)
				}
			}
			else {
				if (
					ecc_checksector(
					sector + 0xC,
					sector + 0x10,
					sector + 0x81C
					) &&
					edc_compute(0, sector, 0x810) == get32lsb(sector + 0x810)
					) {
					return 1; // Mode 1
				}
				return -2; // Mode 1 but 0x814-81B isn't zero
			}
		}
		else if (
			sector[0x0F] == 0x02 // mode (1 byte)
			) {
			if(
				sector[0x10] == sector[0x14] && // flags (4 bytes)
				sector[0x11] == sector[0x15] && //   versus redundant copy
				sector[0x12] == sector[0x16] &&
				sector[0x13] == sector[0x17]
				) {
				//
				// Might be Mode 2, Form 1 or 2
				//
				if (
					ecc_checksector(
					zeroaddress,
					sector + 0x10,
					sector + 0x10 + 0x80C
					) &&
					edc_compute(0, sector + 0x10, 0x808) == get32lsb(sector + 0x10 + 0x808)
					) {
					return 2; // Mode 2, Form 1
				}
				//
				// Might be Mode 2, Form 2
				//
				if (
					edc_compute(0, sector + 0x10, 0x91C) == get32lsb(sector + 0x10 + 0x91C)
					) {
					return 3; // Mode 2, Form 2
				}
				else {
					return 4; // Mode 2, No EDC (for PlayStation)
				}
			}
			else {
				return -3; // flags isn't same
			}
		}
	}
	//
	// Nothing
	//
	return 0;
}

////////////////////////////////////////////////////////////////////////////////
//
// Reconstruct a sector based on type
//
static void reconstruct_sector(
	uint8_t* sector, // must point to a full 2352-byte sector
	int8_t type
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

	switch (type) {
	case 1:
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
		break;
	case 2:
	case 3:
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
		break;
	}

	//
	// Compute EDC
	//
	switch (type) {
	case 1:
		put32lsb(sector + 0x810, edc_compute(0, sector, 0x810));
		break;
	case 2:
		put32lsb(sector + 0x818, edc_compute(0, sector + 0x10, 0x808));
		break;
	case 3:
		put32lsb(sector + 0x92C, edc_compute(0, sector + 0x10, 0x91C));
		break;
	}

	//
	// Compute ECC
	//
	switch (type) {
	case 1:
		ecc_writesector(sector + 0xC, sector + 0x10, sector + 0x81C);
		break;
	case 2:
		ecc_writesector(zeroaddress, sector + 0x10, sector + 0x81C);
		break;
	}

	//
	// Done
	//
}

// above original source is ecm.c in cmdpack-1.03-src.tar.gz
//  Copyright (C) 1996-2011 Neill Corlett
//  http://www.neillcorlett.com/cmdpack/
////////////////////////////////////////////////////////////////////////////////

typedef enum _EXEC_TYPE {
	check,
	fix,
	write
} EXEC_TYPE, *PEXEC_TYPE;

static DWORD s_startLBA = 0;
static DWORD s_endLBA = 0;
static BYTE s_Minute = 0;
static BYTE s_Second = 0;
static BYTE s_Frame = 0;
static int8_t s_Mode = 0;
static DWORD s_MaxRoop = 0;

#define CD_RAW_SECTOR_SIZE	(2352)

#define OutputString(str, ...) \
{ \
	printf(str, __VA_ARGS__); \
}
#ifdef _DEBUG
char logBuffer[2048];
#define OutputErrorString(str, ...) \
{ \
	_snprintf(logBuffer, 2048, str, __VA_ARGS__); \
	logBuffer[2047] = 0; \
	OutputDebugString(logBuffer); \
}
#else
#define OutputErrorString(str, ...) \
{ \
	fprintf(stderr, str, __VA_ARGS__); \
}
#endif

VOID OutputLastErrorNumAndString(
	LPCSTR pszFuncName,
	LONG lLineNum
	)
{
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);

	OutputErrorString("[F:%s][L:%ld] GetLastError: %lu, %s\n",
		pszFuncName, lLineNum, GetLastError(), (LPCSTR)lpMsgBuf);
	LocalFree(lpMsgBuf);
}

BOOL IsValidDataHeader(
	LPBYTE lpSrc
	)
{
	BYTE aHeader[] = {
		0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
	};
	BOOL bRet = TRUE;
	for (INT c = 0; c < sizeof(aHeader); c++) {
		if (lpSrc[c] != aHeader[c]) {
			bRet = FALSE;
			break;
		}
	}
	return bRet;
}

BOOL IsScrambledDataHeader(
	LPBYTE lpSrc
	)
{
	BOOL bRet = FALSE;
	if (IsValidDataHeader(lpSrc)) {
		if ((lpSrc[0xf] & 0x60)) {
			bRet = TRUE;
		}
	}
	return bRet;
}

BOOL IsErrorSector(
	LPBYTE lpSrc
	)
{
	BOOL bRet = FALSE;
	INT cnt = 0;
	if (IsValidDataHeader(lpSrc)) {
		for (INT i = 16; i < CD_RAW_SECTOR_SIZE; i++) {
			if (lpSrc[i] == 0x55) {
				cnt++;
			}
		}
		if (cnt == 2336) {
			bRet = TRUE;
		}
	}
	return bRet;
}

DWORD GetFileSize(
	LONG lOffset,
	FILE *fp
	)
{
	DWORD dwFileSize = 0;
	if (fp != NULL) {
		fseek(fp, 0, SEEK_END);
		dwFileSize = (DWORD)ftell(fp);
		fseek(fp, lOffset, SEEK_SET);
	}
	return dwFileSize;
}

VOID LBAtoMSF(
	INT nLBA,
	LPBYTE byMinute,
	LPBYTE bySecond,
	LPBYTE byFrame
	)
{
	*byFrame = (BYTE)(nLBA % 75);
	nLBA /= 75;
	*bySecond = (BYTE)(nLBA % 60);
	nLBA /= 60;
	*byMinute = (BYTE)(nLBA);
}

BYTE DecToBcd(
	BYTE bySrc
	)
{
	INT m = 0;
	INT n = bySrc;
	m += n / 10;
	n -= m * 10;
	return (BYTE)(m << 4 | n);
}

void printUsage(void)
{
	OutputString(
		"Usage\n"
		"\tcheck <InFileName>\n"
		"\t\tValidate user data of 2048 byte per sector\n"
		"\tfix <InOutFileName>\n"
		"\t\tReplace data of 2336 byte to '0x55' except header\n"
		"\tfix <InOutFileName> <startLBA> <endLBA>\n"
		"\t\tReplace data of 2336 byte to '0x55' except header from <startLBA> to <endLBA>\n"
		"\twrite <OutFileName> <Minute> <Second> <Frame> <Mode> <CreateSectorNum>\n"
		"\t\tCreate a 2352 byte per sector with sync, addr, mode, ecc, edc. (User data is all zero)\n"
		"\t\tMode\t1: mode 1, 2: mode 2 form 1, 3: mode 2 form 2\n"
		);
}

int checkArg(int argc, char* argv[], PEXEC_TYPE pExecType)
{
	int ret = TRUE;
	CHAR* endptr = NULL;
	if (argc == 3 && (!strcmp(argv[1], "check"))) {
		*pExecType = check;
	}
	else if (argc == 3 && (!strcmp(argv[1], "fix"))) {
		*pExecType = fix;
	}
	else if (argc == 5 && (!strcmp(argv[1], "fix"))) {
		s_startLBA = strtoul(argv[3], &endptr, 10);
		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);
			return FALSE;
		}
		s_endLBA = strtoul(argv[4], &endptr, 10);
		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);
			return FALSE;
		}
		*pExecType = fix;
	}
	else if (argc == 8 && (!strcmp(argv[1], "write"))) {
		s_Minute = (BYTE)strtoul(argv[3], &endptr, 10);
		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);
			return FALSE;
		}
		s_Second = (BYTE)strtoul(argv[4], &endptr, 10);
		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);
			return FALSE;
		}
		s_Frame = (BYTE)strtoul(argv[5], &endptr, 10);
		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);
			return FALSE;
		}
		s_Mode = (int8_t)strtoul(argv[6], &endptr, 10);
		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);
			return FALSE;
		}
		s_MaxRoop = strtoul(argv[7], &endptr, 10);
		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);
			return FALSE;
		}
		*pExecType = write;
	}
	else {
		OutputErrorString("argc: %d\n", argc);
		ret = FALSE;
	}
	return ret;
}

int main(int argc, char** argv) {
	EXEC_TYPE execType;
	if (!checkArg(argc, argv, &execType)) {
		printUsage();
		return EXIT_FAILURE;
	}
	//
	// Initialize the ECC/EDC tables
	//
	eccedc_init();
	if (execType == check || execType == fix) {
		FILE* fp = NULL;
		if (execType == check) {
			fp = fopen(argv[2], "rb");
			if (!fp) {
				OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
				return EXIT_FAILURE;
			}
		}
		else if (execType == fix) {
			fp = fopen(argv[2], "rb+");
			if (!fp) {
				OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
				return EXIT_FAILURE;
			}
		}
		CHAR path[_MAX_PATH] = { 0 };
		CHAR drive[_MAX_DRIVE] = { 0 };
		CHAR dir[_MAX_DIR] = { 0 };
		CHAR fname[_MAX_FNAME] = { 0 };
		_splitpath(argv[2], drive, dir, fname, NULL);
		strncat(fname, "_EdcEcc", 7);
		_makepath(path, drive, dir, fname, ".txt");
		FILE* fpError = fopen(path, "w");
		if (!fpError) {
			OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
			return EXIT_FAILURE;
		}
		uint8_t buf[CD_RAW_SECTOR_SIZE] = { 0 };
		DWORD roopSize = GetFileSize(0, fp) / CD_RAW_SECTOR_SIZE;
		DWORD* errorSectorNum = (DWORD*)calloc(roopSize, sizeof(DWORD));
		if (!errorSectorNum) {
			OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
			return EXIT_FAILURE;
		}
		DWORD* noMatchLBANum = (DWORD*)calloc(roopSize, sizeof(DWORD));
		if (!noMatchLBANum) {
			OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
			return EXIT_FAILURE;
		}
		DWORD* reservedSectorNum = (DWORD*)calloc(roopSize, sizeof(DWORD));
		if (!reservedSectorNum) {
			OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
			return EXIT_FAILURE;
		}
		DWORD* noEDCSectorNum = (DWORD*)calloc(roopSize, sizeof(DWORD));
		if (!noEDCSectorNum) {
			OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
			return EXIT_FAILURE;
		}
		DWORD* flagSectorNum = (DWORD*)calloc(roopSize, sizeof(DWORD));
		if (!flagSectorNum) {
			OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
			return EXIT_FAILURE;
		}
		INT cntError = -1;
		INT cntMinus1 = -1;
		INT cntMinus2 = -1;
		INT cnt4 = -1;
		INT cntMinus3 = -1;
		if (s_startLBA == 0 && s_endLBA == 0) {
			s_endLBA = roopSize;
		}
		for (DWORD i = 0; i < roopSize; i++) {
			fread(buf, sizeof(uint8_t), sizeof(buf), fp);
			if (!IsScrambledDataHeader(buf)) {
				if (IsErrorSector(buf)) {
					fprintf(fpError,
						"LBA[%06ld, %#07lx], 2336 bytes have been already replaced at 0x55\n", i, i);
					errorSectorNum[cntError++] = i;
					continue;
				}
				int8_t ret = detect_sector(buf, CD_RAW_SECTOR_SIZE);
				if (i == 0 && ret != 0) {
					cntError = 0;
					cntMinus1 = 0;
					cntMinus2 = 0;
					cnt4 = 0;
					cntMinus3 = 0;
				}
				if (ret == 1 || ret == -1 || ret == -2) {
					fprintf(fpError, "LBA[%06ld, %#07lx], mode 1", i, i);
					if (ret == 1) {
						fprintf(fpError, "\n");
					}
					else if (ret == -1) {
						fprintf(fpError, " User data vs. ecc/edc doesn't match\n");
						noMatchLBANum[cntMinus1++] = i;
					}
					else if (ret == -2) {
						if (buf[0x814] == 0x55 && buf[0x815] == 0x55 && buf[0x816] == 0x55 && buf[0x817] == 0x55 &&
							buf[0x818] == 0x55 && buf[0x819] == 0x55 && buf[0x81a] == 0x55 && buf[0x81b] == 0x55) {
							fprintf(fpError,
								" This sector have been already replaced at 0x55 but it's incompletely\n");
							noMatchLBANum[cntMinus1++] = i;
						}
						else {
							fprintf(fpError,
								" Reserved byte doesn't zero."
								" [0x814]:%#04x, [0x815]:%#04x, [0x816]:%#04x, [0x817]:%#04x,"
								" [0x818]:%#04x, [0x819]:%#04x, [0x81a]:%#04x, [0x81b]:%#04x\n"
								, buf[0x814], buf[0x815], buf[0x816], buf[0x817]
								, buf[0x818], buf[0x819], buf[0x81a], buf[0x81b]);
							reservedSectorNum[cntMinus2++] = i;
						}
					}
				}
				else if (ret == 2 || ret == 3 || ret == 4 || ret == -3) {
					BOOL bNoEdc = FALSE;
					fprintf(fpError, "LBA[%06ld, %#07lx], mode 2 ", i, i);
					if (ret == 2) {
						fprintf(fpError, "form 1, ");
					}
					else if (ret == 3) {
						fprintf(fpError, "form 2, ");
					}
					else if (ret == 4) {
						fprintf(fpError, "no edc, ");
						noEDCSectorNum[cnt4++] = i;
						bNoEdc = TRUE;
					}
					else if (ret == -3) {
						fprintf(fpError, 
							"flags doesn't same."
							" [0x10]:%#04x, [0x11]:%#04x, [0x12]:%#04x, [0x13]:%#04x,"
							" [0x14]:%#04x, [0x15]:%#04x, [0x16]:%#04x, [0x17]:%#04x, "
							, buf[0x10], buf[0x11], buf[0x12], buf[0x13]
							, buf[0x14], buf[0x15], buf[0x16], buf[0x17]);
						flagSectorNum[cntMinus3++] = i;
					}
					fprintf(fpError,
						"SubHeader[1](FileNum[%02x]), [2](ChannelNum[%02x]), ",	buf[16], buf[17]);
					fprintf(fpError, "[3](SubMode[%02x]), ", buf[18]);
					if (buf[18] & 0x80) {
						fprintf(fpError, "End-of-File, ");
					}
					if (buf[18] & 0x40) {
						fprintf(fpError, "Real-time block, ");
					}
					if (buf[18] & 0x20) {
						fprintf(fpError, "Form 2, ");
					}
					else {
						fprintf(fpError, "Form 1, ");
						if (bNoEdc) {
							noMatchLBANum[cntMinus1++] = i;
						}
					}
					if (buf[18] & 0x10) {
						fprintf(fpError, "Trigger Block, ");
					}
					BOOL bAudio = FALSE;
					if (buf[18] & 0x08) {
						fprintf(fpError, "Data Block, ");
					}
					else if (buf[18] & 0x04) {
						fprintf(fpError, "Audio Block, ");
						bAudio = TRUE;
					}
					else if (buf[18] & 0x02) {
						fprintf(fpError, "Video Block, ");
					}
					if (buf[18] & 0x01) {
						fprintf(fpError, "End-of-Record, ");
					}

					fprintf(fpError, "[4](CodingInfo[%02x])", buf[19]);
					if (bAudio) {
						if (buf[19] & 0x80) {
							fprintf(fpError, "Reserved, ");
						}
						if (buf[19] & 0x40) {
							fprintf(fpError, "Emphasis, ");
						}
						if (buf[19] & 0x20) {
							fprintf(fpError, "Reserved, ");
						}
						if (buf[19] & 0x10) {
							fprintf(fpError, "8 bits/sample, 4 sound sectors, ");
						}
						else {
							fprintf(fpError, "4 bits/sample, 8 sound sectors, ");
						}
						if (buf[19] & 0x08) {
							fprintf(fpError, "Reserved, ");
						}
						if (buf[19] & 0x04) {
							fprintf(fpError, "18.9kHz, ");
						}
						else {
							fprintf(fpError, "37.8kHz, ");
						}
						if (buf[19] & 0x02) {
							fprintf(fpError, "Reserved, ");
						}
						if (buf[19] & 0x01) {
							fprintf(fpError, "Stereo, ");
						}
						else {
							fprintf(fpError, "Mono, ");
						}
					}
					else {
						if (buf[19]) {
							fprintf(fpError, "Reserved, ");
						}
					}
					fprintf(fpError, "\n");
				}
			}
			else {
				fprintf(fpError,
					"LBA[%06ld, %#07lx], This sector is audio or scrambled data or corrupt data\n", i, i);
			}
			OutputString("\rChecking data sectors (LBA) %6lu/%6lu", i, roopSize - 1);
		}
		OutputString("\n");
		if (cntError > 0) {
			OutputString("Number of sector(s) where 2336 byte is all 0x55: %d\n", cntError);
			fprintf(fpError, "Number of sector(s) where 2336 byte is all 0x55: %d\n", cntError);
			fprintf(fpError, "\tSector: ");
			for (int i = 0; i < cntError; i++) {
				fprintf(fpError, "%ld, ", errorSectorNum[i]);
			}
			fprintf(fpError, "\n");
		}
		if (cntMinus1 > 0) {
			OutputString("Number of sector(s) where user data doesn't match the expected ECC/EDC: %d\n", cntMinus1);
			fprintf(fpError, "Number of sector(s) where user data doesn't match the expected ECC/EDC: %d\n", cntMinus1);
			fprintf(fpError, "\tSector: ");
			for (int i = 0; i < cntMinus1; i++) {
				fprintf(fpError, "%ld, ", noMatchLBANum[i]);
			}
			fprintf(fpError, "\n");
		}
		if (cntMinus2 > 0) {
			OutputString("Number of sector(s) where reserved byte doesn't zero: %d\n", cntMinus2);
			fprintf(fpError, "Number of sector(s) where reserved byte doesn't zero: %d\n", cntMinus2);
			fprintf(fpError, "\tSector: ");
			for (int i = 0; i < cntMinus2; i++) {
				fprintf(fpError, "%ld, ", reservedSectorNum[i]);
			}
			fprintf(fpError, "\n");
		}
		if (cnt4 > 0) {
			OutputString("Number of sector(s) where while user data does match the expected ECC/EDC there is no EDC: %d\n", cnt4);
			fprintf(fpError, "Number of sector(s) where while user data does match the expected ECC/EDC there is no EDC: %d\n", cnt4);
			fprintf(fpError, "\tSector: ");
			for (int i = 0; i < cnt4; i++) {
				fprintf(fpError, "%ld, ", noEDCSectorNum[i]);
			}
			fprintf(fpError, "\n");
		}
		if (cntMinus3 > 0) {
			OutputString("Number of sector(s) where flag byte doesn't zero: %d\n", cntMinus3);
			fprintf(fpError, "Number of sector(s) where flag byte doesn't zero: %d\n", cntMinus3);
			fprintf(fpError, "\tSector: ");
			for (int i = 0; i < cntMinus3; i++) {
				fprintf(fpError, "%ld, ", flagSectorNum[i]);
			}
			fprintf(fpError, "\n");
		}
		if (cntError == 0 && cntMinus1 == 0 && cntMinus2 == 0 && cnt4 == 0 && cntMinus3 == 0) {
			OutputString("User data vs. ecc/edc match all\n");
			fprintf(fpError, "User data vs. ecc/edc match all\n");
		}
		if (execType == fix) {
			if (cntMinus1 > 0) {
				int fixedCnt = 0;
				for (int i = 0; i < cntMinus1; i++) {
					if (s_startLBA <= noMatchLBANum[i] && noMatchLBANum[i] <= s_endLBA) {
						fseek(fp, (LONG)(noMatchLBANum[i] * CD_RAW_SECTOR_SIZE + 12), SEEK_SET);
						BYTE m, s, f;
						LBAtoMSF((INT)noMatchLBANum[i] + 150, &m, &s, &f);
						fputc(DecToBcd(m), fp);
						fputc(DecToBcd(s), fp);
						fputc(DecToBcd(f), fp);
						fseek(fp, 1, SEEK_CUR);
						for (int j = 0; j < 2336; j++) {
							fputc(0x55, fp);
						}
						fixedCnt++;
					}
				}
				OutputString("%d unmatch sector is replaced at 0x55 except header\n", fixedCnt);
				fprintf(fpError, "%d unmatch sector is replaced at 0x55 except header\n", fixedCnt);
			}
		}
		DWORD fsize = GetFileSize(0, fpError);
		fclose(fp);
		fclose(fpError);
		free(errorSectorNum);
		free(noMatchLBANum);
		free(reservedSectorNum);
		free(noEDCSectorNum);
		free(flagSectorNum);
		if (!fsize) {
			if (remove(path)) {
				OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
			}
		}
	}
	else if (execType == write) {
		FILE* fp = fopen(argv[2], "ab");
		if (!fp) {
			OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
			return EXIT_FAILURE;
		}
		for (DWORD i = 0; i < s_MaxRoop; i++) {
			uint8_t buf[CD_RAW_SECTOR_SIZE] = { 0 };
			buf[0xc] = (uint8_t)(s_Minute + 6 * (s_Minute / 10));
			buf[0xd] = (uint8_t)(s_Second + 6 * (s_Second / 10));
			buf[0xe] = (uint8_t)(s_Frame + 6 * (s_Frame / 10));
			reconstruct_sector(buf, s_Mode);
			fwrite(buf, sizeof(uint8_t), sizeof(buf), fp);
			s_Frame++;
			if (s_Frame == 75) {
				s_Frame = 0;
				s_Second++;
				if (s_Second == 60) {
					s_Second = 0;
					s_Minute++;
				}
			}
		}
		fclose(fp);
	}

	return EXIT_SUCCESS;
}
