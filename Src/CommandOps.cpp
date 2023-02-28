// CommandOps.cpp
//
// Command line operations for batch image processing and conversions. Ooperations such as quantization,
// rescaling/filtering, cropping, rotation, extracting frames, creating contact-sheets, amalgamating images into
// animated formats, and levels adjustments are specified/implemented here.
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

#include "CommandOps.h"
#include "Command.h"


Command::OperationResize::OperationResize(const tString& argsStr)
{
	tList<tStringItem> args;
	int numArgs = tStd::tExplode(args, argsStr, ',');
	if (numArgs < 2)
	{
		tPrintfNorm("Operation Resize Invalid. At least 2 arguments required.\n");
		return;
	}

	// The AsInt calls return 0 if * is entered.
	tStringItem* currArg = args.First();
	Width = currArg->AsInt32();

	currArg = currArg->Next();
	Height = currArg->AsInt32();

	// Either width or height needs to be specified. If only one is present it uses aspect preserve.
	if ((Width <= 0) && (Height <= 0))
	{
		tPrintfNorm("Operation Resize Invalid. Width or Height or both must be specified.\n");
		return;
	}

	if (numArgs >= 3)
	{
		currArg = currArg->Next();
		ResampleFilter = tImage::tResampleFilter::Bilinear;
		for (int f = 0; f < int(tImage::tResampleFilter::NumFilters); f++)
		{
			if (currArg->IsEqualCI(tImage::tResampleFilterNamesSimple[f]))
			{
				ResampleFilter = tImage::tResampleFilter(f);
				break;
			}
		}
	}

	if (numArgs >= 4)
	{
		currArg = currArg->Next();
		EdgeMode = tImage::tResampleEdgeMode::Clamp;
		for (int e = 0; e < int(tImage::tResampleEdgeMode::NumEdgeModes); e++)
		{
			if (currArg->IsEqualCI(tImage::tResampleEdgeModeNamesSimple[e]))
			{
				EdgeMode = tImage::tResampleEdgeMode(e);
				break;
			}
		}
	}

	Valid = true;
}


bool Command::OperationResize::Apply(Viewer::Image& image)
{
	tAssert(Valid);

	int srcW = image.GetWidth();
	int srcH = image.GetHeight();
	if ((srcW <= 0) || (srcH <= 0))
		return false;

	float aspect = float(srcW) / float(srcH);

	int dstW = Width;
	int dstH = Height;
	if (dstW <= 0)
		dstW = int( float(dstH) * aspect );
	else if (dstH <= 0)
		dstH = int( float(dstW) / aspect );

	tMath::tiClamp(dstW, 4, 32768);
	tMath::tiClamp(dstH, 4, 32768);

	if ((srcW == dstW) && (srcH == dstH))
	{
		tPrintfFull("Resize not applied. Image already has correct dimensions.\n");
		return true;
	}

	tPrintfFull("Resize | Resample[Dim:%dx%d Filter:%s EdgeMode:%s]\n", dstW, dstH, tImage::tResampleFilterNamesSimple[int(ResampleFilter)], tImage::tResampleEdgeModeNamesSimple[int(EdgeMode)]);
	image.Resample(dstW, dstH, ResampleFilter, EdgeMode);
	return true;
}


