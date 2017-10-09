==============================What is this tool?===============================
* Summary
 This program checks or fix user data of the 2048 byte per sector by using ecc/edc.
 This also creates 2352 sector with sync, addr, ecc, edc (user data is all zero).
 It works on Windows PC (WinXP or higher).

* Usage
 Start cmd.exe & run exe. for more information, run in no arg.

* Development Tool
** first release - 20150820
 Visual Studio 2013
** 20160721 -
 Visual Studio 2015

* Source Code
 https://github.com/saramibreak/EccEdc

* License
 See gpl.txt.
 Original source is ecm.c in cmdpack-1.03-src.tar.gz
  Copyright (C) 1996-2011 Neill Corlett
  http://web.archive.org/web/20140330233023/http://www.neillcorlett.com/cmdpack/

* Disclaimer
 Use this tool at own your risk.
 Trouble in regard to the use of this tool, I can not guarantee any.

* Bug report
 To: http://forum.redump.org/topic/10483/discimagecreator/

* Gratitude
 Thank's redump.org users.

==================================Change Log===================================
*2014-04-11
http://www.mediafire.com/?glwmoytaqo00e55/
first release

*2014-04-16
http://www.mediafire.com/?74xxfl46ozbt1tz/
fixed: detect mode 2

*2014-04-29
http://www.mediafire.com/?j87e0amayhc2q7h/
fixed: skip scrambled sector

*2014-05-21
http://www.mediafire.com/?g2cbic87c48bqan/
added: analyze header from [16] to [19]

*2014-12-17
http://www.mediafire.com/?7ac64jer16sl7zi/
added: For those corrupted sectors, replace everything except the sync and header fields with 0x55 bytes.
added: checking of 0x814-0x81b per sector of mode 1 and 0x10-0x17 per sector of mode 2
fixed: output message

*2015-08-20
http://www.mediafire.com/?ls3xlze3op452a5/
added: start & end LBA to fix command
fixed: some message

*2016-07-21
updated: Visual Studio 2015 Update 2

*2016-08-05
updated: Visual Studio 2015 Update 3
fixed: the init value of error count

*2017-03-30
merged: the code of branch
refactoring: splitted the src, removed and disabled build warning
added: some messages

*2017-04-14
added: read subchannel file (for reading the data sector definitely)

*2017-04-30
changed: Some log message
changed: Split some code
changed: Disabled SSE2 of vcxproj (for old CPU)

*2017-07-02
fixed: forgot FLAG DCP

*2017-07-28
added: print total errors
added: check the reserved byte for unknown mode

*2017-08-17
fixed: invalid mode

*2017-09-09
changed: Some log messages

*2017-10-10
added: Write MSF
added: Distinguish between zero sync and zero sync of the pregap sector
