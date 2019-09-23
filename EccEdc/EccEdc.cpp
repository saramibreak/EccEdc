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
#ifdef _WIN32
#include "StringUtils.hpp"
#include "FileUtils.hpp"
#endif
#include "Enum.h"
#include "_external/ecm.h"

static UINT check_fix_mode_s_startLBA = 0;
static UINT check_fix_mode_s_endLBA = 0;
static BYTE write_mode_s_Minute = 0;
static BYTE write_mode_s_Second = 0;
static BYTE write_mode_s_Frame = 0;
static SectorType write_mode_s_Mode = Nothing;
static DWORD write_mode_s_MaxRoop = 0;
static FILE *fpLog;

typedef struct _ERROR_STRUCT {
	INT cnt_BadMsf = 0;
	INT cnt_SectorFilled55 = 0;
	INT cnt_Mode0NotAllZero = 0;
	INT cnt_Mode1BadEcc = 0;
	INT cnt_Mode1ReservedNotZero = 0;
	INT cnt_Mode2Form1SubheaderNotSame = 0;
	INT cnt_Mode2Form2SubheaderNotSame = 0;
	INT cnt_Mode2SubheaderNotSame = 0;
	INT cnt_Mode2 = 0;
	INT cnt_InvalidMode = 0;
	INT cnt_NonZeroInvalidSync = 0; // For VOB
	INT cnt_UnknownMode = 0; // For SecuROM
	INT cnt_ZeroSync = 0;
	INT cnt_ZeroSyncPregap = 0;
	DWORD* badMsfNum;
	DWORD* notAllZeroNum;
	DWORD* errorNum;
	DWORD* noMatchLBANum;
	DWORD* reservedNum;
	DWORD* noEDCNum;
	DWORD* mode2Form1Num;
	DWORD* mode2Form2Num;
	DWORD* mode2Num;
	DWORD* invalidModeNum;
	DWORD* nonZeroInvalidSyncNum;
	DWORD* zeroSyncNum;
	DWORD* zeroSyncPregapNum;
	DWORD* unknownModeNum;
} ERROR_STRUCT, *PERROR_STRUCT;

#define CD_RAW_SECTOR_SIZE	(2352)