Command::OperationCanvas::OperationCanvas(const tString& argsStr)
{
	tList<tStringItem> args;
	int numArgs = tStd::tExplode(args, argsStr, ',');
	if (numArgs < 2)
	{
		tPrintfNorm("Operation Canvas Invalid. At least 2 arguments required.\n");
		return;
	}

	// The AsInt calls return 0 if * is entered.
	tStringItem* currArg = args.First();
	Width = currArg->AsInt32();

	currArg = currArg->Next();
	Height = currArg->AsInt32();

	// Either width or height needs to be specified. If only one is present it uses aspect preserve.
	if ((Width <= 0) && (Height <= 0))
	{
		tPrintfNorm("Operation Canvas Invalid. Width or Height or both must be specified.\n");
		return;
	}

	// Anchor.
	if (numArgs >= 3)
	{
		currArg = currArg->Next();
		Anchor = tImage::tPicture::Anchor::MiddleMiddle;
		switch (tHash::tHashString(currArg->Chr()))
		{
			case tHash::tHashCT("tl"):	Anchor = tImage::tPicture::Anchor::LeftTop;			break;
			case tHash::tHashCT("tm"):	Anchor = tImage::tPicture::Anchor::MiddleTop;		break;
			case tHash::tHashCT("tr"):	Anchor = tImage::tPicture::Anchor::RightTop;		break;
			case tHash::tHashCT("ml"):	Anchor = tImage::tPicture::Anchor::LeftMiddle;		break;
			case tHash::tHashCT("mm"):	Anchor = tImage::tPicture::Anchor::MiddleMiddle;	break;
			case tHash::tHashCT("mr"):	Anchor = tImage::tPicture::Anchor::RightMiddle;		break;
			case tHash::tHashCT("bl"):	Anchor = tImage::tPicture::Anchor::LeftBottom;		break;
			case tHash::tHashCT("bm"):	Anchor = tImage::tPicture::Anchor::MiddleBottom;	break;
			case tHash::tHashCT("br"):	Anchor = tImage::tPicture::Anchor::RightBottom;		break;
		}
	}

	// Fill colour.
	if (numArgs >= 4)
	{
		currArg = currArg->Next();
		tString colStr = *currArg;
		if (colStr[0] == '#')
		{
			uint32 hex = colStr.AsUInt32(16);
			FillColour.Set( uint8((hex >> 24) & 0xFF), uint8((hex >> 16) & 0xFF), uint8((hex >> 8) & 0xFF), uint8((hex >> 0) & 0xFF) );
		}
		else
		{
			switch (tHash::tHashString(colStr.Chr()))
			{
				case tHash::tHashCT("black"):	FillColour = tColour4i::black;		break;
				case tHash::tHashCT("white"):	FillColour = tColour4i::white;		break;
				case tHash::tHashCT("grey"):	FillColour = tColour4i::grey;		break;
				case tHash::tHashCT("red"):		FillColour = tColour4i::red;		break;
				case tHash::tHashCT("green"):	FillColour = tColour4i::red;		break;
				case tHash::tHashCT("blue"):	FillColour = tColour4i::red;		break;
				case tHash::tHashCT("yellow"):	FillColour = tColour4i::yellow;		break;
				case tHash::tHashCT("cyan"):	FillColour = tColour4i::cyan;		break;
				case tHash::tHashCT("magenta"):	FillColour = tColour4i::magenta;	break;
			}
		}
	}

	// Anchor X.
	if (numArgs >= 5)
	{
		currArg = currArg->Next();
		tString xstr = *currArg;
		if (!xstr.IsEmpty() && (xstr[0] != '*'))
			AnchorX = xstr.AsInt32();
	}

	// Anchor Y.
	if (numArgs >= 6)
	{
		currArg = currArg->Next();
		tString ystr = *currArg;
		if (!ystr.IsEmpty() && (ystr[0] != '*'))
			AnchorY = ystr.AsInt32();
	}

	Valid = true;
}


bool Command::OperationCanvas::Apply(Viewer::Image& image)
{
	tAssert(Valid);

	int srcW = image.GetWidth();
	int srcH = image.GetHeight();
	if ((srcW <= 0) || (srcH <= 0))
		return false;

	float aspect = float(srcW) / float(srcH);

	int dstW = Width;
	int dstH = Height;
	if (dstW <= 0)
		dstW = int( float(dstH) * aspect );
	else if (dstH <= 0)
		dstH = int( float(dstW) / aspect );

	tMath::tiClamp(dstW, 4, 32768);
	tMath::tiClamp(dstH, 4, 32768);

	if ((srcW == dstW) && (srcH == dstH))
	{
		tPrintfFull("Canvas not applied. Image has same dimensions.\n");
		return true;
	}

	// Use the specified anchor pos if it was specified.
	if ((AnchorX >= 0) && (AnchorY >= 0))
	{
		// Honestly can't quite remember what this does. I believe it has something to do with
		// the coordinate system of the crop position. In any case, it works in the GUI properly
		// so we need to keep it.
		int originX = (AnchorX * (srcW - dstW)) / srcW;
		int originY = (AnchorY * (srcH - dstH)) / srcH;

		tPrintfFull("Canvas | Crop[Dim:%dx%d Origin:%d,%d Fill:%02x,%02x,%02x,%02x]\n", dstW, dstH, originX, originY, FillColour.R, FillColour.G, FillColour.B, FillColour.A);
		image.Crop(dstW, dstH, originX, originY, FillColour);
	}
	else
	{
		tPrintfFull("Canvas | Crop[Dim:%dx%d Anchor:%d Fill:%02x,%02x,%02x,%02x]\n", dstW, dstH, int(Anchor), FillColour.R, FillColour.G, FillColour.B, FillColour.A);
		image.Crop(dstW, dstH, Anchor, FillColour);
	}

	return true;
}


