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

#include "StringUtils.hpp"
#include "FileUtils.hpp"
#include "Enum.h"
#include "_external/ecm.h"

static DWORD check_fix_mode_s_startLBA = 0;
static DWORD check_fix_mode_s_endLBA = 0;
static BYTE write_mode_s_Minute = 0;
static BYTE write_mode_s_Second = 0;
static BYTE write_mode_s_Frame = 0;
static SectorType write_mode_s_Mode = SectorTypeNothing;
static DWORD write_mode_s_MaxRoop = 0;

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

LONG GetFileSize(
	LONG lOffset,
	FILE *fp
	)
{
	LONG lFileSize = 0;
	if (fp != NULL) {
		fseek(fp, 0, SEEK_END);
		lFileSize = ftell(fp);
		fseek(fp, lOffset, SEEK_SET);
	}
	return lFileSize;
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
		"\tcheckex <CueFile>\n"
		"\t\tValidate user data of 2048 byte per sector (alternate)\n"
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
	CHAR* endptr = NULL;
	int ret = TRUE;

	if (argc == 3 && (!strcmp(argv[1], "check"))) {
		*pExecType = check;
	} else if (argc == 3 && (!strcmp(argv[1], "checkex"))) {
		*pExecType = checkex;
	} else if (argc == 3 && (!strcmp(argv[1], "fix"))) {
		*pExecType = fix;
	} else if (argc == 5 && (!strcmp(argv[1], "fix"))) {
		check_fix_mode_s_startLBA = strtoul(argv[3], &endptr, 10);

		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);

			return FALSE;
		}

		check_fix_mode_s_endLBA = strtoul(argv[4], &endptr, 10);

		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);

			return FALSE;
		}

		*pExecType = fix;
	} else if (argc == 8 && (!strcmp(argv[1], "write"))) {
		write_mode_s_Minute = (BYTE)strtoul(argv[3], &endptr, 10);

		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);

			return FALSE;
		}

		write_mode_s_Second = (BYTE)strtoul(argv[4], &endptr, 10);

		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);

			return FALSE;
		}

		write_mode_s_Frame = (BYTE)strtoul(argv[5], &endptr, 10);

		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);

			return FALSE;
		}

		write_mode_s_Mode = (SectorType)strtoul(argv[6], &endptr, 10);
		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);

			return FALSE;
		}

		write_mode_s_MaxRoop = strtoul(argv[7], &endptr, 10);

		if (*endptr) {
			OutputErrorString("Bad arg: %s Please integer\n", endptr);

			return FALSE;
		}

		*pExecType = write;
	} else {
		OutputErrorString("argc: %d\n", argc);

		ret = FALSE;
	}

	return ret;
}

int fixSectorsFromArray(FILE *fp, DWORD *errorSectors, INT sectorCount, DWORD startLBA, DWORD endLBA) {
	int fixedCount = 0;

	for (int i = 0; i < sectorCount; i++) {
		if (startLBA <= errorSectors[i] && errorSectors[i] <= endLBA) {
//			fseek(fp, (LONG)((errorSectors[i] - startLBA) * CD_RAW_SECTOR_SIZE + 12), SEEK_SET);
			fseek(fp, (LONG)(errorSectors[i] * CD_RAW_SECTOR_SIZE + 12), SEEK_SET);

			BYTE m, s, f;
			LBAtoMSF((INT)errorSectors[i] + 150, &m, &s, &f);

			fputc(DecToBcd(m), fp);
			fputc(DecToBcd(s), fp);
			fputc(DecToBcd(f), fp);

			fseek(fp, 1, SEEK_CUR);

			for (int j = 0; j < 2336; j++) {
				fputc(0x55, fp);
			}

			fixedCount++;
		}
	}

	return fixedCount;
}

