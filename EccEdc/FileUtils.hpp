#ifndef _FILE_UTILS_HPP_
#define _FILE_UTILS_HPP_

namespace FileUtils {
	BOOL readFileLines(LPCSTR filePath, std::vector<std::string> & lines);
	BOOL getFileSize(LPCSTR filePath, ULONG & fileSize);
};

#endif