Command::OperationAspect::OperationAspect(const tString& argsStr)
{
	tList<tStringItem> args;
	int numArgs = tStd::tExplode(args, argsStr, ',');
	if (numArgs < 2)
	{
		tPrintfNorm("Operation Aspect Invalid. At least 2 arguments required.\n");
		return;
	}

	tStringItem* currArg = args.First();
	tList<tStringItem> aspectArgs;
	int numAspectArgs = tStd::tExplode(aspectArgs, *currArg, ':');
	if (numAspectArgs == 2)
	{
		// The AsInt calls return 0 if * is entered.
		Num = aspectArgs.First()->AsInt32();	if (Num<=0) Num = 16;
		Den = aspectArgs.Last()->AsInt32();		if (Den<=0) Den = 9;
	}

	currArg = currArg->Next();
	switch (tHash::tHashString(currArg->Chr()))
	{
		case tHash::tHashCT("crop"):		Mode = AspectMode::Crop;		break;
		case tHash::tHashCT("letter"):		Mode = AspectMode::Letterbox;	break;
		// Mode is already at default if not found.
	}

	// Anchor.
	if (numArgs >= 3)
	{
		currArg = currArg->Next();
		Anchor = tImage::tPicture::Anchor::MiddleMiddle;
		switch (tHash::tHashString(currArg->Chr()))
		{
			case tHash::tHashCT("tl"):	Anchor = tImage::tPicture::Anchor::LeftTop;			break;
			case tHash::tHashCT("tm"):	Anchor = tImage::tPicture::Anchor::MiddleTop;		break;
			case tHash::tHashCT("tr"):	Anchor = tImage::tPicture::Anchor::RightTop;		break;
			case tHash::tHashCT("ml"):	Anchor = tImage::tPicture::Anchor::LeftMiddle;		break;
			case tHash::tHashCT("mm"):	Anchor = tImage::tPicture::Anchor::MiddleMiddle;	break;
			case tHash::tHashCT("mr"):	Anchor = tImage::tPicture::Anchor::RightMiddle;		break;
			case tHash::tHashCT("bl"):	Anchor = tImage::tPicture::Anchor::LeftBottom;		break;
			case tHash::tHashCT("bm"):	Anchor = tImage::tPicture::Anchor::MiddleBottom;	break;
			case tHash::tHashCT("br"):	Anchor = tImage::tPicture::Anchor::RightBottom;		break;
		}
	}

	// Fill colour.
	if (numArgs >= 4)
	{
		currArg = currArg->Next();
		tString colStr = *currArg;
		if (colStr[0] == '#')
		{
			uint32 hex = colStr.AsUInt32(16);
			FillColour.Set( uint8((hex >> 24) & 0xFF), uint8((hex >> 16) & 0xFF), uint8((hex >> 8) & 0xFF), uint8((hex >> 0) & 0xFF) );
		}
		else
		{
			switch (tHash::tHashString(colStr.Chr()))
			{
				case tHash::tHashCT("black"):	FillColour = tColour4i::black;		break;
				case tHash::tHashCT("white"):	FillColour = tColour4i::white;		break;
				case tHash::tHashCT("grey"):	FillColour = tColour4i::grey;		break;
				case tHash::tHashCT("red"):		FillColour = tColour4i::red;		break;
				case tHash::tHashCT("green"):	FillColour = tColour4i::red;		break;
				case tHash::tHashCT("blue"):	FillColour = tColour4i::red;		break;
				case tHash::tHashCT("yellow"):	FillColour = tColour4i::yellow;		break;
				case tHash::tHashCT("cyan"):	FillColour = tColour4i::cyan;		break;
				case tHash::tHashCT("magenta"):	FillColour = tColour4i::magenta;	break;
			}
		}
	}

	// Anchor X.
	if (numArgs >= 5)
	{
		currArg = currArg->Next();
		tString xstr = *currArg;
		if (!xstr.IsEmpty() && (xstr[0] != '*'))
			AnchorX = xstr.AsInt32();
	}

	// Anchor Y.
	if (numArgs >= 6)
	{
		currArg = currArg->Next();
		tString ystr = *currArg;
		if (!ystr.IsEmpty() && (ystr[0] != '*'))
			AnchorY = ystr.AsInt32();
	}

	Valid = true;
}