int handleCheckOrFix(LPCSTR filePath, EXEC_TYPE execType, DWORD startLBA, DWORD endLBA, TrackMode targetTrackMode, LPCSTR errorLogFilePath) {
	FILE* fp = NULL;

	if (execType == check || execType == checkex) {
		fp = fopen(filePath, "rb");
		if (!fp) {
			OutputLastErrorNumAndString(__FUNCTION__, __LINE__);

			return EXIT_FAILURE;
		}
	} else if (execType == fix) {
		fp = fopen(filePath, "rb+");
		if (!fp) {
			OutputLastErrorNumAndString(__FUNCTION__, __LINE__);

			return EXIT_FAILURE;
		}
	} else {
		return EXIT_FAILURE;
	}

	FILE *fpError = fopen(errorLogFilePath, "w");
	if (!fpError) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);

		fclose(fp);

		return EXIT_FAILURE;
	}

	uint8_t buf[CD_RAW_SECTOR_SIZE] = { 0 };
	INT roopSize = GetFileSize(0, fp) / CD_RAW_SECTOR_SIZE;

	DWORD* errorSectorNum = (DWORD*)calloc((size_t)roopSize, sizeof(DWORD));
	if (!errorSectorNum) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);

		return EXIT_FAILURE;
	}

	DWORD* noMatchLBANum = (DWORD*)calloc((size_t)roopSize, sizeof(DWORD));
	if (!noMatchLBANum) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);

		return EXIT_FAILURE;
	}

	DWORD* reservedSectorNum = (DWORD*)calloc((size_t)roopSize, sizeof(DWORD));
	if (!reservedSectorNum) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);

		return EXIT_FAILURE;
	}

	DWORD* noEDCSectorNum = (DWORD*)calloc((size_t)roopSize, sizeof(DWORD));
	if (!noEDCSectorNum) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);

		return EXIT_FAILURE;
	}

	DWORD* flagSectorNum = (DWORD*)calloc((size_t)roopSize, sizeof(DWORD));
	if (!flagSectorNum) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);

		return EXIT_FAILURE;
	}

	DWORD* nonZeroInvalidSyncSectorNum = (DWORD*)calloc((size_t)roopSize, sizeof(DWORD));
	if (!nonZeroInvalidSyncSectorNum) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);

		return EXIT_FAILURE;
	}

	DWORD* zeroSyncSectorNum = (DWORD*)calloc((size_t)roopSize, sizeof(DWORD));
	if (!zeroSyncSectorNum) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);

		return EXIT_FAILURE;
	}

	memset(zeroSyncSectorNum, 0xFF, roopSize * sizeof(DWORD));

	DWORD* unknownModeSectorNum = (DWORD*)calloc((size_t)roopSize, sizeof(DWORD));
	if (!unknownModeSectorNum) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);

		return EXIT_FAILURE;
	}

	INT cnt_SectorFilled55 = 0;
	INT cnt_SectorTypeMode1BadEcc = 0;
	INT cnt_SectorTypeMode1ReservedNotZero = 0;
	INT cnt_SectorTypeMode2FlagsNotSame = 0;
	INT cnt_SectorTypeMode2 = 0;
	INT cnt_SectorTypeNonZeroInvalidSync = 0; // For VOB
	INT cnt_SectorTypeUnknownMode = 0; // For SecuROM
	
	if (startLBA == 0 && endLBA == 0) {
		endLBA = (DWORD)roopSize;
	}

	BOOL skipTrackModeCheck = targetTrackMode == TrackModeUnknown;
	TrackMode trackMode = targetTrackMode;
	