#define OutputString(str, ...)		printf(str, ##__VA_ARGS__);
#define OutputErrorString(str, ...)	fprintf(stderr, str, ##__VA_ARGS__);
#define OutputFile(str, ...)		fprintf(fpLog, str, ##__VA_ARGS__);
#define OutputFileWithLba(str, ...)	fprintf(fpLog, "LBA[%06d, %#07x], " str, ##__VA_ARGS__);
#define OutputFileWithLbaMsf(str, ...)	fprintf(fpLog, "LBA[%06d, %#07x], MSF[%02x:%02x:%02x], " str, ##__VA_ARGS__);
#define OutputLog(type, str, ...) \
{ \
	INT t = type; \
	if ((t & standardOut) == standardOut) { \
		OutputString(str, ##__VA_ARGS__); \
	} \
	if ((t & standardError) == standardError) { \
		OutputErrorString(str, ##__VA_ARGS__); \
	} \
	if ((t & file) == file) { \
		OutputFile(str, ##__VA_ARGS__); \
	} \
}

#define FreeAndNull(lpBuf) \
{ \
	if (lpBuf) { \
		free(lpBuf); \
		lpBuf = NULL; \
	} \
}

VOID OutputLastErrorNumAndString(
	LPCSTR pszFuncName,
	LONG lLineNum
) {
#ifdef _WIN32
	LPVOID lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&lpMsgBuf, 0, NULL);

	OutputErrorString("[F:%s][L:%lu] GetLastError: %lu, %s\n",
		pszFuncName, lLineNum, GetLastError(), (LPCTSTR)lpMsgBuf);

	LocalFree(lpMsgBuf);
#else
	OutputErrorString("[F:%s][L:%lu] GetLastError: %lu, %s\n",
		pszFuncName, lLineNum, GetLastError(), strerror(GetLastError()));
#endif
}

BOOL IsValidDataHeader(
	LPBYTE lpSrc
) {
	BYTE aHeader[] = {
		0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00
	};
	BOOL bRet = TRUE;
	for (size_t c = 0; c < sizeof(aHeader); c++) {
		if (lpSrc[c] != aHeader[c]) {
			bRet = FALSE;
			break;
		}
	}
	return bRet;
}

BOOL IsScrambledDataHeader(
	LPBYTE lpSrc
) {
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
) {
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
) {
	LONG lFileSize = 0;
	if (fp != NULL) {
		fseek(fp, 0, SEEK_END);
		lFileSize = ftell(fp);
		fseek(fp, lOffset, SEEK_SET);
	}
	return lFileSize;
}

INT MSFtoLBA(
	BYTE byMinute,
	BYTE bySecond,
	BYTE byFrame
) {
	return (byMinute * 60 + bySecond) * 75 + byFrame;
}

VOID LBAtoMSF(
	INT nLBA,
	LPBYTE byMinute,
	LPBYTE bySecond,
	LPBYTE byFrame
) {
	*byFrame = (BYTE)(nLBA % 75);
	nLBA /= 75;
	*bySecond = (BYTE)(nLBA % 60);
	nLBA /= 60;
	*byMinute = (BYTE)(nLBA);
}

BYTE BcdToDec(
	BYTE bySrc
) {
	return (BYTE)(((bySrc >> 4) & 0x0f) * 10 + (bySrc & 0x0f));
}

BYTE DecToBcd(
	BYTE bySrc
) {
	INT m = 0;
	INT n = bySrc;
	m += n / 10;
	n -= m * 10;
	return (BYTE)(m << 4 | n);
}

INT fixSectorsFromArray(
	EXEC_TYPE execType,
	FILE *fp,
	DWORD *errorSectors,
	INT sectorCount,
	DWORD startLBA,
	DWORD endLBA
) {
#ifndef _WIN32
	UNREFERENCED_PARAMETER(execType);
#endif
	INT fixedCount = 0;

	for (INT i = 0; i < sectorCount; i++) {
		if (startLBA <= errorSectors[i] && errorSectors[i] <= endLBA) {
#ifdef _WIN32
			if (execType == checkex) {
				fseek(fp, (LONG)((errorSectors[i] - startLBA) * CD_RAW_SECTOR_SIZE + 12), SEEK_SET);
			}
			else {
#endif
				fseek(fp, (LONG)(errorSectors[i] * CD_RAW_SECTOR_SIZE + 12), SEEK_SET);
#ifdef _WIN32
			}
#endif
			BYTE m, s, f;
			LBAtoMSF((INT)errorSectors[i] + 150, &m, &s, &f);

			fputc(DecToBcd(m), fp);
			fputc(DecToBcd(s), fp);
			fputc(DecToBcd(f), fp);

			fseek(fp, 1, SEEK_CUR);

			for (INT j = 0; j < 2336; j++) {
				fputc(0x55, fp);
			}

			fixedCount++;
		}
	}

	return fixedCount;
}

INT initCountNum(
	PERROR_STRUCT pErrStruct,
	size_t stAllocSize
) {
	if (NULL == ((*pErrStruct).badMsfNum = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	if (NULL == ((*pErrStruct).notAllZeroNum = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	if (NULL == ((*pErrStruct).errorNum = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	if (NULL == ((*pErrStruct).noMatchLBANum = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	if (NULL == ((*pErrStruct).reservedNum = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	if (NULL == ((*pErrStruct).noEDCNum = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	if (NULL == ((*pErrStruct).mode2Form1Num = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	if (NULL == ((*pErrStruct).mode2Form2Num = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	if (NULL == ((*pErrStruct).mode2Num = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	if (NULL == ((*pErrStruct).invalidModeNum = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	if (NULL == ((*pErrStruct).nonZeroInvalidSyncNum = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	if (NULL == ((*pErrStruct).zeroSyncNum = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	memset((*pErrStruct).zeroSyncNum, 0xFF, stAllocSize * sizeof(DWORD));

	if (NULL == ((*pErrStruct).zeroSyncPregapNum = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	if (NULL == ((*pErrStruct).unknownModeNum = (DWORD*)calloc(stAllocSize, sizeof(DWORD)))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		return FALSE;
	}
	return TRUE;
}

VOID terminateCountNum(
	PERROR_STRUCT pErrStruct
) {
	FreeAndNull((*pErrStruct).badMsfNum);
	FreeAndNull((*pErrStruct).notAllZeroNum);
	FreeAndNull((*pErrStruct).errorNum);
	FreeAndNull((*pErrStruct).noMatchLBANum);
	FreeAndNull((*pErrStruct).reservedNum);
	FreeAndNull((*pErrStruct).noEDCNum);
	FreeAndNull((*pErrStruct).mode2Form1Num);
	FreeAndNull((*pErrStruct).mode2Form2Num);
	FreeAndNull((*pErrStruct).mode2Num);
	FreeAndNull((*pErrStruct).invalidModeNum);
	FreeAndNull((*pErrStruct).nonZeroInvalidSyncNum);
	FreeAndNull((*pErrStruct).zeroSyncNum);
	FreeAndNull((*pErrStruct).zeroSyncPregapNum);
	FreeAndNull((*pErrStruct).unknownModeNum);
}

INT handleCheckDetail(
	PERROR_STRUCT pErrStruct,
	EXEC_TYPE execType,
	LPBYTE buf,
	BOOL skipTrackModeCheck,
	TrackMode trackMode,
	UINT roopCnt,
	UINT roopCnt2,
	BOOL bSub,
	LPBYTE subBuf
) {
#ifndef _WIN32
	UNREFERENCED_PARAMETER(execType);
	UNREFERENCED_PARAMETER(roopCnt2);
#endif
	if (IsErrorSector(buf)) {
		OutputFileWithLbaMsf("2336 bytes have been already replaced at 0x55\n", roopCnt, roopCnt, buf[12], buf[13], buf[14]);
		pErrStruct->errorNum[pErrStruct->cnt_SectorFilled55++] = roopCnt;
		return TRUE;
	}

	TrackMode trackModeLocal = TrackModeUnknown;
	SectorType sectorType = detect_sector(buf, CD_RAW_SECTOR_SIZE, &trackModeLocal);

	if (trackMode == TrackModeUnknown && trackModeLocal != TrackModeUnknown) {
		trackMode = trackModeLocal;
		skipTrackModeCheck = FALSE;
	}

	BYTE byCtl = (BYTE)((subBuf[12] >> 4) & 0x0f);
	BYTE byIdx = subBuf[14];

	if (sectorType == Mode0 || sectorType == InvalidMode0 || sectorType == Mode0NotAllZero) {
		OutputFileWithLbaMsf("mode 0", roopCnt, roopCnt, buf[12], buf[13], buf[14]);
		if (sectorType == Mode0) {
			OutputFile("\n");
		}
		else if (sectorType == InvalidMode0) {
			OutputFile(" Invalid mode: [%02x]\n", buf[15]);
			pErrStruct->invalidModeNum[pErrStruct->cnt_InvalidMode++] = roopCnt;
		}
		else {
			OutputFile(" Not all user data zero\n");
			pErrStruct->notAllZeroNum[pErrStruct->cnt_Mode0NotAllZero++] = roopCnt;
		}
	}
	else if (sectorType == Mode1 || sectorType == InvalidMode1 ||
		sectorType == Mode1BadEcc || sectorType == Mode1ReservedNotZero) {
		OutputFileWithLbaMsf("mode 1", roopCnt, roopCnt, buf[12], buf[13], buf[14]);
		if (sectorType == Mode1) {
			OutputFile("\n");
		}
		else if (sectorType == InvalidMode1) {
			OutputFile(" Invalid mode: [%02x]\n", buf[15]);
			pErrStruct->invalidModeNum[pErrStruct->cnt_InvalidMode++] = roopCnt;
		}
		else if (sectorType == Mode1BadEcc) {
			OutputFile(" User data vs. ecc/edc doesn't match\n");

			pErrStruct->noMatchLBANum[pErrStruct->cnt_Mode1BadEcc++] = roopCnt;
		}
		else if (sectorType == Mode1ReservedNotZero) {
			if (buf[0x814] == 0x55 && buf[0x815] == 0x55 && buf[0x816] == 0x55 && buf[0x817] == 0x55 &&
				buf[0x818] == 0x55 && buf[0x819] == 0x55 && buf[0x81a] == 0x55 && buf[0x81b] == 0x55) {
				OutputFile(" This sector have been already replaced at 0x55 but it's incompletely\n");

				pErrStruct->noMatchLBANum[pErrStruct->cnt_Mode1BadEcc++] = roopCnt;
			}
			else {
				OutputFile(
					" Reserved doesn't zero."
					" [0x814]:%#04x, [0x815]:%#04x, [0x816]:%#04x, [0x817]:%#04x,"
					" [0x818]:%#04x, [0x819]:%#04x, [0x81a]:%#04x, [0x81b]:%#04x\n"
					, buf[0x814], buf[0x815], buf[0x816], buf[0x817]
					, buf[0x818], buf[0x819], buf[0x81a], buf[0x81b]);

				pErrStruct->reservedNum[pErrStruct->cnt_Mode1ReservedNotZero++] = roopCnt;
			}
		}
	}
	else if (sectorType == Mode2Form1 || sectorType == InvalidMode2Form1 ||
		sectorType == Mode2Form2 || sectorType == InvalidMode2Form2 ||
		sectorType == Mode2 || sectorType == InvalidMode2 ||
		sectorType == Mode2Form1SubheaderNotSame ||
		sectorType == Mode2Form2SubheaderNotSame || sectorType == Mode2SubheaderNotSame) {
		BOOL bNoEdc = FALSE;

		OutputFileWithLbaMsf("mode 2 ", roopCnt, roopCnt, buf[12], buf[13], buf[14]);
		if (sectorType == Mode2Form1) {
			OutputFile("form 1, ");
		}
		else if (sectorType == InvalidMode2Form1) {
			OutputFile(" Invalid mode: [%02x]\n", buf[15]);
			pErrStruct->invalidModeNum[pErrStruct->cnt_InvalidMode++] = roopCnt;
		}
		else if (sectorType == Mode2Form2) {
			OutputFile("form 2, ");
		}
		else if (sectorType == InvalidMode2Form2) {
			OutputFile(" Invalid mode: [%02x]\n", buf[15]);
			pErrStruct->invalidModeNum[pErrStruct->cnt_InvalidMode++] = roopCnt;
		}
		else if (sectorType == Mode2) {
			OutputFile("no edc, ");
			bNoEdc = TRUE;
		}
		else if (sectorType == InvalidMode2) {
			OutputFile(" Invalid mode: [%02x]\n", buf[15]);
			pErrStruct->invalidModeNum[pErrStruct->cnt_InvalidMode++] = roopCnt;
		}
		else if (sectorType == Mode2Form1SubheaderNotSame ||
			sectorType == Mode2Form2SubheaderNotSame ||
			sectorType == Mode2SubheaderNotSame) {
			if (sectorType == Mode2Form1SubheaderNotSame) {
				OutputFile("form 1, ");
				pErrStruct->mode2Form1Num[pErrStruct->cnt_Mode2Form1SubheaderNotSame++] = roopCnt;
			}
			else if (sectorType == Mode2Form2SubheaderNotSame) {
				OutputFile("form 2, ");
				pErrStruct->mode2Form2Num[pErrStruct->cnt_Mode2Form2SubheaderNotSame++] = roopCnt;
			}
			else if (sectorType == Mode2SubheaderNotSame) {
				OutputFile("no edc, ");
				pErrStruct->mode2Num[pErrStruct->cnt_Mode2SubheaderNotSame++] = roopCnt;
			}
			OutputFile("Subheader isn't same."
				" [0x10]:%#04x, [0x11]:%#04x, [0x12]:%#04x, [0x13]:%#04x,"
				" [0x14]:%#04x, [0x15]:%#04x, [0x16]:%#04x, [0x17]:%#04x, "
				, buf[0x10], buf[0x11], buf[0x12], buf[0x13], buf[0x14], buf[0x15], buf[0x16], buf[0x17]);
		}

		OutputFile("SubHeader[1](IsInterleaved[%02x]), [2](ChannelNum[%02x]), [3](SubMode[%02x]), ", buf[16], buf[17], buf[18]);
		if (buf[18] & 0x80) {
			OutputFile("IsEof, ");
		}

		if (buf[18] & 0x40) {
			OutputFile("Real-time, ");
		}

		if (buf[18] & 0x20) {
			OutputFile("Form 2, ");
			if (bNoEdc) {
				pErrStruct->noEDCNum[pErrStruct->cnt_Mode2++] = roopCnt;
			}
		}
		else {
			OutputFile("Form 1, ");
			if (bNoEdc) {
				pErrStruct->noMatchLBANum[pErrStruct->cnt_Mode1BadEcc++] = roopCnt;
			}
		}

		if (buf[18] & 0x10) {
			OutputFile("Trigger, ");
		}

		BOOL bAudio = FALSE;

		if (buf[18] & 0x08) {
			OutputFile("Data, ");
		}
		else if (buf[18] & 0x04) {
			OutputFile("Audio, ");
			bAudio = TRUE;
		}
		else if (buf[18] & 0x02) {
			OutputFile("Video, ");
		}

		if (buf[18] & 0x01) {
			OutputFile("End audio, ");
		}

		OutputFile("[4](CodingInfo[%02x])", buf[19]);

		if (bAudio) {
			if (buf[19] & 0x80) {
				OutputFile("Reserved, ");
			}

			if (buf[19] & 0x40) {
				OutputFile("Emphasis, ");
			}

			if (buf[19] & 0x20) {
				OutputFile("bits/sample, ");
			}

			if (buf[19] & 0x10) {
				OutputFile("8 bits/sample, 4 sound sectors, ");
			}
			else {
				OutputFile("4 bits/sample, 8 sound sectors, ");
			}

			if (buf[19] & 0x08) {
				OutputFile("sample rate, ");
			}

			if (buf[19] & 0x04) {
				OutputFile("18.9kHz playback, ");
			}
			else {
				OutputFile("37.8kHz playback, ");
			}

			if (buf[19] & 0x02) {
				OutputFile("Stereo, ");
			}

			if (buf[19] & 0x01) {
				OutputFile("Stereo, ");
			}
			else {
				OutputFile("Mono, ");
			}
		}
		else {
			if (buf[19]) {
				OutputFile("Reserved, ");
			}
		}
		OutputFile("\n");
	}
	else if (sectorType == UnknownMode) {
		OutputFileWithLbaMsf("unknown mode: %02x\n", roopCnt, roopCnt, buf[12], buf[13], buf[14], buf[15]);
		pErrStruct->unknownModeNum[pErrStruct->cnt_UnknownMode++] = roopCnt;
	}
	else if (!skipTrackModeCheck && trackMode != trackModeLocal) {
		OutputFileWithLbaMsf("changed track mode: %d %d\n", roopCnt, roopCnt, buf[12], buf[13], buf[14], trackMode, trackModeLocal);
		pErrStruct->unknownModeNum[pErrStruct->cnt_UnknownMode++] = roopCnt;
	}
	else if (sectorType == NonZeroInvalidSync) {
		if (bSub) {
			OutputFileWithLbaMsf("invalid sync\n", roopCnt, roopCnt, buf[12], buf[13], buf[14]);
		}
		else {
			OutputFileWithLbaMsf("audio or invalid sync\n", roopCnt, roopCnt, buf[12], buf[13], buf[14]);
		}
		pErrStruct->nonZeroInvalidSyncNum[pErrStruct->cnt_NonZeroInvalidSync++] = roopCnt;
	}
	else if (sectorType == ZeroSync) {
		if (bSub) {
			if ((byCtl == 0 || byCtl == 2) && byIdx == 0) {
				OutputFileWithLbaMsf("zero sync (pregap)\n", roopCnt, roopCnt, buf[12], buf[13], buf[14]);
			}
			else {
				OutputFileWithLbaMsf("zero sync\n", roopCnt, roopCnt, buf[12], buf[13], buf[14]);
			}
		}
		else {
			OutputFileWithLbaMsf("audio or zero sync\n", roopCnt, roopCnt, buf[12], buf[13], buf[14]);
		}
#ifdef _WIN32
		if (execType == checkex) {
			pErrStruct->zeroSyncNum[roopCnt2] = roopCnt;
		}
		else {
#endif
			if (bSub && (byCtl == 0 || byCtl == 2) && byIdx == 0) {
				pErrStruct->zeroSyncPregapNum[pErrStruct->cnt_ZeroSyncPregap++] = roopCnt;
			}
			else {
				pErrStruct->zeroSyncNum[pErrStruct->cnt_ZeroSync++] = roopCnt;
			}
#ifdef _WIN32
		}
#endif
	}
	return TRUE;
}

INT handleCheckOrFix(
	LPCSTR filePath,
	EXEC_TYPE execType,
	UINT startLBA,
	UINT endLBA,
	TrackMode targetTrackMode,
	LPCSTR logFilePath
) {
	FILE* fp = NULL;

	if (execType == check || execType == checkex) {
		if (NULL == (fp = fopen(filePath, "rb"))) {
			OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
			return EXIT_FAILURE;
		}
	}
	else if (execType == fix) {
		if (NULL == (fp = fopen(filePath, "rb+"))) {
			OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
			return EXIT_FAILURE;
		}
	}
	else {
		return EXIT_FAILURE;
	}
	fpLog = fopen(logFilePath, "w");
	if (!fpLog) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		fclose(fp);
		return EXIT_FAILURE;
	}
	CHAR path[_MAX_PATH] = { 0 };
	CHAR drive[_MAX_DRIVE] = { 0 };
	CHAR dir[_MAX_DIR] = { 0 };
	CHAR fname[_MAX_FNAME] = { 0 };
	_splitpath(filePath, drive, dir, fname, NULL);
	_makepath(path, drive, dir, fname, ".sub");

	FILE* fpSub = NULL;
	if (NULL == (fpSub = fopen(path, "rb"))) {
		OutputLastErrorNumAndString(__FUNCTION__, __LINE__);
		OutputErrorString("%s\n", path);
		OutputErrorString("If sub file exists, this app can check the data sector precisely\n");
	}

	BYTE buf[CD_RAW_SECTOR_SIZE] = { 0 };
	BYTE subbuf[96] = { 0 };
	UINT roopSize = (UINT)GetFileSize(0, fp) / CD_RAW_SECTOR_SIZE;
	ERROR_STRUCT errStruct;
	if (!initCountNum(&errStruct, roopSize)) {
		return EXIT_FAILURE;
	}

	if (startLBA == 0 && endLBA == 0) {
		endLBA = roopSize;
	}

	BOOL skipTrackModeCheck = targetTrackMode == TrackModeUnknown;
	TrackMode trackMode = targetTrackMode;
	UINT j = 0;
	if (fpSub) {
		OutputFile("Sub file exists\n");
	}
	else {
		OutputFile("Sub file doesn't exist\n");
	}

	BYTE prevMode = 0;
	BYTE prevCtl = 0;
	BYTE byCtl = 0;
	INT nFirstLBA = 0;
	INT nLBA = 0;
	INT nPrevLBA = 0;
	BOOL bBadMsf = FALSE;

	for (UINT i = 0; i < roopSize; i++, j++) {
#ifdef _WIN32
		if (execType == checkex) {
			i = j + startLBA;
		}
#endif
		if (fread(buf, sizeof(BYTE), sizeof(buf), fp) < sizeof(buf)) {
			OutputErrorString("Failed to read [F:%s][L:%d]\n", __FUNCTION__, __LINE__);
		}
		if (i == 0) {
			nFirstLBA = MSFtoLBA(BcdToDec(buf[12]), BcdToDec(buf[13]), BcdToDec(buf[14]));
		}
		if (fpSub) {
			if (fread(subbuf, sizeof(BYTE), sizeof(subbuf), fpSub) < sizeof(subbuf)) {
				OutputErrorString("Failed to read [F:%s][L:%d]\n", __FUNCTION__, __LINE__);
			}
			byCtl = (BYTE)((subbuf[12] >> 4) & 0x0f);
			if (byCtl & 0x04) {
				if (nLBA > 0) {
					if (bBadMsf) {
						nPrevLBA++;
						bBadMsf = FALSE;
					}
					else {
						nPrevLBA = nLBA;
					}
				}
				if ((buf[13] & 0x80) == 0x80) {
					nLBA = MSFtoLBA(BcdToDec(BYTE(buf[12] ^ 0x01)), BcdToDec(BYTE(buf[13] ^ 0x80)), BcdToDec(buf[14])) - 150;
				}
				else {
					nLBA = MSFtoLBA(BcdToDec(buf[12]), BcdToDec(buf[13]), BcdToDec(buf[14])) - 150;
				}

				if (nLBA == -150 && (prevCtl & 0x04) == 0) {
					nLBA = (INT)i;
					nPrevLBA = (INT)i - 1;
				}

				if (nLBA > 0 && (prevCtl & 0x04) && nPrevLBA + 1 != nLBA) {
					errStruct.badMsfNum[errStruct.cnt_BadMsf++] = i;
					bBadMsf = TRUE;
					OutputFileWithLbaMsf("bad msf\n", nPrevLBA + 1, nPrevLBA + 1, buf[12], buf[13], buf[14]);
				}
				else {
					if (nLBA == -150 && nPrevLBA != 0) {
						// for audio sector of data track
						nLBA = nPrevLBA + 1;
					}
					handleCheckDetail(&errStruct, execType, buf, skipTrackModeCheck, trackMode, (UINT)nLBA, j, TRUE, subbuf);
				}
			}
			else {
				if (i == 0) {
					nFirstLBA = (INT)startLBA + 150;
				}
				OutputFileWithLba("audio\n", nFirstLBA - 150 + i, nFirstLBA - 150 + i);
			}
		}
		else {
			handleCheckDetail(&errStruct, execType, buf, skipTrackModeCheck, trackMode, i, j, FALSE, subbuf);
		}

		if (i == roopSize - 4 && buf[15] == 0x02 && prevMode == 0x01) {
			errStruct.invalidModeNum[errStruct.cnt_InvalidMode++] = i;
		}
		prevMode = buf[15];
		prevCtl = byCtl;

#ifdef _WIN32
		if (execType == checkex) {
			OutputString("\rChecking sectors: %6u/%6u", i, startLBA + roopSize - 1);
		}
		else {
#endif
			OutputString("\rChecking sectors: %6u/%6u", i, roopSize - 1);
#ifdef _WIN32
		}
#endif
	}
	OutputString("\n");

#ifdef _WIN32
	INT nonZeroSyncIndexStart = 0;
	INT nonZeroSyncIndexEnd = (INT)roopSize - 1;
	if (execType == checkex) {
		for (INT i = 0; i < (INT)roopSize; ++i, ++nonZeroSyncIndexStart) {
			if (errStruct.zeroSyncNum[i] == 0xFFFFFFFF)
				break;
		}

		for (INT i = (INT)roopSize - 1; i >= 0; --i, --nonZeroSyncIndexEnd) {
			if (errStruct.zeroSyncNum[i] == 0xFFFFFFFF)
				break;
		}


		if (nonZeroSyncIndexStart != (INT)roopSize - 1) { // Empty track
			assert(nonZeroSyncIndexStart <= nonZeroSyncIndexEnd);

			for (INT i = nonZeroSyncIndexStart; i <= nonZeroSyncIndexEnd; ++i) {
				if (errStruct.zeroSyncNum[i] != 0xFFFFFFFF) {
					errStruct.cnt_ZeroSync++;
				}
			}
		}
	}
#endif
	if (errStruct.cnt_BadMsf) {
		OutputLog(standardOut | file
			, "[ERROR] Number of sector(s) where bad MSF: %d\n", errStruct.cnt_BadMsf);
		OutputFile("\tSector: ");
		for (INT i = 0; i < errStruct.cnt_BadMsf; i++) {
			OutputFile("%ld, ", errStruct.badMsfNum[i]);
		}
		OutputFile("\n");
	}

	if (errStruct.cnt_SectorFilled55) {
		OutputLog(standardOut | file
			, "[ERROR] Number of sector(s) where 2336 byte is all 0x55: %d\n", errStruct.cnt_SectorFilled55);
		OutputFile("\tSector: ");
		for (INT i = 0; i < errStruct.cnt_SectorFilled55; i++) {
			OutputFile("%ld, ", errStruct.errorNum[i]);
		}
		OutputFile("\n");
	}

	if (errStruct.cnt_Mode0NotAllZero) {
		OutputLog(standardOut | file
			, "[ERROR] Number of sector(s) where user data doesn't all zero sector: %d\n", errStruct.cnt_Mode0NotAllZero);
		OutputFile("\tSector: ");
		for (INT i = 0; i < errStruct.cnt_Mode0NotAllZero; i++) {
			OutputFile("%ld, ", errStruct.notAllZeroNum[i]);
		}
		OutputFile("\n");
	}

	if (errStruct.cnt_Mode1BadEcc) {
		OutputLog(standardOut | file
			, "[ERROR] Number of sector(s) where user data doesn't match the expected ECC/EDC: %d\n", errStruct.cnt_Mode1BadEcc);
		OutputFile("\tSector: ");
		for (INT i = 0; i < errStruct.cnt_Mode1BadEcc; i++) {
			OutputFile("%ld, ", errStruct.noMatchLBANum[i]);
		}
		OutputFile("\n");
	}

	if (errStruct.cnt_Mode1ReservedNotZero) {
		OutputLog(standardOut | file,
			"[WARNING] Number of sector(s) where reserved(0x814 - 0x81b) doesn't zero: %d\n", errStruct.cnt_Mode1ReservedNotZero);
		OutputFile("\tSector: ");
		for (INT i = 0; i < errStruct.cnt_Mode1ReservedNotZero; i++) {
			OutputFile("%ld, ", errStruct.reservedNum[i]);
		}
		OutputFile("\n");
	}

	if (errStruct.cnt_Mode2) {
		OutputLog(standardOut | file,
			"[INFO] Number of sector(s) where EDC doesn't exist: %d\n", errStruct.cnt_Mode2);
#if 0
		OutputFile("\tSector: ");
		for (INT i = 0; i < errStruct.cnt_Mode2; i++) {
			OutputFile("%ld, ", errStruct.noEDCNum[i]);
		}
		OutputFile("\n");
#endif
	}

	if (errStruct.cnt_Mode2Form1SubheaderNotSame) {
		OutputLog(standardOut | file,
			"[WARNING] Number of sector(s) where mode2 form1 subheader(0x10 - 0x17) isn't same: %d\n", errStruct.cnt_Mode2Form1SubheaderNotSame);
		OutputFile("\tSector: ");
		for (INT i = 0; i < errStruct.cnt_Mode2Form1SubheaderNotSame; i++) {
			OutputFile("%ld, ", errStruct.mode2Form1Num[i]);
		}
		OutputFile("\n");
	}

	if (errStruct.cnt_Mode2Form2SubheaderNotSame) {
		OutputLog(standardOut | file,
			"[WARNING] Number of sector(s) where mode2 form2 subheader(0x10 - 0x17) isn't same: %d\n", errStruct.cnt_Mode2Form2SubheaderNotSame);
		OutputFile("\tSector: ");
		for (INT i = 0; i < errStruct.cnt_Mode2Form2SubheaderNotSame; i++) {
			OutputFile("%ld, ", errStruct.mode2Form2Num[i]);
		}
		OutputFile("\n");
	}

	if (errStruct.cnt_Mode2SubheaderNotSame) {
		OutputLog(standardOut | file,
			"[ERROR] Number of sector(s) where mode2 NoEdc subheader(0x10 - 0x17) isn't same: %d\n", errStruct.cnt_Mode2SubheaderNotSame);
		OutputFile("\tSector: ");
		for (INT i = 0; i < errStruct.cnt_Mode2SubheaderNotSame; i++) {
			OutputFile("%ld, ", errStruct.mode2Num[i]);
		}
		OutputFile("\n");
	}

	if (errStruct.cnt_InvalidMode) {
		OutputLog(standardOut | file,
			"[ERROR] Number of sector(s) where mode is invalid: %d\n", errStruct.cnt_InvalidMode);
		OutputFile("\tSector: ");
		for (INT i = 0; i < errStruct.cnt_InvalidMode; i++) {
			OutputFile("%ld, ", errStruct.invalidModeNum[i]);
		}
		OutputFile("\n");
	}

	if (errStruct.cnt_UnknownMode) {
		OutputLog(standardOut | file,
			"[ERROR] Number of sector(s) where mode(0x0f) is unknown: %d\n", errStruct.cnt_UnknownMode);
		OutputFile("\tSector: ");
		for (INT i = 0; i < errStruct.cnt_UnknownMode; i++) {
			OutputFile("%ld, ", errStruct.unknownModeNum[i]);
		}
		OutputFile("\n");
	}

	if (fpSub && errStruct.cnt_NonZeroInvalidSync) {
		OutputLog(standardOut | file,
			"[ERROR] Number of sector(s) where sync(0x00 - 0x0c) is invalid: %d\n", errStruct.cnt_NonZeroInvalidSync);
		OutputFile("\tSector: ");
		for (INT i = 0; i < errStruct.cnt_NonZeroInvalidSync; i++) {
			OutputFile("%ld, ", errStruct.nonZeroInvalidSyncNum[i]);
		}
		OutputFile("\n");
	}

	if (fpSub && errStruct.cnt_ZeroSync) {
		OutputLog(standardOut | file,
			"[ERROR] Number of sector(s) where sync(0x00 - 0x0c) is zero: %d\n", errStruct.cnt_ZeroSync);
		OutputFile("\tSector: ");
#ifdef _WIN32
		if (execType == checkex) {
			for (INT i = nonZeroSyncIndexStart; i <= nonZeroSyncIndexEnd; ++i) {
				if (errStruct.zeroSyncNum[i] != 0xFFFFFFFF) {
					OutputFile("%ld, ", errStruct.zeroSyncNum[i]);
				}
			}
		}
		else {
#endif
			for (INT i = 0; i < errStruct.cnt_ZeroSync; i++) {
				OutputFile("%ld, ", errStruct.zeroSyncNum[i]);
			}
#ifdef _WIN32
		}
#endif
		OutputFile("\n");
	}
	if (fpSub && errStruct.cnt_ZeroSyncPregap) {
		OutputLog(standardOut | file,
			"[INFO] Number of pregap sector(s) where sync(0x00 - 0x0c) is zero: %d\n", errStruct.cnt_ZeroSyncPregap);
	}

	if (fpSub && errStruct.cnt_BadMsf == 0 && errStruct.cnt_SectorFilled55 == 0 &&
		errStruct.cnt_Mode0NotAllZero == 0 &&
		errStruct.cnt_Mode1BadEcc == 0 && errStruct.cnt_Mode1ReservedNotZero == 0 &&
		errStruct.cnt_Mode2Form1SubheaderNotSame == 0 &&
		errStruct.cnt_Mode2Form2SubheaderNotSame == 0 &&
		errStruct.cnt_Mode2 == 0 && errStruct.cnt_Mode2SubheaderNotSame == 0 &&
		errStruct.cnt_InvalidMode == 0 && errStruct.cnt_UnknownMode == 0 &&
		errStruct.cnt_NonZeroInvalidSync == 0 && errStruct.cnt_ZeroSync == 0) {
		OutputLog(standardOut | file, "[NO ERROR] User data vs. ecc/edc match all\n");
	}
	else if (!fpSub && errStruct.cnt_SectorFilled55 == 0 && errStruct.cnt_Mode0NotAllZero == 0 &&
		errStruct.cnt_Mode1BadEcc == 0 &&	errStruct.cnt_Mode1ReservedNotZero == 0 &&
		errStruct.cnt_Mode2 == 0 && errStruct.cnt_InvalidMode == 0 &&
		errStruct.cnt_Mode2SubheaderNotSame == 0 && errStruct.cnt_UnknownMode == 0) {
		OutputLog(standardOut | file, "User data vs. ecc/edc match");

		if (errStruct.cnt_NonZeroInvalidSync == 0 && errStruct.cnt_ZeroSync == 0) {
			OutputLog(standardOut | file, " all\n");
		}
		if (errStruct.cnt_NonZeroInvalidSync) {
			OutputLog(standardOut | file
				, "\nAudio or invalid sync sector num: %d", errStruct.cnt_NonZeroInvalidSync);
		}
		if (errStruct.cnt_ZeroSync) {
			OutputLog(standardOut | file
				, "\nAudio or zero sync sector num: %d", errStruct.cnt_ZeroSync);
		}
		OutputLog(standardOut | file, "\n");
	}
	else {
		INT errors = errStruct.cnt_BadMsf + errStruct.cnt_SectorFilled55 + errStruct.cnt_Mode0NotAllZero +
			errStruct.cnt_Mode1BadEcc + errStruct.cnt_Mode2SubheaderNotSame +
			errStruct.cnt_ZeroSync + errStruct.cnt_InvalidMode +
			errStruct.cnt_UnknownMode + errStruct.cnt_NonZeroInvalidSync;
		OutputLog(standardOut | file, "Total errors: %d\n", errors);

		INT warnings = errStruct.cnt_Mode1ReservedNotZero + errStruct.cnt_Mode2Form1SubheaderNotSame +
			errStruct.cnt_Mode2Form2SubheaderNotSame;
		OutputLog(standardOut | file, "Total warnings: %d\n", warnings);
	}

	if (execType == fix) {
		if (errStruct.cnt_Mode1BadEcc ||
			errStruct.cnt_Mode2SubheaderNotSame ||
			errStruct.cnt_NonZeroInvalidSync) {
			INT fixedCnt = 0;

			if (errStruct.cnt_Mode1BadEcc) {
				fixedCnt += fixSectorsFromArray(execType, fp
					, errStruct.noMatchLBANum, errStruct.cnt_Mode1BadEcc, startLBA, endLBA);
			}
			if (errStruct.cnt_Mode2SubheaderNotSame) {
				fixedCnt += fixSectorsFromArray(execType, fp
					, errStruct.mode2Num, errStruct.cnt_Mode2SubheaderNotSame, startLBA, endLBA);
			}
			if (errStruct.cnt_NonZeroInvalidSync) {
				fixedCnt += fixSectorsFromArray(execType, fp
					, errStruct.nonZeroInvalidSyncNum, errStruct.cnt_NonZeroInvalidSync, startLBA, endLBA);
			}
			OutputLog(standardOut | file, "%d unmatch sector is replaced at 0x55 except header\n", fixedCnt);
		}
	}
	terminateCountNum(&errStruct);
	fclose(fp);
	if (fpSub) {
		fclose(fpSub);
	}
	fclose(fpLog);
	return EXIT_SUCCESS;
}
#ifdef _WIN32
INT handleCheckEx(
	LPCSTR filePath
) {
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
			}
			else {
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
		}
		else if (StringUtils::startsWith(line, "TRACK")) {
			if (StringUtils::endsWith(line, "AUDIO")) {
				currentTrack.trackMode = TrackModeAudio;
			}
			else if (StringUtils::endsWith(line, "MODE1/2352")) {
				currentTrack.trackMode = TrackMode1;
			}
			else if (StringUtils::endsWith(line, "MODE2/2352") || StringUtils::endsWith(line, "MODE2/2336")) {
				currentTrack.trackMode = TrackMode2;
			}
			else {
				OutputString("Invalid track mode: %s\n", line.c_str());
				return EXIT_FAILURE;
			}
		}
	}

	tracks.push_back(currentTrack);

	INT retVal = EXIT_FAILURE;

	for (auto & track : tracks) {
		if (track.trackMode != TrackModeAudio) {
			char suffixBuffer[24];
			sprintf(suffixBuffer, "Track_%lu.txt", track.trackNo + 1);

			std::string logFilePath = std::string(filePath) + "_EdcEcc_" + suffixBuffer;

			if ((retVal = handleCheckOrFix(track.trackPath.c_str(), check, track.lsnStart, track.lsnEnd, track.trackMode, logFilePath.c_str())) != EXIT_SUCCESS) {
				OutputString("Cannot check track: %s\n", track.trackPath.c_str());

				break;
			}
		}
	}
	return retVal;
}
#endif
INT handleWrite(
	LPCSTR filePath
) {
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

VOID printUsage(
	VOID
) {
#ifdef _WIN32
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
	system("pause");
#else
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
#endif
}

INT checkArg(
	INT argc,
	char* argv[],
	PEXEC_TYPE pExecType
) {
	PCHAR endptr = NULL;
	INT ret = TRUE;

	if (argc == 3 && (!strcmp(argv[1], "check"))) {
		*pExecType = check;
	}
#ifdef _WIN32
	else if (argc == 3 && (!strcmp(argv[1], "checkex"))) {
		*pExecType = checkex;
	}
#endif
	else if (argc == 3 && (!strcmp(argv[1], "fix"))) {
		*pExecType = fix;
	}
	else if (argc == 5 && (!strcmp(argv[1], "fix"))) {
		check_fix_mode_s_startLBA = (UINT)strtoul(argv[3], &endptr, 10);
		if (*endptr) {
			OutputErrorString("[%s] is invalid argument. Please input integer.\n", endptr);
			return FALSE;
		}

		check_fix_mode_s_endLBA = (UINT)strtoul(argv[4], &endptr, 10);
		if (*endptr) {
			OutputErrorString("[%s] is invalid argument. Please input integer.\n", endptr);
			return FALSE;
		}
		*pExecType = fix;
	}
	else if (argc == 8 && (!strcmp(argv[1], "write"))) {
		write_mode_s_Minute = (BYTE)strtoul(argv[3], &endptr, 10);
		if (*endptr) {
			OutputErrorString("[%s] is invalid argument. Please input integer.\n", endptr);
			return FALSE;
		}

		write_mode_s_Second = (BYTE)strtoul(argv[4], &endptr, 10);
		if (*endptr) {
			OutputErrorString("[%s] is invalid argument. Please input integer.\n", endptr);
			return FALSE;
		}

		write_mode_s_Frame = (BYTE)strtoul(argv[5], &endptr, 10);
		if (*endptr) {
			OutputErrorString("[%s] is invalid argument. Please input integer.\n", endptr);
			return FALSE;
		}

		write_mode_s_Mode = (SectorType)strtoul(argv[6], &endptr, 10);
		if (*endptr) {
			OutputErrorString("[%s] is invalid argument. Please input integer.\n", endptr);
			return FALSE;
		}

		write_mode_s_MaxRoop = strtoul(argv[7], &endptr, 10);
		if (*endptr) {
			OutputErrorString("[%s] is invalid argument. Please input integer.\n", endptr);
			return FALSE;
		}
		*pExecType = _write;
	}
	else {
		OutputErrorString("argc: %d\n", argc);
		ret = FALSE;
	}
	return ret;
}

int main(int argc, char** argv)
{
	EXEC_TYPE execType;

	if (!checkArg(argc, argv, &execType)) {
		printUsage();
		return EXIT_FAILURE;
	}

	eccedc_init(); // Initialize the ECC/EDC tables

	INT retVal = EXIT_FAILURE;

	OutputString("FILE: %s\n", argv[2]);

	if (execType == check || execType == fix) {
		std::string logFilePath = std::string(argv[2]) + "_EccEdc.txt";

		retVal = handleCheckOrFix(argv[2], execType
			, check_fix_mode_s_startLBA, check_fix_mode_s_endLBA, TrackModeUnknown, logFilePath.c_str());
	}
#ifdef _WIN32
	else if (execType == checkex) {
		retVal = handleCheckEx(argv[2]);
	}
#endif
	else if (execType == _write) {
		retVal = handleWrite(argv[2]);
	}
	return retVal;
}