bool Command::OperationAspect::Apply(Viewer::Image& image)
{
	tAssert(Valid);

	int srcW = image.GetWidth();
	int srcH = image.GetHeight();
	if ((srcW <= 0) || (srcH <= 0))
		return false;

	int dstH = srcH;
	int dstW = srcW;
	float srcAspect = float(srcW)/float(srcH);
	float dstAspect = float(Num)/float(Den);
	switch (Mode)
	{
		case AspectMode::Crop:
			if (dstAspect > srcAspect)
				dstH = tMath::tFloatToInt(float(dstW) / dstAspect);
			else if (dstAspect < srcAspect)
				dstW = tMath::tFloatToInt(float(dstH) * dstAspect);
			break;
		
		case AspectMode::Letterbox:
			if (dstAspect > srcAspect)
				dstW = tMath::tFloatToInt(float(dstH) * dstAspect);
			else if (dstAspect < srcAspect)
				dstH = tMath::tFloatToInt(float(dstW) / dstAspect);
	}

	if ((srcW == dstW) && (srcH == dstH))
	{
		tPrintfFull("Aspect not applied. Image has same dimensions.\n");
		return true;
	}

	// Use the specified anchor pos if it was specified.
	if ((AnchorX >= 0) && (AnchorY >= 0))
	{
		// Honestly can't quite remember what this does. I believe it has something to do with
		// the coordinate system of the crop position. In any case, it works in the GUI properly
		// so we need to keep it.
		int originX = (AnchorX * (srcW - dstW)) / srcW;
		int originY = (AnchorY * (srcH - dstH)) / srcH;

		tPrintfFull("Aspect | Crop[Dim:%dx%d Origin:%d,%d Fill:%02x,%02x,%02x,%02x]\n", dstW, dstH, originX, originY, FillColour.R, FillColour.G, FillColour.B, FillColour.A);
		image.Crop(dstW, dstH, originX, originY, FillColour);
	}
	else
	{
		tPrintfFull("Aspect | Crop[Dim:%dx%d Anchor:%d Fill:%02x,%02x,%02x,%02x]\n", dstW, dstH, int(Anchor), FillColour.R, FillColour.G, FillColour.B, FillColour.A);
		image.Crop(dstW, dstH, Anchor, FillColour);
	}

	return true;
}


Command::OperationDeborder::OperationDeborder(const tString& argsStr)
{
	tList<tStringItem> args;
	int numArgs = tStd::tExplode(args, argsStr, ',');
	tStringItem* currArg = nullptr;

	// Test colour.
	if (numArgs >= 1)
	{
		currArg = args.First();
		tString colStr = *currArg;
		if (colStr[0] == '#')
		{
			uint32 hex = colStr.AsUInt32(16);
			TestColour.Set( uint8((hex >> 24) & 0xFF), uint8((hex >> 16) & 0xFF), uint8((hex >> 8) & 0xFF), uint8((hex >> 0) & 0xFF) );
			UseTestColour = true;
		}
		else
		{
			UseTestColour = true;
			switch (tHash::tHashString(colStr.Chr()))
			{
				case tHash::tHashCT("black"):	TestColour = tColour4i::black;		break;
				case tHash::tHashCT("white"):	TestColour = tColour4i::white;		break;
				case tHash::tHashCT("grey"):	TestColour = tColour4i::grey;		break;
				case tHash::tHashCT("red"):		TestColour = tColour4i::red;		break;
				case tHash::tHashCT("green"):	TestColour = tColour4i::red;		break;
				case tHash::tHashCT("blue"):	TestColour = tColour4i::red;		break;
				case tHash::tHashCT("yellow"):	TestColour = tColour4i::yellow;		break;
				case tHash::tHashCT("cyan"):	TestColour = tColour4i::cyan;		break;
				case tHash::tHashCT("magenta"):	TestColour = tColour4i::magenta;	break;
				default:						UseTestColour = false;				break;
			}
		}
	}

	// Channels to test.
	if (numArgs >= 2)
	{
		currArg = currArg->Next();
		tString chanStr = *currArg;
		Channels = 0;
		if (chanStr.FindChar('*') != -1)	Channels = tComp_RGBA;
		if (chanStr.FindAny("rR") != -1)	Channels |= tComp_R;
		if (chanStr.FindAny("gG") != -1)	Channels |= tComp_G;
		if (chanStr.FindAny("bB") != -1)	Channels |= tComp_B;
		if (chanStr.FindAny("aA") != -1)	Channels |= tComp_A;
		if (!Channels)						Channels = tComp_RGBA;
	}

	Valid = true;
}