//	for (DWORD j = 0; j < (DWORD)roopSize; j++) {
//		DWORD i = j + startLBA;
	for (DWORD i = 0; i < (DWORD)roopSize; i++) {
		fread(buf, sizeof(uint8_t), sizeof(buf), fp);

		if (!IsScrambledDataHeader(buf)) {
			if (IsErrorSector(buf)) {
				fprintf(fpError, "LBA[%06ld, %#07lx], 2336 bytes have been already replaced at 0x55\n", i, i);
				errorSectorNum[cnt_SectorFilled55++] = i;

				continue;
			}

			TrackMode trackModeLocal = TrackModeUnknown;
			SectorType sectorType = detect_sector(buf, CD_RAW_SECTOR_SIZE, &trackModeLocal);

			if (trackMode == TrackModeUnknown && trackModeLocal != TrackModeUnknown) {
				trackMode = trackModeLocal;
				skipTrackModeCheck = FALSE;
			}

			if (sectorType == SectorTypeMode1 || sectorType == SectorTypeMode1BadEcc || sectorType == SectorTypeMode1ReservedNotZero) {
				fprintf(fpError, "LBA[%06ld, %#07lx], mode 1", i, i);

				if (sectorType == SectorTypeMode1) {
					fprintf(fpError, "\n");
				} else if (sectorType == SectorTypeMode1BadEcc) {
					fprintf(fpError, " User data vs. ecc/edc doesn't match\n");

					noMatchLBANum[cnt_SectorTypeMode1BadEcc++] = i;
				} else if (sectorType == SectorTypeMode1ReservedNotZero) {
					if (buf[0x814] == 0x55 && buf[0x815] == 0x55 && buf[0x816] == 0x55 && buf[0x817] == 0x55 &&	buf[0x818] == 0x55 && buf[0x819] == 0x55 && buf[0x81a] == 0x55 && buf[0x81b] == 0x55) {
						fprintf(fpError, " This sector have been already replaced at 0x55 but it's incompletely\n");

						noMatchLBANum[cnt_SectorTypeMode1BadEcc++] = i;
					} else {
						fprintf(fpError,
							" Reserved byte doesn't zero."
							" [0x814]:%#04x, [0x815]:%#04x, [0x816]:%#04x, [0x817]:%#04x,"
							" [0x818]:%#04x, [0x819]:%#04x, [0x81a]:%#04x, [0x81b]:%#04x\n"
							, buf[0x814], buf[0x815], buf[0x816], buf[0x817]
							, buf[0x818], buf[0x819], buf[0x81a], buf[0x81b]);

						reservedSectorNum[cnt_SectorTypeMode1ReservedNotZero++] = i;
					}
				}
			} else if (sectorType == SectorTypeMode2Form1 || sectorType == SectorTypeMode2Form2 || sectorType == SectorTypeMode2 || sectorType == SectorTypeMode2FlagsNotSame) {
				BOOL bNoEdc = FALSE;

				fprintf(fpError, "LBA[%06ld, %#07lx], mode 2 ", i, i);

				if (sectorType == SectorTypeMode2Form1) {
					fprintf(fpError, "form 1, ");
				} else if (sectorType == SectorTypeMode2Form2) {
					fprintf(fpError, "form 2, ");
				} else if (sectorType == SectorTypeMode2) {
					fprintf(fpError, "no edc, ");

					noEDCSectorNum[cnt_SectorTypeMode2++] = i;
					bNoEdc = TRUE;
				} else if (sectorType == SectorTypeMode2FlagsNotSame) {
					fprintf(fpError,
						" Flags arent't the same."
						" [0x10]:%#04x, [0x11]:%#04x, [0x12]:%#04x, [0x13]:%#04x,"
						" [0x14]:%#04x, [0x15]:%#04x, [0x16]:%#04x, [0x17]:%#04x, "
						, buf[0x10], buf[0x11], buf[0x12], buf[0x13]
						, buf[0x14], buf[0x15], buf[0x16], buf[0x17]);

					flagSectorNum[cnt_SectorTypeMode2FlagsNotSame++] = i;
				}

				fprintf(fpError, "SubHeader[1](FileNum[%02x]), [2](ChannelNum[%02x]), ", buf[16], buf[17]);
				fprintf(fpError, "[3](SubMode[%02x]), ", buf[18]);

				if (buf[18] & 0x80) {
					fprintf(fpError, "End-of-File, ");
				}

				if (buf[18] & 0x40) {
					fprintf(fpError, "Real-time block, ");
				}

				if (buf[18] & 0x20) {
					fprintf(fpError, "Form 2, ");
				} else {
					fprintf(fpError, "Form 1, ");

					if (bNoEdc) {
						noMatchLBANum[cnt_SectorTypeMode1BadEcc++] = i;
					}
				}

				if (buf[18] & 0x10) {
					fprintf(fpError, "Trigger Block, ");
				}

				BOOL bAudio = FALSE;

				if (buf[18] & 0x08) {
					fprintf(fpError, "Data Block, ");
				} else if (buf[18] & 0x04) {
					fprintf(fpError, "Audio Block, ");
					bAudio = TRUE;
				} else if (buf[18] & 0x02) {
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
					} else {
						fprintf(fpError, "4 bits/sample, 8 sound sectors, ");
					}

					if (buf[19] & 0x08) {
						fprintf(fpError, "Reserved, ");
					}

					if (buf[19] & 0x04) {
						fprintf(fpError, "18.9kHz, ");
					} else {
						fprintf(fpError, "37.8kHz, ");
					}

					if (buf[19] & 0x02) {
						fprintf(fpError, "Reserved, ");
					}

					if (buf[19] & 0x01) {
						fprintf(fpError, "Stereo, ");
					} else {
						fprintf(fpError, "Mono, ");
					}
				} else {
					if (buf[19]) {
						fprintf(fpError, "Reserved, ");
					}
				}

				fprintf(fpError, "\n");
			} else if (sectorType == SectorTypeNonZeroInvalidSync) {
				fprintf(fpError, "LBA[%06ld, %#07lx], This sector has invalid sync\n", i, i);

				nonZeroInvalidSyncSectorNum[cnt_SectorTypeNonZeroInvalidSync++] = i;
			} else if (sectorType == SectorTypeUnknownMode) {
				fprintf(fpError, "LBA[%06ld, %#07lx], This sector has unknown mode field\n", i, i);

				unknownModeSectorNum[cnt_SectorTypeUnknownMode++] = i;
			} else if (sectorType == SectorTypeZeroSync) {
//				zeroSyncSectorNum[j] = i;
				zeroSyncSectorNum[i] = i;
				fprintf(fpError, "LBA[%06ld, %#07lx], This sector has zero sync\n", i, i);
			} else if (!skipTrackModeCheck && trackMode != trackModeLocal) {
				fprintf(fpError, "LBA[%06ld, %#07lx], This sector has changed track mode: %d %d\n", i, i, trackMode, trackModeLocal);

				unknownModeSectorNum[cnt_SectorTypeUnknownMode++] = i;
			}
		} else {
			fprintf(fpError, "LBA[%06ld, %#07lx], This sector is audio or scrambled data or corrupt data\n", i, i);
		}

