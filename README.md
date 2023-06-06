# EccEdc
## Overview
 This command-line program checks or fixes the user data of the 2048 bytes per sector by using ecc/edc.
 This also creates 2352 sectors with sync, addr, ecc, edc (user data is all zero).

 This program works on Windows PC (Windows XP or higher) and Unix based PC (Linux, macOS).

## Bug report
 To: http://forum.redump.org/topic/10483/discimagecreator/
  or  
 To: https://github.com/saramibreak/EccEdc/issues

## Usage
 Start cmd.exe & run exe. for more information, run in no arg.

## Development Tool
- Visual Studio 2022 (Visual C++ 2022)
  - Linux build on Windows  
    - Windows Subsystem for Linux (WSL)  
      https://devblogs.microsoft.com/cppblog/targeting-windows-subsystem-for-linux-from-visual-studio/  
      https://docs.microsoft.com/en-us/windows/wsl/install-win10

- Linux
  - GCC, make

- macOS
  - Clang, make

## License
 See LICENSE  
 Original source is ecm.c in cmdpack-1.03-src.tar.gz  
  Copyright (C) 1996-2011 Neill Corlett  
  http://web.archive.org/web/20140330233023/http://www.neillcorlett.com/cmdpack/

## Disclaimer
 Use this tool at own your risk.
 Trouble in regard to the use of this tool, I can not guarantee any.

## Gratitude
 Thank's redump.org users.
