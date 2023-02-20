// Command.cpp
//
// Command line use of TacentView to perform batch image processing and conversions. The command-line interface supports
// operations such as quantization, rescaling/filtering, cropping, rotation, extracting frames, creating contact-sheets,
// amalgamating images into animated formats, and levels adjustments.
//
// Copyright (c) 2023 Tristan Grimmer.
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby
// granted, provided that the above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
// AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif
#include <Foundation/tFundamentals.h>
#include <System/tCmdLine.h>
#include <System/tPrint.h>
#include <System/tFile.h>
#include "Version.cmake.h"
#include "Command.h"
#include "TacentView.h"


namespace Command
{
	tCmdLine::tParam  ParamInputFiles	("Input image files",					"inputfiles",	0			);

	// Note, -c and --cli are reserved.
	tCmdLine::tOption OptionOutType		("Output file type",					"outtype",		'o',	1	);
	tCmdLine::tOption OptionInType		("Input file type(s)",					"intype",		'i',	1	);
	tCmdLine::tOption OptionHelp		("Print help/usage information",		"help",			'h'			);
	tCmdLine::tOption OptionSyntax		("Print syntax help",					"syntax",		's'			);

	void BeginConsoleOutput();
	void EndConsoleOutput();
	#ifdef PLATFORM_WINDOWS
	bool AttachedToConsole = false;
	#endif
	struct ConsoleOutputScoped
	{
		ConsoleOutputScoped()			{ BeginConsoleOutput(); }
		~ConsoleOutputScoped()			{ EndConsoleOutput(); }
	};

	tList<tStringItem> InputFiles;
}


void Command::BeginConsoleOutput()
{
	#ifdef PLATFORM_WINDOWS
	if (AttachedToConsole)
		return;

	// See https://www.tillett.info/2013/05/13/how-to-create-a-windows-program-that-works-as-both-as-a-gui-and-console-application/
	// We only want to attach to an existing console if the current out stream is not a pipe or disk
	// stream. This allows stuff like "prog.exe > hello.txt" and "prog.exe | prob2.exe" to work.
	bool wantStdOutAttach = true;
	HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	if (stdoutHandle)
	{
		DWORD fileType = GetFileType(stdoutHandle);
		if ((fileType == FILE_TYPE_DISK) || (fileType == FILE_TYPE_PIPE))
			wantStdOutAttach = false;
	}

	bool wantStdErrAttach = true;
	HANDLE stderrHandle = GetStdHandle(STD_ERROR_HANDLE);
	if (stderrHandle)
	{
		DWORD fileType = GetFileType(stderrHandle);
		if ((fileType == FILE_TYPE_DISK) || (fileType == FILE_TYPE_PIPE))
			wantStdErrAttach = false;
	}

	if (wantStdOutAttach || wantStdErrAttach)
	{
		// Attach to parent console. We have currently disabled creating a new console as we never
		// want a standalone console window appearing.
		if (AttachConsole(ATTACH_PARENT_PROCESS))
			AttachedToConsole = true;
		// else if (AllocConsole())
		//  	attachedToConsole = true;

		if (AttachedToConsole)
		{
			// Redirect stdout and stdin to the console.
			if (wantStdOutAttach)
			{
				freopen("CONOUT$", "w", stdout);
				setvbuf(stdout, NULL, _IONBF, 0);
			}

			if (wantStdErrAttach)
			{
				freopen("CONOUT$", "w", stderr);
				setvbuf(stderr, NULL, _IONBF, 0);
			}
		}
	}
	#endif
}


void Command::EndConsoleOutput()
{
	#ifdef PLATFORM_WINDOWS
	if (!AttachedToConsole)
		return;

	// Send the "enter" key to the console to release the command prompt on the parent console.
	INPUT ip;

	// Set up a generic keyboard event.
	ip.type = INPUT_KEYBOARD;
	ip.ki.wScan = 0;				// Hardware scan code for key.
	ip.ki.time = 0;
	ip.ki.dwExtraInfo = 0;

	// Send the Enter key.
	ip.ki.wVk = 0x0D;				// Virtual-key code for the Enter key.
	ip.ki.dwFlags = 0;				// 0 for key press.
	SendInput(1, &ip, sizeof(INPUT));

	// Release the Enter key.
	ip.ki.dwFlags = KEYEVENTF_KEYUP;
	SendInput(1, &ip, sizeof(INPUT));
	#endif
}