//		OutputString("\rChecking data sectors (LBA) %6lu/%6lu", i, startLBA + roopSize - 1);
		OutputString("\rChecking data sectors (LBA) %6lu/%6d", i, roopSize - 1);
	}

	INT nonZeroSyncIndexStart = 0;
	INT nonZeroSyncIndexEnd = roopSize - 1;

	for (INT i = 0; i < roopSize; ++i, ++nonZeroSyncIndexStart) {
		if (zeroSyncSectorNum[i] == 0xFFFFFFFF)
			break;
	}

	for (INT i = roopSize - 1; i >= 0; --i, --nonZeroSyncIndexEnd) {
		if (zeroSyncSectorNum[i] == 0xFFFFFFFF)
			break;
	}

	INT cnt_SectorTypeZeroSync = 0;

	if (nonZeroSyncIndexStart != roopSize - 1) { // Empty track
		assert(nonZeroSyncIndexStart <= nonZeroSyncIndexEnd);
		
		for (int i = nonZeroSyncIndexStart; i <= nonZeroSyncIndexEnd; ++i) {
			if (zeroSyncSectorNum[i] != 0xFFFFFFFF) {
				cnt_SectorTypeZeroSync++;
			}
		}
	}

	OutputString("\n");

	if (cnt_SectorFilled55) {
		OutputString("Number of sector(s) where 2336 byte is all 0x55: %d\n", cnt_SectorFilled55);
		fprintf(fpError, "[WARNING] Number of sector(s) where 2336 byte is all 0x55: %d\n", cnt_SectorFilled55);
		fprintf(fpError, "\tSector: ");

		for (int i = 0; i < cnt_SectorFilled55; i++) {
			fprintf(fpError, "%ld, ", errorSectorNum[i]);
		}

		fprintf(fpError, "\n");
	}

	if (cnt_SectorTypeMode1BadEcc) {
		OutputString("Number of sector(s) where user data doesn't match the expected ECC/EDC: %d\n", cnt_SectorTypeMode1BadEcc);
		fprintf(fpError, "[ERROR] Number of sector(s) where user data doesn't match the expected ECC/EDC: %d\n", cnt_SectorTypeMode1BadEcc);
		fprintf(fpError, "\tSector: ");

		for (int i = 0; i < cnt_SectorTypeMode1BadEcc; i++) {
			fprintf(fpError, "%ld, ", noMatchLBANum[i]);
		}

		fprintf(fpError, "\n");
	}

	if (cnt_SectorTypeMode1ReservedNotZero) {
		OutputString("Number of sector(s) where reserved byte doesn't zero: %d\n", cnt_SectorTypeMode1ReservedNotZero);
		fprintf(fpError, "[WARNING] Number of sector(s) where reserved byte doesn't zero: %d\n", cnt_SectorTypeMode1ReservedNotZero);
		fprintf(fpError, "\tSector: ");

		for (int i = 0; i < cnt_SectorTypeMode1ReservedNotZero; i++) {
			fprintf(fpError, "%ld, ", reservedSectorNum[i]);
		}

		fprintf(fpError, "\n");
	}

	if (cnt_SectorTypeMode2) {
		OutputString("Number of sector(s) where while user data does match the expected ECC/EDC there is no EDC: %d\n", cnt_SectorTypeMode2);
		fprintf(fpError, "[WARNING] Number of sector(s) where while user data does match the expected ECC/EDC there is no EDC: %d\n", cnt_SectorTypeMode2);
		fprintf(fpError, "\tSector: ");

		for (int i = 0; i < cnt_SectorTypeMode2; i++) {
			fprintf(fpError, "%ld, ", noEDCSectorNum[i]);
		}

		fprintf(fpError, "\n");
	}

	if (cnt_SectorTypeMode2FlagsNotSame) {
		OutputString("Number of sector(s) where flag byte doesn't zero: %d\n", cnt_SectorTypeMode2FlagsNotSame);
		fprintf(fpError, "[WARNING] Number of sector(s) where flag byte doesn't zero: %d\n", cnt_SectorTypeMode2FlagsNotSame);
		fprintf(fpError, "\tSector: ");

		for (int i = 0; i < cnt_SectorTypeMode2FlagsNotSame; i++) {
			fprintf(fpError, "%ld, ", flagSectorNum[i]);
		}

		fprintf(fpError, "\n");
	}

	if (cnt_SectorTypeNonZeroInvalidSync) {
		OutputString("Number of sector(s) where sync is invalid: %d\n", cnt_SectorTypeNonZeroInvalidSync);
		fprintf(fpError, "[ERROR] Number of sector(s) where sync is invalid: %d\n", cnt_SectorTypeNonZeroInvalidSync);
		fprintf(fpError, "\tSector: ");

		for (int i = 0; i < cnt_SectorTypeNonZeroInvalidSync; i++) {
			fprintf(fpError, "%ld, ", nonZeroInvalidSyncSectorNum[i]);
		}

		fprintf(fpError, "\n");
	}

	if (cnt_SectorTypeZeroSync) {
		OutputString("Number of sector(s) where sync is zero: %d\n", cnt_SectorTypeZeroSync);
		fprintf(fpError, "[ERROR] Number of sector(s) where sync is zero: %d\n", cnt_SectorTypeZeroSync);
		fprintf(fpError, "\tSector: ");

		for (int i = nonZeroSyncIndexStart; i <= nonZeroSyncIndexEnd; ++i) {
			if (zeroSyncSectorNum[i] != 0xFFFFFFFF) {
				fprintf(fpError, "%ld, ", zeroSyncSectorNum[i]);
			}
		}

		fprintf(fpError, "\n");
	}

	if (cnt_SectorTypeUnknownMode) {
		OutputString("Number of sector(s) where mode is unknown: %d\n", cnt_SectorTypeUnknownMode);
		fprintf(fpError, "[ERROR] Number of sector(s) where mode is unknown: %d\n", cnt_SectorTypeUnknownMode);
		fprintf(fpError, "\tSector: ");

		for (int i = 0; i < cnt_SectorTypeUnknownMode; i++) {
			fprintf(fpError, "%ld, ", unknownModeSectorNum[i]);
		}

		fprintf(fpError, "\n");
	}

	if (cnt_SectorFilled55 == 0 && cnt_SectorTypeMode1BadEcc == 0 && cnt_SectorTypeMode1ReservedNotZero == 0 && cnt_SectorTypeMode2 == 0 && cnt_SectorTypeMode2FlagsNotSame == 0 &&
		cnt_SectorTypeNonZeroInvalidSync == 0 && cnt_SectorTypeZeroSync == 0 && cnt_SectorTypeUnknownMode == 0) {
		OutputString("User data vs. ecc/edc match all\n");
		fprintf(fpError, "[SUCCESS] User data vs. ecc/edc match all\n");
	}

	if (execType == fix) {
		if (cnt_SectorTypeMode1BadEcc || cnt_SectorTypeNonZeroInvalidSync) {
			int fixedCnt = 0;

			if (cnt_SectorTypeMode1BadEcc) {
				fixedCnt += fixSectorsFromArray(fp, noMatchLBANum, cnt_SectorTypeMode1BadEcc, startLBA, endLBA);
			}
			
			if (cnt_SectorTypeNonZeroInvalidSync) {
				fixedCnt += fixSectorsFromArray(fp, nonZeroInvalidSyncSectorNum, cnt_SectorTypeNonZeroInvalidSync, startLBA, endLBA);
			}

			OutputString("%d unmatch sector is replaced at 0x55 except header\n", fixedCnt);
			fprintf(fpError, "%d unmatch sector is replaced at 0x55 except header\n", fixedCnt);
		}
	}
	
	free(unknownModeSectorNum);
	free(nonZeroInvalidSyncSectorNum);
	free(zeroSyncSectorNum);
	free(errorSectorNum);
	free(noMatchLBANum);
	free(reservedSectorNum);
	free(noEDCSectorNum);
	free(flagSectorNum);
	fclose(fp);

	if (!GetFileSize(0, fpError)) {
		if (remove(errorLogFilePath)) {
			OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		}
	}
	fclose(fpError);

	return EXIT_SUCCESS;
}