bool Command::OperationDeborder::Apply(Viewer::Image& image)
{
	tAssert(Valid);
	tColour4i testCol = TestColour;

	// Do we need to retrieve test colour from the origin (bottom-left 0,0) of the image?
	if (!UseTestColour)
		testCol = image.GetPixel(0, 0);

	tPrintfFull("Deborder | Crop[Col:%02x,%02x,%02x,%02x Channels:%08x]\n", testCol.R, testCol.G, testCol.B, testCol.A, Channels);
	image.Crop(testCol, Channels);
	return true;
}


Command::OperationCrop::OperationCrop(const tString& argsStr)
{
	tList<tStringItem> args;
	int numArgs = tStd::tExplode(args, argsStr, ',');
	
	Valid = true;
}


bool Command::OperationCrop::Apply(Viewer::Image& image)
{
	return true;
}


Command::OperationFlip::OperationFlip(const tString& argsStr)
{
	tList<tStringItem> args;
	int numArgs = tStd::tExplode(args, argsStr, ',');
	
	Valid = true;
}


bool Command::OperationFlip::Apply(Viewer::Image& image)
{
	tAssert(Valid);
	return true;
}


Command::OperationRotate::OperationRotate(const tString& argsStr)
{
	tList<tStringItem> args;
	int numArgs = tStd::tExplode(args, argsStr, ',');
	if (numArgs != 1)
	{
		tPrintfNorm("Operation Rotate Invalid. At least 1 argument required.\n");
		return;
	}

	tStringItem* currArg = args.First();
	Angle = currArg->AsFloat();

	Valid = true;
}


bool Command::OperationRotate::Apply(Viewer::Image& image)
{
	tAssert(Valid);
	float angleRadians = tMath::tDegToRad(Angle);

	tPrintfFull("Rotate | Rotate[Angle:%f]\n", angleRadians);
	image.Rotate(angleRadians, tColouri::black, tImage::tResampleFilter::Bicubic, tImage::tResampleFilter::Box);
	return true;
}


Command::OperationLevels::OperationLevels(const tString& argsStr)
{
	tList<tStringItem> args;
	int numArgs = tStd::tExplode(args, argsStr, ',');
	
	Valid = true;
}


bool Command::OperationLevels::Apply(Viewer::Image& image)
{
	tAssert(Valid);
	return true;
}


Command::OperationContrast::OperationContrast(const tString& argsStr)
{
	tList<tStringItem> args;
	int numArgs = tStd::tExplode(args, argsStr, ',');
	
	Valid = true;
}


bool Command::OperationContrast::Apply(Viewer::Image& image)
{
	tAssert(Valid);
	return true;
}


Command::OperationBrightness::OperationBrightness(const tString& argsStr)
{
	tList<tStringItem> args;
	int numArgs = tStd::tExplode(args, argsStr, ',');
	
	Valid = true;
}


bool Command::OperationBrightness::Apply(Viewer::Image& image)
{
	tAssert(Valid);
	return true;
}


Command::OperationQuantize::OperationQuantize(const tString& argsStr)
{
	tList<tStringItem> args;
	int numArgs = tStd::tExplode(args, argsStr, ',');
	
	Valid = true;
}


bool Command::OperationQuantize::Apply(Viewer::Image& image)
{
	tAssert(Valid);
	return true;
}


Command::OperationAlpha::OperationAlpha(const tString& argsStr)
{
	tList<tStringItem> args;
	int numArgs = tStd::tExplode(args, argsStr, ',');
	
	Valid = true;
}


bool Command::OperationAlpha::Apply(Viewer::Image& image)
{
	tAssert(Valid);
	return true;
}
