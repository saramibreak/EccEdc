#ifndef _STRING_UTILS_HPP_
#define _STRING_UTILS_HPP_

namespace StringUtils {
	BOOL startsWith(const std::wstring & str, const std::wstring & lookFor);
	BOOL startsWith(const std::string & str, const std::string & lookFor);
	BOOL endsWith(const std::wstring & str, const std::wstring & lookFor);
	BOOL endsWith(const std::string & str, const std::string & lookFor);
	BOOL equals(const std::wstring & str, const std::wstring & other);
	BOOL equals(const std::string & str, const std::string & other);
	BOOL contains(const std::wstring & str, const std::wstring & lookFor);
	BOOL contains(const std::string & str, const std::string & lookFor);

	std::string getDirectoryFromPath(const std::string & filePath);

	void trim(std::wstring & line);
	void trim(std::string & line);

	void trimBegin(std::wstring & line);
	void trimBegin(std::string & line);

	void trimEnd(std::wstring & line);
	void trimEnd(std::string & line);
};

#endif