int handleCheckEx(LPCSTR filePath) {
	std::vector<std::string> cueLines;

	if (!FileUtils::readFileLines(filePath, cueLines)) {
		OutputString("Cannot read cue lines\n");

		return EXIT_FAILURE;
	}

	struct TrackInfo {
		std::string trackPath;
		ULONG lsnStart;
		ULONG lsnEnd;
		ULONG trackNo;
		TrackMode trackMode;
	};

	std::vector<TrackInfo> tracks;
	std::string cueDirPath = StringUtils::getDirectoryFromPath(filePath);

	TrackInfo currentTrack = {};
	ULONG currentTrackNo = 0;
	ULONG currentLsn = 0;

	for (auto & line : cueLines) {
		StringUtils::trim(line);

		if (StringUtils::startsWith(line, "FILE") && StringUtils::endsWith(line, "BINARY")) {
			if (currentTrack.trackPath.size()) {
				tracks.push_back(currentTrack);
			} else {
				currentTrack = {};
			}

			std::string stripped = line.substr(sizeof("FILE"), line.size() - sizeof("FILE") - sizeof("BINARY"));
			StringUtils::trim(stripped);

			currentTrack.trackPath = cueDirPath + stripped.substr(1, stripped.size() - 2);
			currentTrack.trackNo = currentTrackNo++;

			ULONG fileSize = 0;

			if (!FileUtils::getFileSize(currentTrack.trackPath.c_str(), fileSize) || (fileSize % 2352)) {
				OutputString("Cannot get track size or track size mismatch: %s\n", currentTrack.trackPath.c_str());

				return EXIT_FAILURE;
			}

			currentTrack.lsnStart = currentLsn;
			currentTrack.lsnEnd = currentLsn + fileSize / 2352;

			currentLsn = currentTrack.lsnEnd;
		} else if(StringUtils::startsWith(line, "TRACK")) {
			if (StringUtils::endsWith(line, "AUDIO")) {
				currentTrack.trackMode = TrackModeAudio;
			} else if (StringUtils::endsWith(line, "MODE1/2352")) {
				currentTrack.trackMode = TrackMode1;
			} else if (StringUtils::endsWith(line, "MODE2/2352") || StringUtils::endsWith(line, "MODE2/2336")) {
				currentTrack.trackMode = TrackMode2;
			} else {
				OutputString("Invalid track mode: %s\n", line.c_str());

				return EXIT_FAILURE;
			}
		}
	}

	tracks.push_back(currentTrack);

	int retVal = EXIT_FAILURE;

	for (auto & track : tracks) {
		if (track.trackMode != TrackModeAudio) {
			char suffixBuffer[24];
			sprintf(suffixBuffer, "Track_%lu.txt", track.trackNo + 1);

			std::string errorLogFilePath = std::string(filePath) + "_EdcEcc_" + suffixBuffer;

			if ((retVal = handleCheckOrFix(track.trackPath.c_str(), check, track.lsnStart, track.lsnEnd, track.trackMode, errorLogFilePath.c_str())) != EXIT_SUCCESS) {
				OutputString("Cannot check track: %s\n", track.trackPath.c_str());

				break;
			}
		}
	}

	return retVal;
}