int Command::Process()
{
	ConsoleOutputScoped scopedConsoleOutput;
	
	tPrintf("\nTacent View %d.%d.%d in CLI Mode.\nCall with --help for instructions.\n", ViewerVersion::Major, ViewerVersion::Minor, ViewerVersion::Revision);
	if (OptionHelp)
	{
		tCmdLine::tPrintUsage(u8"Tristan Grimmer", ViewerVersion::Major, ViewerVersion::Minor, ViewerVersion::Revision);
		tPrintf
		(
R"U5AG3(Additional Notes:
01234567890123456789012345678901234567890123456789012345678901234567890123456789
You MUST call with -c or --cli to use this program in CLI mode. Even if you
just want to print syntax usage you would need -c -s in the command line.

Specify a manifest file containing images to process using the @ symbol.
eg. @list.txt will load files from a manifest file called list.txt

When processing an entire directory of images you may specify what types of
input files to process. A type like 'tif' may have more than one accepted
extension (like tiff). You don't specify the extension, you specify the type.

If no input files are specified, the current directory is processed.
If no input types are specified, all supported types are processed.
)U5AG3"
		);

		tPrintf("Supported input file types:");
		int col = 27;
		for (tSystem::tFileTypes::tFileTypeItem* typ = Viewer::FileTypes_Load.First(); typ; typ = typ->Next())
		{
			col += tPrintf(" %s", tSystem::tGetFileTypeName(typ->FileType).Chr());
			if (col > 80-4)
			{
				tPrintf("\n");
				col = 0;
			}
		}
		tPrintf("\n");

		tPrintf("Supported input file extensions:");
		col = 32;
		for (tSystem::tFileTypes::tFileTypeItem* typ = Viewer::FileTypes_Load.First(); typ; typ = typ->Next())
		{
			tList<tStringItem> extensions;
			tSystem::tGetExtensions(extensions, typ->FileType);
			for (tStringItem* ext = extensions.First(); ext; ext = ext->Next())
			{
				tPrintf(" %s", ext->Chr());
				if (col > 80-4)
				{
					tPrintf("\n");
					col = 0;
				}
			}
		}
		tPrintf("\n");
		return 0;
	}

	if (OptionSyntax)
	{
		tCmdLine::tPrintSyntax();
		return 0;
	}

	// Determine what input types are being processed.
	tSystem::tFileTypes inputTypes;
	if (OptionInType)
	{
		tList<tStringItem> types;
		OptionInType.GetArgs(types);
		for (tStringItem* typ = types.First(); typ; typ = typ->Next())
		{
			tSystem::tFileType ft = tSystem::tGetFileTypeFromName(*typ);
			inputTypes.Add(ft);
		}
	}
	else
	{
		inputTypes.Add(Viewer::FileTypes_Load);
	}
	tPrintf("Input types:");
	for (tSystem::tFileTypes::tFileTypeItem* typ = inputTypes.First(); typ; typ = typ->Next())
		tPrintf(" %s", tSystem::tGetFileTypeName(typ->FileType).Chr());
	tPrintf("\n");

	tList<tSystem::tFileInfo> foundFiles;
	if (!ParamInputFiles)
	{
		tPrintf("No input files specified. Using contents of current directory.\n");
		tSystem::tFindFiles(foundFiles, "", inputTypes);
	}

	for (tStringItem* fileItem = ParamInputFiles.Values.First(); fileItem; fileItem = fileItem->Next())
	{
		tSystem::tFileInfo info;
		bool found = tSystem::tGetFileInfo(info, *fileItem);
		if (!found)
			continue;

		if (info.Directory)
		{
			tSystem::tFindFiles(foundFiles, *fileItem, inputTypes);
		}
		else
		{
			// Only add existing files that are of a globally supported filetype.
			tSystem::tFileType typ = tSystem::tGetFileType(*fileItem);
			if (Viewer::FileTypes_Load.Contains(typ))
				foundFiles.Append(new tSystem::tFileInfo(info));
		}
	}

	tPrintf("Input Files:\n");
	for (tSystem::tFileInfo* info = foundFiles.First(); info; info = info->Next())
	{
		tPrintf("Input File: %s\n", info->FileName.Chr());
	}

	return 0;
}
