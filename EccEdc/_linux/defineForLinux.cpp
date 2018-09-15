
// https://groups.google.com/forum/#!topic/gnu.gcc.help/0dKxhmV4voE
// Abstract:   split a path into its parts
// Parameters: Path: Object to split
//             Drive: Logical drive , only for compatibility , not considered
//             Directory: Directory part of path
//             Filename: File part of path
//             Extension: Extension part of path (includes the leading point)
// Returns:    Directory Filename and Extension are changed
// Comment:    Note that the concept of an extension is not available in Linux,
//             nevertheless it is considered

void _splitpath(const char* Path, char* Drive, char* Directory, char* Filename, char* Extension)
{
	char* CopyOfPath = (char*)Path;
	int Counter = 0;
	int Last = 0;
	int Rest = 0;

	// no drives available in linux .
	// extensions are not common in linux
	// but considered anyway
	if (Drive != NULL) {
		if (*CopyOfPath == '/') {
			strncpy(Drive, Path, 1);
			CopyOfPath++;
			Counter++;
		}
		else {
			Drive = NULL;
		}
	}

	while (*CopyOfPath != '\0')
	{
		// search for the last slash
		while (*CopyOfPath != '/' && *CopyOfPath != '\0')
		{
			CopyOfPath++;
			Counter++;
		}
		if (*CopyOfPath == '/')
		{
			CopyOfPath++;
			Counter++;
			Last = Counter;
		}
		else
			Rest = Counter - Last;
	}
	if (Directory != NULL) {
		// directory is the first part of the path until the
		// last slash appears
		if (Drive) {
			strncpy(Directory, Path + 1, Last - 1);
		}
		else {
			strncpy(Directory, Path, Last);
		}
		// strncpy doesnt add a '\0'
		Directory[Last] = '\0';
	}
	char ext[256] = { 0 };
	char* pExt = ext;
	if (Filename != NULL) {
		// Filename is the part behind the last slahs
		strcpy(Filename, CopyOfPath -= Rest);
		strncpy(pExt, Filename, strlen(Filename));
		PathRemoveExtension(Filename);
	}
	if (Extension != NULL) {
		// get extension if there is any
		while (*pExt != '\0')
		{
			// the part behind the point is called extension in windows systems
			// at least that is what i thought apperantly the '.' is used as part
			// of the extension too .
			if (*pExt == '.')
			{
				while (*pExt != '\0')
				{
//					*Extension = *Filename;
					strncpy(Extension, pExt, 4);
//					*Filename = '\0';
					break;
//					Extension++;
//					Filename++;
				}
			}
			if (*pExt != '\0')
			{
				pExt++;
			}
		}
//		*Extension = '\0';
	}
	return;
}

// Abstract:   Make a path out of its parts
// Parameters: Path: Object to be made
//             Drive: Logical drive , only for compatibility , not considered
//             Directory: Directory part of path
//             Filename: File part of path
//             Extension: Extension part of path (includes the leading point)
// Returns:    Path is changed
// Comment:    Note that the concept of an extension is not available in Linux,
//             nevertheless it is considered

void _makepath(char* Path, const char* Drive, const char* Directory,
	const char* File, const char* Extension)
{
	while (*Drive != '\0' && Drive != NULL)
	{
		*Path = *Drive;
		Path++;
		Drive++;
	}
	while (*Directory != '\0' && Directory != NULL)
	{
		*Path = *Directory;
		Path++;
		Directory++;
	}
	while (*File != '\0' && File != NULL)
	{
		*Path = *File;
		Path++;
		File++;
	}
	while (*Extension != '\0' && Extension != NULL)
	{
		*Path = *Extension;
		Path++;
		Extension++;
	}
	*Path = '\0';
	return;
}

int PathRemoveExtension(char* path)
{
	size_t length = strlen(path);
	for (size_t j = length - 1; j > 0; j--) {
		if (path[j] == '.') {
			path[j] = '\0';
			break;
		}
	}
	return 1;
}

int GetLastError(void)
{
	return errno;
}