int handleWrite(LPCSTR filePath) {
	FILE* fp = fopen(filePath, "ab");
	if (!fp) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);

		return EXIT_FAILURE;
	}

	for (DWORD i = 0; i < write_mode_s_MaxRoop; i++) {
		uint8_t buf[CD_RAW_SECTOR_SIZE] = { 0 };
		buf[0xc] = (uint8_t)(write_mode_s_Minute + 6 * (write_mode_s_Minute / 10));
		buf[0xd] = (uint8_t)(write_mode_s_Second + 6 * (write_mode_s_Second / 10));
		buf[0xe] = (uint8_t)(write_mode_s_Frame + 6 * (write_mode_s_Frame / 10));

		if (!reconstruct_sector(buf, write_mode_s_Mode)) {
			OutputString("Invalid mode specified: %d\n", write_mode_s_Mode);
		}

		fwrite(buf, sizeof(uint8_t), sizeof(buf), fp);

		write_mode_s_Frame++;

		if (write_mode_s_Frame == 75) {
			write_mode_s_Frame = 0;
			write_mode_s_Second++;

			if (write_mode_s_Second == 60) {
				write_mode_s_Second = 0;
				write_mode_s_Minute++;
			}
		}
	}

	fclose(fp);

	return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
	EXEC_TYPE execType;

	if (!checkArg(argc, argv, &execType)) {
		printUsage();

		return EXIT_FAILURE;
	}

	eccedc_init(); // Initialize the ECC/EDC tables

	int retVal = EXIT_FAILURE;

	fprintf(stdout, "FILE: %s\n", argv[2]);

	if (execType == check || execType == fix) {
		std::string errorLogFilePath = std::string(argv[2]) + "_EdcEcc.txt";

		retVal = handleCheckOrFix(argv[2], execType, check_fix_mode_s_startLBA, check_fix_mode_s_endLBA, TrackModeUnknown, errorLogFilePath.c_str());
	} else if (execType == checkex) {
		retVal = handleCheckEx(argv[2]);
	} else if (execType == write) {
		retVal = handleWrite(argv[2]);
	}

	return retVal;
}
