// Image.cpp
//
// An image class that can load a file from disk into main memory and to VRAM.
//
// Copyright (c) 2019-2023 Tristan Grimmer.
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby
// granted, provided that the above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
// AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#include <mutex>
#include <glad/glad.h>
#include <GLFW/glfw3.h>				// Include glfw3.h after our OpenGL definitions.
#include <Foundation/tHash.h>
#include <Foundation/tFundamentals.h>
#include <System/tFile.h>
#include <System/tTime.h>
#include <System/tMachine.h>
#include <System/tChunk.h>
#include <Math/tRandom.h>
#include "Image.h"
#include "Config.h"
using namespace tStd;
using namespace tSystem;
using namespace tImage;
using namespace tMath;
using namespace Viewer;
int Image::ThumbnailNumThreadsRunning = 0;
tString Image::ThumbCacheDir;
static tMath::tRandom::tGeneratorMersenneTwister ShuffleGenerator((uint64)tSystem::tGetTimeUTC());


const uint32 Image::ThumbChunkInfoID		= 0x0B000000;
const int Image::ThumbWidth					= 256;
const int Image::ThumbHeight				= 144;
const int Image::ThumbMinDispWidth			= 64;


Image::Image() :
	Filename(),
	Filetype(tFileType::Unknown),
	FileSizeB(0)
{
	tMemset(&FileModTime, 0, sizeof(FileModTime));
	ResetLoadParams();
	ShuffleValue = ShuffleGenerator.GetBits();
}


Image::Image(const tString& filename) :
	Filename(filename),
	Filetype(tGetFileType(filename)),
	FileSizeB(0)
{
	tMemset(&FileModTime, 0, sizeof(FileModTime));
	ResetLoadParams();
	tSystem::tFileInfo info;
	if (tSystem::tGetFileInfo(info, filename))
	{
		FileModTime = info.ModificationTime;
		FileSizeB = info.FileSize;
	}
	ShuffleValue = ShuffleGenerator.GetBits();
}


Image::Image(const tSystem::tFileInfo& fileInfo) :
	Filename(fileInfo.FileName),
	Filetype(tGetFileType(Filename)),
	FileModTime(fileInfo.ModificationTime),
	FileSizeB(fileInfo.FileSize)
{
	ResetLoadParams();
	ShuffleValue = ShuffleGenerator.GetBits();
}


Image::~Image()
{
	// If we're being destroyed before the thumbnail thread is done, we have to wait because that thread
	// accesses the thumbnail picture of this object... so 'this' must be valid.
	if (ThumbnailThread.joinable())
		ThumbnailThread.join();

	// It is important that the thread count decrements if necessary since Images can be deleted
	// when changing folders. The threads need to be available to do more work in a new folder.
	if (ThumbnailRequested && ThumbnailThreadRunning)
	{
		ThumbnailNumThreadsRunning--;
		tiClampMin(ThumbnailNumThreadsRunning, 0);
	}

	// Free GPU image mem and texture IDs.
	Unload(true);
}


void Image::RegenerateShuffleValue()
{
	ShuffleValue = ShuffleGenerator.GetBits();
}


void Image::ResetLoadParams()
{
	Config::ProfileData& profile = Config::GetProfileData();

	LoadParams_ASTC.Reset();
	LoadParams_ASTC.Gamma = profile.MonitorGamma;

	LoadParams_DDS.Reset();
	LoadParams_DDS.Gamma = profile.MonitorGamma;

	LoadParams_PVR.Reset();
	LoadParams_PVR.Gamma = profile.MonitorGamma;

	LoadParams_EXR.Reset();
	LoadParams_EXR.Gamma = profile.MonitorGamma;

	LoadParams_HDR.Reset();
	LoadParams_HDR.Gamma = profile.MonitorGamma;

	LoadParams_JPG.Reset();

	LoadParams_KTX.Reset();
	LoadParams_KTX.Gamma = profile.MonitorGamma;

	LoadParams_PKM.Reset();
	LoadParams_PKM.Gamma = profile.MonitorGamma;

	LoadParams_PNG.Reset();
	LoadParams_DetectAPNGInsidePNG = false;
}


bool Image::Load(const tString& filename, bool loadParamsFromConfig)
{
	if (filename.IsEmpty())
		return false;

	Filename = filename;
	Filetype = tGetFileType(Filename);

	tSystem::tFileInfo info;
	if (tSystem::tGetFileInfo(info, filename))
	{
		FileModTime = info.ModificationTime;
		FileSizeB = info.FileSize;
	}

	return Load(loadParamsFromConfig);
}


bool Image::Load(bool loadParamsFromConfig)
{
	if (IsLoaded() && !Dirty)
	{
		LoadedTime = tSystem::tGetTime();
		return true;
	}

	if (Filetype == tFileType::Unknown)
		return false;

	// If the type is a png file, we may actually be dealing with an apng file inside.
	// It is more efficient to only use the apng loader if we need to (even though it will
	// handle non apng files). To this end, we modify the filetype used for loading if necessary.
	// Note also that from the outside, we only determine the type from the extension. The 
	// IsAnimatedPNG call is quite expensive -- it needs to load part of the fileinto memory,
	// not something we want to do before actually loading an image. If DetectAPNGInsidePNG is
	// false, the PNG loader will always be used for .png files even if they have an apng inside.
	// The designers of apng made the format backwards compatible with single-frame png loaders.
	Config::ProfileData& profile = Config::GetProfileData();
	tSystem::tFileType loadingFiletype = Filetype;
	bool detectAPNGInsidePNG = loadParamsFromConfig ? profile.DetectAPNGInsidePNG : LoadParams_DetectAPNGInsidePNG;
	if ((Filetype == tSystem::tFileType::PNG) && detectAPNGInsidePNG && tImageAPNG::IsAnimatedPNG(Filename))
		loadingFiletype = tSystem::tFileType::APNG;

	Info.SrcPixelFormat = tPixelFormat::Invalid;
	Info.SrcColourProfile = tColourProfile::Unspecified;
	bool success = false;

	switch (loadingFiletype)
	{
		case tSystem::tFileType::APNG:
		{
			tImageAPNG apng;
			bool ok = apng.Load(Filename);
			if (!ok)
				break;

			Info.SrcPixelFormat = apng.GetPixelFormatSrc();
			int numFrames = apng.GetNumFrames();
			for (int f = 0; f < numFrames; f++)
			{
				tFrame* frame = apng.StealFrame(0);

				// This constructor sets the duration from the frame as well. The frame is deleted for you since steal is true.
				tPicture* picture = new tPicture(frame, true);
				Pictures.Append(picture);
			}
			success = true;
			break;
		}

		case tSystem::tFileType::BMP:
		{
			tImageBMP bmp;
			bool ok = bmp.Load(Filename);
			if (!ok)
				break;

			Info.SrcPixelFormat = bmp.GetPixelFormatSrc();
			int width = bmp.GetWidth();
			int height = bmp.GetHeight();
			tPixel* pixels = bmp.StealPixels();

			tPicture* picture = new tPicture(width, height, pixels, false);
			Pictures.Append(picture);
			success = true;
			break;
		}

		case tSystem::tFileType::EXR:
		{
			tImageEXR exr;
			bool ok = exr.Load(Filename, LoadParams_EXR);
			if (!ok)
				break;

			Info.SrcPixelFormat = exr.GetPixelFormatSrc();
			int numFrames = exr.GetNumFrames();
			for (int f = 0; f < numFrames; f++)
			{
				tFrame* frame = exr.StealFrame(0);

				// This constructor sets the duration from the frame as well. The frame is deleted for you since steal is true.
				tPicture* picture = new tPicture(frame, true);
				Pictures.Append(picture);
			}
			success = true;
			break;
		}

		case tSystem::tFileType::GIF:
		{
			tImageGIF gif;
			bool ok = gif.Load(Filename);
			if (!ok)
				break;

			Info.SrcPixelFormat = gif.GetPixelFormatSrc();
			int numFrames = gif.GetNumFrames();
			for (int f = 0; f < numFrames; f++)
			{
				// This steals the first frame every time, leving the remainder for the next time through.
				tFrame* frame = gif.StealFrame(0);

				// This constructor sets the duration from the frame as well. The frame is deleted for you since steal is true.
				tPicture* picture = new tPicture(frame, true);
				Pictures.Append(picture);
			}
			success = true;
			break;
		}

		case tSystem::tFileType::HDR:
		{
			tImageHDR hdr;
			bool ok = hdr.Load(Filename, LoadParams_HDR);
			if (!ok)
				break;

			Info.SrcPixelFormat = hdr.GetPixelFormatSrc();
			Info.SrcColourProfile = tColourProfile::lRGB;
			int width = hdr.GetWidth();
			int height = hdr.GetHeight();
			tPixel* pixels = hdr.StealPixels();

			tPicture* picture = new tPicture(width, height, pixels, false);
			Pictures.Append(picture);
			success = true;
			break;
		}

		case tSystem::tFileType::ICO:
		{
			tImageICO ico;
			bool ok = ico.Load(Filename);
			if (!ok)
				break;

			Info.SrcPixelFormat = ico.GetPixelFormatSrc();
			Info.SrcColourProfile = tColourProfile::sRGB;
			int numFrames = ico.GetNumFrames();
			for (int p = 0; p < numFrames; p++)
			{
				tFrame* frame = ico.StealFrame(0);
				tAssert(frame);

				// This constructor sets the duration from the frame as well. The frame is deleted for you since steal is true.
				tPicture* picture = new tPicture(frame, true);
				Pictures.Append(picture);
			}
			success = true;
			break;
		}

		case tSystem::tFileType::JPG:
		{
			tImageJPG jpg;
			tImageJPG::LoadParams params = LoadParams_JPG;
			if (loadParamsFromConfig)
			{
				params.Reset();
				params.Flags =
					(profile.StrictLoading		? tImageJPG::LoadFlag_Strict		: 0) |
					(profile.ExifOrientLoading	? tImageJPG::LoadFlag_ExifOrient	: 0);
			}

			bool ok = jpg.Load(Filename, params);
			if (!ok)
				break;

			Info.SrcPixelFormat = jpg.GetPixelFormatSrc();
			Info.SrcColourProfile = tColourProfile::sRGB;
			int width = jpg.GetWidth();
			int height = jpg.GetHeight();
			tPixel* pixels = jpg.StealPixels();

			tPicture* picture = new tPicture(width, height, pixels, false);
			Pictures.Append(picture);

			Cached_MetaData = jpg.MetaData;
			success = true;
			break;
		}

		case tSystem::tFileType::PNG:
		{
			tImagePNG png;
			tImagePNG::LoadParams params = LoadParams_PNG;
			if (loadParamsFromConfig)
			{
				params.Reset();
				params.Flags =
					(!profile.StrictLoading	? tImagePNG::LoadFlag_AllowJPG		: 0);
			}

			bool ok = png.Load(Filename, params);
			if (!ok)
				break;

			Info.SrcPixelFormat = png.GetPixelFormatSrc();
			int width = png.GetWidth();
			int height = png.GetHeight();
			tPixel* pixels = png.StealPixels();

			tPicture* picture = new tPicture(width, height, pixels, false);
			Pictures.Append(picture);
			success = true;
			break;
		}

		case tSystem::tFileType::TGA:
		{
			tImageTGA tga;
			bool ok = tga.Load(Filename);
			if (!ok)
				break;

			Info.SrcPixelFormat = tga.GetPixelFormatSrc();
			int width = tga.GetWidth();
			int height = tga.GetHeight();
			tPixel* pixels = tga.StealPixels();

			tPicture* picture = new tPicture(width, height, pixels, false);
			Pictures.Append(picture);
			success = true;
			break;
		}

		case tSystem::tFileType::QOI:
		{
			tImageQOI qoi;
			bool ok = qoi.Load(Filename);
			if (!ok)
				break;

			Info.SrcPixelFormat = qoi.GetPixelFormatSrc();
			int width = qoi.GetWidth();
			int height = qoi.GetHeight();
			tPixel* pixels = qoi.StealPixels();

			tPicture* picture = new tPicture(width, height, pixels, false);
			Pictures.Append(picture);
			success = true;
			break;
		}

		case tSystem::tFileType::TIFF:
		{
			tImageTIFF tiff;
			bool ok = tiff.Load(Filename);
			if (!ok)
				break;

			Info.SrcPixelFormat = tiff.GetPixelFormatSrc();
			int numFrames = tiff.GetNumFrames();
			for (int f = 0; f < numFrames; f++)
			{
				tFrame* frame = tiff.StealFrame(0);

				// This constructor sets the duration from the frame as well. The frame is deleted for you since steal is true.
				tPicture* picture = new tPicture(frame, true);
				Pictures.Append(picture);
			}
			success = true;
			break;
		}

		case tSystem::tFileType::WEBP:
		{
			tImageWEBP webp;
			bool ok = webp.Load(Filename);
			if (!ok)
				break;

			Info.SrcPixelFormat = webp.GetPixelFormatSrc();
			BackgroundColourOverride = webp.BackgroundColour;

			int numFrames = webp.GetNumFrames();
			for (int f = 0; f < numFrames; f++)
			{
				tFrame* frame = webp.StealFrame(0);

				// This constructor sets the duration from the frame as well. The frame is deleted for you since steal is true.
				tPicture* picture = new tPicture(frame, true);
				Pictures.Append(picture);
			}
			success = true;
			break;
		}

		case tSystem::tFileType::DDS:
		{
			tImageDDS::LoadParams params(LoadParams_DDS);
			if (loadParamsFromConfig)
			{
				if (profile.StrictLoading && !(params.Flags & tImageDDS::LoadFlag_StrictLoading))
					params.Flags |= tImageDDS::LoadFlag_StrictLoading;
				else if (!profile.StrictLoading && (params.Flags & tImageDDS::LoadFlag_StrictLoading))
					params.Flags &= ~tImageDDS::LoadFlag_StrictLoading;
			}

			tImageDDS dds;
			bool ok = dds.Load(Filename, params);
			if (!ok || !dds.IsValid())
				break;

			Info.SrcPixelFormat = dds.GetPixelFormatSrc();
			Info.SrcColourProfile = dds.GetColourProfileSrc();

			// Appends to the Pictures list.
			PopulatePicturesDDS(dds);

			// Creates any alt images for cubemap or mipmapped dds files.
			CreateAltPicturesDDS(dds);

			success = true;
			break;
		}

		case tSystem::tFileType::PVR:
		{
			tImagePVR::LoadParams params(LoadParams_PVR);
			if (loadParamsFromConfig)
			{
				if (profile.StrictLoading && !(params.Flags & tImagePVR::LoadFlag_StrictLoading))
					params.Flags |= tImagePVR::LoadFlag_StrictLoading;
				else if (!profile.StrictLoading && (params.Flags & tImagePVR::LoadFlag_StrictLoading))
					params.Flags &= ~tImagePVR::LoadFlag_StrictLoading;
			}

			tImagePVR pvr;
			bool ok = pvr.Load(Filename, params);
			if (!ok || !pvr.IsValid())
				break;

			Info.SrcPixelFormat = pvr.GetPixelFormatSrc();
			Info.SrcColourProfile = pvr.GetColourProfileSrc();

			// Appends to the Pictures list.
			PopulatePicturesPVR(pvr);

			// WIP.
			// Creates any alt images for cubemap or mipmapped pvr files.
			// CreateAltPicturesPVR(pvr);
			success = true;
			break;
		}

		case tSystem::tFileType::KTX:
		case tSystem::tFileType::KTX2:
		{
			tImageKTX ktx;
			bool ok = ktx.Load(Filename, LoadParams_KTX);
			if (!ok || !ktx.IsValid())
				break;

			Info.SrcPixelFormat = ktx.GetPixelFormatSrc();
			Info.SrcColourProfile = ktx.GetColourProfileSrc();

			// Appends to the Pictures list.
			PopulatePicturesKTX(ktx);

			// Creates any alt images for cubemap or mipmapped ktx files.
			CreateAltPicturesKTX(ktx);

			success = true;
			break;
		}

		case tSystem::tFileType::ASTC:
		{
			tImageASTC astc;
			bool ok = astc.Load(Filename, LoadParams_ASTC);
			if (!ok)
				break;

			Info.SrcPixelFormat = astc.GetPixelFormatSrc();
			int width = astc.GetWidth();
			int height = astc.GetHeight();

			tLayer* layer = astc.StealLayer();
			tAssert(layer && (layer->PixelFormat == tPixelFormat::R8G8B8A8));
			tPixel* pixels = (tPixel*)layer->StealData();

			// Give the pixels to the tPicture.
			tPicture* picture = new tPicture(width, height, pixels, false);
			Pictures.Append(picture);

			// We still delete the layer even though its data has been stolen.
			delete layer;
			success = true;
			break;
		}

		case tSystem::tFileType::PKM:
		{
			tImagePKM pkm;
			bool ok = pkm.Load(Filename, LoadParams_PKM);
			if (!ok)
				break;

			Info.SrcPixelFormat = pkm.GetPixelFormatSrc();
			Info.SrcColourProfile = pkm.GetColourProfileSrc();
			int width = pkm.GetWidth();
			int height = pkm.GetHeight();

			tLayer* layer = pkm.StealLayer();
			tAssert(layer && (layer->PixelFormat == tPixelFormat::R8G8B8A8));
			tPixel* pixels = (tPixel*)layer->StealData();

			// Give the pixels to the tPicture.
			tPicture* picture = new tPicture(width, height, pixels, false);
			Pictures.Append(picture);

			// We still delete the layer even though its data has been stolen.
			delete layer;
			success = true;
			break;
		}
	}

	if (!success)
		return false;

	LoadedTime = tSystem::tGetTime();

	// Fill in rest of info struct.
	bool foundOpaque = false; bool foundTransparent = false;
	for (tPicture* pic = Pictures.First(); pic; pic = pic->Next())
	{
		if (pic->IsOpaque())
			foundOpaque = true;
		else
			foundTransparent = true;
	}
	Info.Opacity = ImgInfo::OpacityType::Varies;
	if (foundOpaque && !foundTransparent)
		Info.Opacity = ImgInfo::OpacityType::True;
	else if (foundTransparent && !foundOpaque)
		Info.Opacity = ImgInfo::OpacityType::False;

	Info.FileSizeBytes		= tSystem::tGetFileSize(Filename);
	Info.MemSizeBytes		= GetMemSizeBytes();
	ClearDirty();
	return true;
}


bool Image::Save(const tString& outFile, tFileType fileType, bool useConfigSaveParams, bool onlyCurrentPic) const
{
	Config::ProfileData& profile = Config::GetProfileData();
	bool success = false;
	switch (fileType)
	{
		case tFileType::TGA:
		{
			tPicture* picture = GetCurrentPic();
			if (!picture || !picture->IsValid())
				return false;
			tImageTGA tga(*picture, false);
			tImageTGA::SaveParams params(SaveParamsTGA);
			if (useConfigSaveParams)
			{
				params.Format = tImageTGA::tFormat::Auto;
				switch (profile.SaveFileTgaDepthMode)
				{
					case 1: params.Format = tImageTGA::tFormat::BPP24;		break;
					case 2: params.Format = tImageTGA::tFormat::BPP32;		break;
				}

				params.Compression = profile.SaveFileTgaRLE ? tImageTGA::tCompression::RLE : tImageTGA::tCompression::None;
			}

			tImageTGA::tFormat savedFmt = tga.Save(outFile, params);
			success = (savedFmt != tImageTGA::tFormat::Invalid);
			break;
		}

		case tFileType::PNG:
		{
			tPicture* picture = GetCurrentPic();
			if (!picture || !picture->IsValid())
				return false;

			tImagePNG png(*picture, false);
			tImagePNG::SaveParams params(SaveParamsPNG);
			if (useConfigSaveParams)
			{
				params.Format = tImagePNG::tFormat::Auto;
				switch (profile.SaveFilePngDepthMode)
				{
					case 1: params.Format = tImagePNG::tFormat::BPP24;		break;
					case 2: params.Format = tImagePNG::tFormat::BPP32;		break;
				}
			}
			tImagePNG::tFormat savedFmt = png.Save(outFile, params);
			success = (savedFmt != tImagePNG::tFormat::Invalid);
			break;
		}

		case tFileType::JPG:
		{
			tPicture* picture = GetCurrentPic();
			if (!picture || !picture->IsValid())
				return false;

			tImageJPG jpg(*picture, false);
			tImageJPG::SaveParams params(SaveParamsJPG);
			if (useConfigSaveParams)
				params.Quality = profile.SaveFileJpegQuality;

			success = jpg.Save(outFile, params);
			break;
		}

		case tFileType::GIF:
		{
			tList<tFrame> frames;
			if (onlyCurrentPic)
			{
				const tPicture* picture = GetCurrentPic();
				frames.Append
				(
					new tFrame
					(
						picture->GetPixelPointer(),
						picture->GetWidth(),
						picture->GetHeight(),
						picture->Duration
					)
				);
			}
			else
			{
				const tList<tImage::tPicture>& pics = GetPictures();
				for (const tPicture* picture = pics.First(); picture; picture = picture->Next())
				{
					frames.Append
					(
						new tFrame
						(
							picture->GetPixelPointer(),
							picture->GetWidth(),
							picture->GetHeight(),
							picture->Duration
						)
					);
				}
			}

			tImageGIF gif(frames, true);
			tImageGIF::SaveParams params(SaveParamsGIF);
			if (useConfigSaveParams)
			{
				params.Format					= tPixelFormat(int(tPixelFormat::FirstPalette) + profile.SaveFileGifBPP - 1);
				params.Method					= tQuantize::Method(profile.SaveFileGifQuantMethod);
				params.Loop						= profile.SaveFileGifLoop;
				params.AlphaThreshold			= profile.SaveFileGifAlphaThreshold;
				params.OverrideFrameDuration	= profile.SaveFileGifDurOverride;
				params.DitherLevel				= double(profile.SaveFileGifDitherLevel);
				params.FilterSize				= (profile.SaveFileGifFilterSize * 2) + 1;
				params.SampleFactor				= profile.SaveFileGifSampleFactor;
			}
			success = gif.Save(outFile, params);
			break;
		}

		case tFileType::WEBP:
		{
			tList<tFrame> frames;
			if (onlyCurrentPic)
			{
				const tPicture* picture = GetCurrentPic();
				frames.Append
				(
					new tFrame
					(
						picture->GetPixelPointer(),
						picture->GetWidth(),
						picture->GetHeight(),
						picture->Duration
					)
				);
			}
			else
			{
				const tList<tImage::tPicture>& pics = GetPictures();
				for (tPicture* picture = pics.First(); picture; picture = picture->Next())
				{
					frames.Append
					(
						new tFrame
						(
							picture->GetPixelPointer(),
							picture->GetWidth(),
							picture->GetHeight(),
							picture->Duration
						)
					);
				}
			}

			tImageWEBP webp(frames, true);
			tImageWEBP::SaveParams params(SaveParamsWEBP);
			if (useConfigSaveParams)
			{
				params.Lossy = profile.SaveFileWebpLossy;
				params.QualityCompstr = profile.SaveFileWebpQualComp;
				params.OverrideFrameDuration = profile.SaveFileWebpDurOverride;
			}
			success = webp.Save(outFile, params);
			break;
		}

		case tFileType::QOI:
		{
			tPicture* picture = GetCurrentPic();
			if (!picture || !picture->IsValid())
				return false;

			tImageQOI qoi(*picture, false);
			tImageQOI::SaveParams params(SaveParamsQOI);
			if (useConfigSaveParams)
			{
				params.Format = tImageQOI::tFormat::Auto;
				switch (profile.SaveFileQoiDepthMode)
				{
					case 1: params.Format = tImageQOI::tFormat::BPP24;		break;
					case 2: params.Format = tImageQOI::tFormat::BPP32;		break;
				}

				params.Space = tImageQOI::tSpace::Auto;
				switch (profile.SaveFileQoiColourSpace)
				{
					case 1: params.Space = tImageQOI::tSpace::sRGB;			break;
					case 2: params.Space = tImageQOI::tSpace::Linear;		break;
				}
			}

			tImageQOI::tFormat savedFormat = qoi.Save(outFile, params);
			success = (savedFormat != tImageQOI::tFormat::Invalid);
			break;
		}

		case tFileType::APNG:
		{
			tList<tFrame> frames;
			if (onlyCurrentPic)
			{
				const tPicture* picture = GetCurrentPic();
				frames.Append
				(
					new tFrame
					(
						picture->GetPixelPointer(),
						picture->GetWidth(),
						picture->GetHeight(),
						picture->Duration
					)
				);
			}
			else
			{
				const tList<tImage::tPicture>& pics = GetPictures();
				for (tPicture* picture = pics.First(); picture; picture = picture->Next())
				{
					frames.Append
					(
						new tFrame
						(
							picture->GetPixelPointer(),
							picture->GetWidth(),
							picture->GetHeight(),
							picture->Duration
						)
					);
				}
			}

			tImageAPNG apng(frames, true);
			tImageAPNG::SaveParams params(SaveParamsAPNG);
			if (useConfigSaveParams)
				params.OverrideFrameDuration = profile.SaveFileApngDurOverride;
			tImageAPNG::tFormat savedFormat = apng.Save(outFile, params);
			success = (savedFormat != tImageAPNG::tFormat::Invalid);
			break;
		}

		case tFileType::BMP:
		{
			tPicture* picture = GetCurrentPic();
			if (!picture || !picture->IsValid())
				return false;

			tImageBMP bmp(*picture, false);
			tImageBMP::SaveParams params(SaveParamsBMP);
			if (useConfigSaveParams)
			{
				params.Format = tImageBMP::tFormat::Auto;
				switch (profile.SaveFileBmpDepthMode)
				{
					case 1: params.Format = tImageBMP::tFormat::BPP24;		break;
					case 2: params.Format = tImageBMP::tFormat::BPP32;		break;
				}
			}
			tImageBMP::tFormat savedFormat = bmp.Save(outFile, params);
			success = (savedFormat != tImageBMP::tFormat::Invalid);
			break;
		}

		case tFileType::TIFF:
		{
			tList<tFrame> frames;
			if (onlyCurrentPic)
			{
				const tPicture* picture = GetCurrentPic();
				frames.Append
				(
					new tFrame
					(
						picture->GetPixelPointer(),
						picture->GetWidth(),
						picture->GetHeight(),
						picture->Duration
					)
				);
			}
			else
			{
				const tList<tImage::tPicture>& pics = GetPictures();
				for (tPicture* picture = pics.First(); picture; picture = picture->Next())
				{
					frames.Append
					(
						new tFrame
						(
							picture->GetPixelPointer(),
							picture->GetWidth(),
							picture->GetHeight(),
							picture->Duration
						)
					);
				}
			}

			tImageTIFF tiff(frames, true);
			tImageTIFF::SaveParams params(SaveParamsTIFF);
			if (useConfigSaveParams)
			{
				params.UseZLibCompression = profile.SaveFileTiffZLibDeflate;
				params.OverrideFrameDuration = profile.SaveFileTiffDurOverride;
			}
			success = tiff.Save(outFile, params);
			break;
		}
	}

	return success;
}


int Image::GetMemSizeBytes() const
{
	int numBytes = 0;
	for (tPicture* pic = Pictures.First(); pic; pic = pic->Next())
		numBytes += pic->GetNumPixels() * sizeof(tPixel);

	numBytes += AltPicture.IsValid() ? AltPicture.GetNumPixels()*sizeof(tPixel) : 0;
	return numBytes;
}


void Image::PopulatePicturesDDS(const tImageDDS& dds)
{
	if (dds.IsCubemap())
	{
		int w = dds.GetWidth();
		int h = dds.GetHeight();

		// We want the front (+Z) to be the first image.
		const int numSides = tImageDDS::tSurfIndex_NumSurfaces;
		int sideOrder[numSides] =
		{
			tImageDDS::tSurfIndex_PosZ,
			tImageDDS::tSurfIndex_NegZ,
			tImageDDS::tSurfIndex_PosX,
			tImageDDS::tSurfIndex_NegX,
			tImageDDS::tSurfIndex_PosY,
			tImageDDS::tSurfIndex_NegY
		};

		tList<tLayer> layers[numSides];
		dds.GetCubemapLayers(layers);		
		for (int s = 0; s < int(tImageDDS::tSurfIndex_NumSurfaces); s++)
		{
			int side = sideOrder[s];

			tLayer* topMip = layers[side].First();
			tAssert(topMip->PixelFormat == tPixelFormat::R8G8B8A8);
			Pictures.Append(new tPicture(w, h, (tPixel*)topMip->Data, true));

			// Lets reset the list so it doesn't do anything bad when destructed.
			layers[side].Reset();
		}
	}
	else
	{
		int w = dds.GetWidth();
		int h = dds.GetHeight();

		tList<tLayer> layers;
		dds.GetLayers(layers);

		int numMipmaps = layers.GetNumItems();
		for (tLayer* layer = layers.First(); layer; layer = layer->Next())
			Pictures.Append(new tPicture(layer->Width, layer->Height, (tPixel*)layer->Data, true));

		layers.Reset();
	}
}


void Image::CreateAltPicturesDDS(const tImageDDS& dds)
{
	if (dds.IsCubemap())
	{
		int width = Pictures.First()->GetWidth();
		int height = Pictures.First()->GetHeight();

		AltPicture.Set(width*4, height*3, tPixel::transparent);
		int originX, originY;
		
		// PosZ
		tPicture* pic = Pictures.First();
		originX = width; originY = height;
		for (int y = 0; y < pic->GetHeight(); y++)
			for (int x = 0; x < pic->GetWidth(); x++)
				AltPicture.SetPixel(originX + x, originY + y, pic->GetPixel(x, y));

		// NegZ
		pic = pic->Next();
		originX = 3*width; originY = height;
		for (int y = 0; y < pic->GetHeight(); y++)
			for (int x = 0; x < pic->GetWidth(); x++)
				AltPicture.SetPixel(originX + x, originY + y, pic->GetPixel(x, y));

		// PosX
		pic = pic->Next();
		originX = 2*width; originY = height;
		for (int y = 0; y < pic->GetHeight(); y++)
			for (int x = 0; x < pic->GetWidth(); x++)
				AltPicture.SetPixel(originX + x, originY + y, pic->GetPixel(x, y));

		// NegX
		pic = pic->Next();
		originX = 0; originY = height;
		for (int y = 0; y < pic->GetHeight(); y++)
			for (int x = 0; x < pic->GetWidth(); x++)
				AltPicture.SetPixel(originX + x, originY + y, pic->GetPixel(x, y));

		// PosY
		pic = pic->Next();
		originX = width; originY = 2*height;
		for (int y = 0; y < pic->GetHeight(); y++)
			for (int x = 0; x < pic->GetWidth(); x++)
				AltPicture.SetPixel(originX + x, originY + y, pic->GetPixel(x, y));

		// NegY
		pic = pic->Next();
		originX = width; originY = 0;
		for (int y = 0; y < pic->GetHeight(); y++)
			for (int x = 0; x < pic->GetWidth(); x++)
				AltPicture.SetPixel(originX + x, originY + y, pic->GetPixel(x, y));

		AltPictureTyp = AltPictureType::CubemapTLayout;
	}
	else if (dds.IsMipmapped())
	{
		int width = 0;
		for (tPicture* layer = Pictures.First(); layer; layer = layer->Next())
			width += layer->GetWidth();
		int height = GetHeight();

		AltPicture.Set(width, height, tPixel::transparent);
		int originY = 0;
		int originX = 0;
		for (tPicture* layer = Pictures.First(); layer; layer = layer->Next())
		{
			for (int y = 0; y < layer->GetHeight(); y++)
			{
				for (int x = 0; x < layer->GetWidth(); x++)
				{
					tPixel pixel = layer->GetPixel(x, y);
					AltPicture.SetPixel(originX + x, y, pixel);
				}
			}
			originX += layer->GetWidth();
		}
		AltPictureTyp = AltPictureType::MipmapSideBySide;
	}
}


void Image::PopulatePicturesPVR(const tImagePVR& pvr)
{
	int w = pvr.GetWidth();
	int h = pvr.GetHeight();

	tList<tLayer> layers;
	pvr.GetLayers(layers);

	int numMipmaps = layers.GetNumItems();
	for (tLayer* layer = layers.First(); layer; layer = layer->Next())
		Pictures.Append(new tPicture(layer->Width, layer->Height, (tPixel*)layer->Data, true));

	layers.Reset();
}


void Image::PopulatePicturesKTX(const tImageKTX& ktx)
{
	if (ktx.IsCubemap())
	{
		int w = ktx.GetWidth();
		int h = ktx.GetHeight();

		// We want the front (+Z) to be the first image.
		const int numSides = tImageKTX::tSurfIndex_NumSurfaces;
		int sideOrder[numSides] =
		{
			tImageKTX::tSurfIndex_PosZ,
			tImageKTX::tSurfIndex_NegZ,
			tImageKTX::tSurfIndex_PosX,
			tImageKTX::tSurfIndex_NegX,
			tImageKTX::tSurfIndex_PosY,
			tImageKTX::tSurfIndex_NegY
		};

		tList<tLayer> layers[numSides];
		ktx.GetCubemapLayers(layers);
		for (int s = 0; s < int(tImageKTX::tSurfIndex_NumSurfaces); s++)
		{
			int side = sideOrder[s];

			tLayer* topMip = layers[side].First();
			tAssert(topMip->PixelFormat == tPixelFormat::R8G8B8A8);
			Pictures.Append(new tPicture(w, h, (tPixel*)topMip->Data, true));

			// Lets reset the list so it doesn't do anything bad when destructed.
			layers[side].Reset();
		}
	}
	else
	{
		int w = ktx.GetWidth();
		int h = ktx.GetHeight();

		tList<tLayer> layers;
		ktx.GetLayers(layers);

		int numMipmaps = layers.GetNumItems();
		for (tLayer* layer = layers.First(); layer; layer = layer->Next())
			Pictures.Append(new tPicture(layer->Width, layer->Height, (tPixel*)layer->Data, true));

		layers.Reset();
	}
}


void Image::CreateAltPicturesKTX(const tImageKTX& ktx)
{
	if (ktx.IsCubemap())
	{
		int width = Pictures.First()->GetWidth();
		int height = Pictures.First()->GetHeight();

		AltPicture.Set(width*4, height*3, tPixel::transparent);
		int originX, originY;
		
		// PosZ
		tPicture* pic = Pictures.First();
		originX = width; originY = height;
		for (int y = 0; y < pic->GetHeight(); y++)
			for (int x = 0; x < pic->GetWidth(); x++)
				AltPicture.SetPixel(originX + x, originY + y, pic->GetPixel(x, y));

		// NegZ
		pic = pic->Next();
		originX = 3*width; originY = height;
		for (int y = 0; y < pic->GetHeight(); y++)
			for (int x = 0; x < pic->GetWidth(); x++)
				AltPicture.SetPixel(originX + x, originY + y, pic->GetPixel(x, y));

		// PosX
		pic = pic->Next();
		originX = 2*width; originY = height;
		for (int y = 0; y < pic->GetHeight(); y++)
			for (int x = 0; x < pic->GetWidth(); x++)
				AltPicture.SetPixel(originX + x, originY + y, pic->GetPixel(x, y));

		// NegX
		pic = pic->Next();
		originX = 0; originY = height;
		for (int y = 0; y < pic->GetHeight(); y++)
			for (int x = 0; x < pic->GetWidth(); x++)
				AltPicture.SetPixel(originX + x, originY + y, pic->GetPixel(x, y));

		// PosY
		pic = pic->Next();
		originX = width; originY = 2*height;
		for (int y = 0; y < pic->GetHeight(); y++)
			for (int x = 0; x < pic->GetWidth(); x++)
				AltPicture.SetPixel(originX + x, originY + y, pic->GetPixel(x, y));

		// NegY
		pic = pic->Next();
		originX = width; originY = 0;
		for (int y = 0; y < pic->GetHeight(); y++)
			for (int x = 0; x < pic->GetWidth(); x++)
				AltPicture.SetPixel(originX + x, originY + y, pic->GetPixel(x, y));

		AltPictureTyp = AltPictureType::CubemapTLayout;
	}
	else if (ktx.IsMipmapped())
	{
		int width = 0;
		for (tPicture* layer = Pictures.First(); layer; layer = layer->Next())
			width += layer->GetWidth();
		int height = GetHeight();

		AltPicture.Set(width, height, tPixel::transparent);
		int originY = 0;
		int originX = 0;
		for (tPicture* layer = Pictures.First(); layer; layer = layer->Next())
		{
			for (int y = 0; y < layer->GetHeight(); y++)
			{
				for (int x = 0; x < layer->GetWidth(); x++)
				{
					tPixel pixel = layer->GetPixel(x, y);
					AltPicture.SetPixel(originX + x, y, pixel);
				}
			}
			originX += layer->GetWidth();
		}
		AltPictureTyp = AltPictureType::MipmapSideBySide;
	}
}


bool Image::Unload(bool force)
{
	if (!IsLoaded())
		return true;

	// Not allowed to unload if dirty (modified).
	if (Dirty && !force)
		return false;

	Unbind();
	AltPicture.Clear();
	AltPictureEnabled = false;
	AltPictureTyp = AltPictureType::None;
	Pictures.Clear();
	Info.MemSizeBytes = 0;

	LoadedTime = -1.0f;
	return true;
}


bool Image::IsOpaque() const
{
	if (AltPicture.IsValid() && AltPictureEnabled)
		return AltPicture.IsOpaque();

	tPicture* picture = GetCurrentPic();
	if (picture && picture->IsValid())
		return picture->IsOpaque();

	return true;
}


int Image::GetWidth() const
{
	if (AltPicture.IsValid() && AltPictureEnabled)
		return AltPicture.GetWidth();

	tPicture* picture = GetCurrentPic();
	if (picture && picture->IsValid())
		return picture->GetWidth();

	return 0;
}


int Image::GetHeight() const
{
	if (AltPicture.IsValid() && AltPictureEnabled)
		return AltPicture.GetHeight();

	tPicture* picture = GetCurrentPic();
	if (picture && picture->IsValid())
		return picture->GetHeight();

	return 0;
}


int Image::GetArea() const
{
	if (AltPicture.IsValid() && AltPictureEnabled)
		return AltPicture.GetArea();

	tPicture* picture = GetCurrentPic();
	if (picture && picture->IsValid())
		return picture->GetArea();

	return 0;
}


tColouri Image::GetPixel(int x, int y) const
{
	if (AltPicture.IsValid() && AltPictureEnabled)
		return AltPicture.GetPixel(x, y);

	tPicture* picture = GetCurrentPic();
	if (picture && picture->IsValid())
		return picture->GetPixel(x, y);

	// Generally the PictureImage should always be valid. When dds files (tTextures) are loaded, they get
	// uncompressed into valid PictureImage files so the pixel info can be read.
	return tColouri::black;
}


void Image::Rotate90(bool antiClockWise)
{
	tString desc; tsPrintf(desc, "Rotate 90 %s", antiClockWise ? "ACW" : "CW");
	PushUndo(desc);
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->Rotate90(antiClockWise);

	Dirty = true;
}


bool Image::Rotate(float angle, const tColouri& fill, tResampleFilter upFilter, tResampleFilter downFilter)
{
	if (angle == 0.0f)
		return false;

	tString desc; tsPrintf(desc, "Rotate %.1f", tRadToDeg(angle));
	PushUndo(desc);
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->RotateCenter(angle, fill, upFilter, downFilter);

	Dirty = true;
	return true;
}


void Image::QuantizeFixed(int numColours, bool checkExact)
{
	tString desc; tsPrintf(desc, "Quantize %d", numColours);
	PushUndo(desc);
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->QuantizeFixed(numColours, checkExact);

	Dirty = true;
}


void Image::QuantizeSpatial(int numColours, bool checkExact, double ditherLevel, int filterSize)
{
	tString desc; tsPrintf(desc, "Quantize %d", numColours);
	PushUndo(desc);
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->QuantizeSpatial(numColours, checkExact, ditherLevel, filterSize);

	Dirty = true;
}


void Image::QuantizeNeu(int numColours, bool checkExact, int sampleFactor)
{
	tString desc; tsPrintf(desc, "Quantize %d", numColours);
	PushUndo(desc);
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->QuantizeNeu(numColours, checkExact, sampleFactor);

	Dirty = true;
}


void Image::QuantizeWu(int numColours, bool checkExact)
{
	tString desc; tsPrintf(desc, "Quantize %d", numColours);
	PushUndo(desc);
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->QuantizeWu(numColours, checkExact);

	Dirty = true;
}


bool Image::AdjustmentBegin()
{
	if (!IsLoaded())
		return false;

	PushUndo("Levels");
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->AdjustmentBegin();

	return true;
}


void Image::AdjustBrightness(float brightness, AdjChan channels, bool allFrames)
{
	if (allFrames)
	{
		for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
			picture->AdjustBrightness(brightness, ComponentBits(channels));
	}
	else
	{
		tPicture* picture = GetCurrentPic();
		if (picture)
			picture->AdjustBrightness(brightness, ComponentBits(channels));
	}
	Dirty = true;
}


void Image::AdjustContrast(float contrast, AdjChan channels, bool allFrames)
{
	if (allFrames)
	{
		for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
			picture->AdjustContrast(contrast, ComponentBits(channels));
	}
	else
	{
		tPicture* picture = GetCurrentPic();
		if (picture)
			picture->AdjustContrast(contrast, ComponentBits(channels));
	}
	Dirty = true;
}


void Image::AdjustLevels(float blackPoint, float midPoint, float whitePoint, float blackOut, float whiteOut, bool powerMidGamma, AdjChan channels, bool allFrames)
{
	if (allFrames)
	{
		for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
			picture->AdjustLevels(blackPoint, midPoint, whitePoint, blackOut, whiteOut, powerMidGamma, ComponentBits(channels));
	}
	else
	{
		tPicture* picture = GetCurrentPic();
		if (picture)
			picture->AdjustLevels(blackPoint, midPoint, whitePoint, blackOut, whiteOut, powerMidGamma, ComponentBits(channels));
	}
	Dirty = true;
}


void Image::AdjustRestoreOriginal(bool popUndo)
{
	if (popUndo)
		PopUndo();

	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->AdjustRestoreOriginal();

	Dirty = false;
}


void Image::AdjustGetDefaults(float& brightness, float& contrast, float& blackPoint, float& midPoint, float& whitePoint, float& blackOut, float& whiteOut) const
{
	tPicture* pic = GetCurrentPic();
	if (!pic)
		return;
	pic->AdjustGetDefaultBrightness(brightness);
	pic->AdjustGetDefaultContrast(contrast);
	pic->AdjustGetDefaultLevels(blackPoint, midPoint, whitePoint, blackOut, whiteOut);
}


bool Image::AdjustmentEnd()
{
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->AdjustmentEnd();

	return true;
}


void Image::Flip(bool horizontal)
{
	tString desc; tsPrintf(desc, "Flip %s", horizontal ? "Horiz" : "Vert");
	PushUndo(desc);
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->Flip(horizontal);

	Dirty = true;
}


bool Image::Crop(int newWidth, int newHeight, int originX, int originY, const tColouri& fillColour)
{
	bool atLeastOneDifferentSize = false;
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
	{
		if ((picture->GetWidth() != newWidth) || (picture->GetHeight() != newHeight))
		{
			atLeastOneDifferentSize = true;
			break;
		}
	}
	if (!atLeastOneDifferentSize)
		return false;

	tString desc; tsPrintf(desc, "Crop %d %d", newWidth, newHeight);
	PushUndo(desc);
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->Crop(newWidth, newHeight, originX, originY, fillColour);

	Dirty = true;
	return true;
}


bool Image::Crop(int newWidth, int newHeight, tPicture::Anchor anchor, const tColouri& fillColour)
{
	bool atLeastOneDifferentSize = false;
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
	{
		if ((picture->GetWidth() != newWidth) || (picture->GetHeight() != newHeight))
		{
			atLeastOneDifferentSize = true;
			break;
		}
	}
	if (!atLeastOneDifferentSize)
		return false;

	tString desc; tsPrintf(desc, "Crop %d %d", newWidth, newHeight);
	PushUndo(desc);
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->Crop(newWidth, newHeight, anchor, fillColour);

	Dirty = true;
	return true;
}


bool Image::Deborder(const tColouri& borderColour, comp_t channels)
{
	bool atLeastOneHasBorders = false;
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
	{
		if (picture->HasBorders(borderColour, channels))
		{
			atLeastOneHasBorders = true;
			break;
		}
	}
	if (!atLeastOneHasBorders)
		return false;

	PushUndo("Crop Borders");
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->Deborder(borderColour, channels);

	Dirty = true;
	return true;
}


bool Image::Resample(int newWidth, int newHeight, tImage::tResampleFilter filter, tImage::tResampleEdgeMode edgeMode)
{
	bool atLeastOneDifferentSize = false;
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
	{
		if ((picture->GetWidth() != newWidth) || (picture->GetHeight() != newHeight))
		{
			atLeastOneDifferentSize = true;
			break;
		}
	}
	if (!atLeastOneDifferentSize)
		return false;

	tString desc; tsPrintf(desc, "Resample %d %d", newWidth, newHeight);
	PushUndo(desc);
	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->Resample(newWidth, newHeight, filter, edgeMode);

	Dirty = true;
	return true;
}


void Image::SetPixelColour(int x, int y, const tColouri& colour, bool pushUndo, bool surpressDirty)
{
	if (pushUndo)
	{
		tString desc; tsPrintf(desc, "Pixel Colour (%d,%d)", x, y);
		PushUndo(desc);
	}

	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
	{
		if ((x > 0) && (x < picture->GetWidth()) && (y > 0) && (y < picture->GetHeight()))
			picture->SetPixel(x, y, colour);
	}

	if (!surpressDirty)
		Dirty = true;
}


void Image::SetAllPixels(const tColouri& colour, comp_t channels)
{
	tString desc; tsPrintf(desc, "Set Pixels (%d,%d,%d,%d)", colour.R, colour.G, colour.B, colour.A);
	PushUndo(desc);

	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->SetAll(colour, channels);

	Dirty = true;
}


void Image::Spread(tComp channel)
{
	if (!tIsColourComponent(channel))
		return;

	tString desc; tsPrintf(desc, "Spread %s", tGetComponentName(channel));
	PushUndo(desc);

	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->Spread(channel);

	Dirty = true;
}


void Image::Swizzle(tComp R, tComp G, tComp B, tComp A)
{
	if (tIsMatrixComponent(R) || tIsMatrixComponent(G) || tIsMatrixComponent(B) || tIsMatrixComponent(A))
		return;

	tString channelsStr =
		tString(tGetComponentName(R)) +
		tString(tGetComponentName(G)) +
		tString(tGetComponentName(B)) +
		tString(tGetComponentName(A));

	tString desc; tsPrintf(desc, "Swizzle %s", channelsStr.Chr());
	PushUndo(desc);

	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->Swizzle(R, G, B, A);

	Dirty = true;
}


// Computes RGB intensity and sets specified channels to that value. Any combination of RGBA allowed.
void Image::Intensity(comp_t channels)
{
	tString channelsStr;
	if (channels & tCompBit_R) channelsStr += "R";
	if (channels & tCompBit_G) channelsStr += "G";
	if (channels & tCompBit_B) channelsStr += "B";
	if (channels & tCompBit_A) channelsStr += "A";

	tString desc; tsPrintf(desc, "Intensity %s", channelsStr.Chr());
	PushUndo(desc);

	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->Intensity(channels);

	Dirty = true;
}


void Image::AlphaBlendColour(const tColouri& colour, comp_t channels, int finalAlpha)
{
	tString desc; tsPrintf(desc, "Blend (%d,%d,%d,%d)", colour.R, colour.G, colour.B, colour.A);
	PushUndo(desc);

	for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
		picture->AlphaBlendColour(colour, channels, finalAlpha);

	Dirty = true;
}


void Image::SetFrameDuration(float duration, bool allFrames)
{
	tString desc; tsPrintf(desc, "Frame Dur %.3f", duration);
	PushUndo(desc);

	if (allFrames)
	{
		for (tPicture* picture = Pictures.First(); picture; picture = picture->Next())
			picture->Duration = duration;
	}
	else
	{
		tPicture* pic = GetCurrentPic();
		if (pic)
			pic->Duration = duration;
	}

	Dirty = true;
}


uint64 Image::Bind()
{
	// We bind in a particular order starting with alternate picture if enabled and valid and
	// then current picture. In all cases if the texture ID is already valid, we use it right away and early exit.
	Config::ProfileData& profile = Config::GetProfileData();
	if (AltPictureEnabled && AltPicture.IsValid())
	{
		if (TexIDAlt != 0)
		{
			glBindTexture(GL_TEXTURE_2D, TexIDAlt);
			return TexIDAlt;
		}

		glGenTextures(1, &TexIDAlt);
		if (TexIDAlt == 0)
			return 0;

		tList<tLayer> layers;
		AltPicture.GenerateLayers(layers, tResampleFilter(profile.MipmapFilter), tResampleEdgeMode::Clamp, profile.MipmapChaining);
		BindLayers(layers, TexIDAlt);
		return TexIDAlt;
	}

	tPicture* currPic = GetCurrentPic();
	if (currPic && (currPic->TextureID != 0))
	{
		glBindTexture(GL_TEXTURE_2D, currPic->TextureID);
		return currPic->TextureID;
	}

	if (!IsLoaded())
		return 0;

	// We do this in reverse order so that the last image we bind is the highest resolution image.
	// This is more efficient when it comes to drawing the image since the lowest mip is likely not
	// the one we're going to be viewing right after binding.
	for (tPicture* picture = Pictures.Last(); picture; picture = picture->Prev())
	{
		if (!picture->IsValid())
			continue;

		tAssert(picture->TextureID == 0);
		glGenTextures(1, &picture->TextureID);

		tList<tLayer> layers;
		picture->GenerateLayers(layers, tResampleFilter(profile.MipmapFilter), tResampleEdgeMode::Clamp, profile.MipmapChaining);
		BindLayers(layers, picture->TextureID);
	}
	return GetCurrentPic()->TextureID;
}


void Image::Unbind()
{
	for (tPicture* pic = Pictures.First(); pic; pic = pic->Next())
	{
		if (pic->TextureID != 0)
		{
			glDeleteTextures(1, &pic->TextureID);
			pic->TextureID = 0;
		}
	}

	if (TexIDAlt != 0)
	{
		glDeleteTextures(1, &TexIDAlt);
		TexIDAlt = 0;
	}
}


void Image::BindLayers(const tList<tLayer>& layers, uint texID)
{
	if (layers.IsEmpty())
		return;

	// Since all layers are the same pixel format we first check if we support loading the format and early exit if we don't.
	// Note that ATM we are decoding all DDS files to RGBA, so they are not compressed.
	GLint srcFormat, dstFormat; GLenum srcType; bool compressed;
	tPixelFormat pixelFormat = layers.First()->PixelFormat;
	GetGLFormatInfo(srcFormat, srcType, dstFormat, compressed, pixelFormat);
	if (compressed  && (dstFormat == GL_INVALID_VALUE))
		return;
	if (!compressed && ((srcFormat == GL_INVALID_VALUE) || (srcType == GL_INVALID_ENUM) || (dstFormat == GL_INVALID_VALUE)))
		return;

	glBindTexture(GL_TEXTURE_2D, texID);
	//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	// If the texture format is a mipmapped one, we need to set up OpenGL slightly differently.
	bool mipmapped = layers.GetNumItems() > 1;
	if (mipmapped)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	else
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		// If we're not mipmapping anyway, we might as well avoid bleeding that we get with GL_LINEAR.
		// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	int mipmapLevel = 0;
	if (compressed) for (tLayer* layer = layers.First(); layer; layer = layer->Next(), mipmapLevel++)
		// For each layer (non-mipmapped formats will only have one) we need to submit the texture data.
		// Do a straight DMA. No conversion. Fast.
		glCompressedTexImage2D(GL_TEXTURE_2D, mipmapLevel, dstFormat, layer->Width, layer->Height, 0, layer->GetDataSize(), layer->Data);

	else for (tLayer* layer = layers.First(); layer; layer = layer->Next(), mipmapLevel++)
		// Although this call can handle compressing during the DMA, it should never need to do any work because
		// the internal and external texture formats should always be identical. This isn't always entirely true.
		// The nVidia paper "Achieving Efficient Bandwidth Rates" explains that the src data should be in BGRA,
		// while the dest can be RGBA8 (for 32bit textures). This is because internally to the driver the OpenGL
		// internalFormal GL_RGBA8 will be stored as BGRA so if the source isn't BGRA then some swizzling takes
		// place. This is why PixelFormat_B8G8R8A8 is quite efficient for example.
		glTexImage2D(GL_TEXTURE_2D, mipmapLevel, dstFormat, layer->Width, layer->Height, 0, srcFormat, srcType, layer->Data);
}


void Image::GetGLFormatInfo(GLint& srcFormat, GLenum& srcType, GLint& dstFormat, bool& compressed, tPixelFormat pixelFormat)
{
	srcFormat	= GL_INVALID_VALUE;
	srcType		= GL_INVALID_ENUM;
	dstFormat	= GL_INVALID_VALUE;
	compressed	= false;

	// Note that the destination format only specifies the resolution of each colour component, not the order or the
	// specifics -- they're up to the OpenGL driver. For example, nVidia cards use an internal BGRA format when GL_RGBA8
	// is specified -- that's why having srcFormat = GL_BGRA and dstFormat = RGBA8 is very efficient (no swizzling).
	tAssert(GL_EXT_texture_compression_s3tc);
	switch (pixelFormat)
	{
		case tPixelFormat::R8G8B8:
			srcFormat = GL_RGB;										srcType = GL_UNSIGNED_BYTE;
			dstFormat = GL_RGB8;
			break;

		case tPixelFormat::R8G8B8A8:
			srcFormat = GL_RGBA;									srcType = GL_UNSIGNED_BYTE;
			dstFormat = GL_RGBA8;
			break;

		case tPixelFormat::B8G8R8:
			srcFormat = GL_BGR;										srcType = GL_UNSIGNED_BYTE;
			dstFormat = GL_RGB8;
			break;

		case tPixelFormat::B8G8R8A8:
			srcFormat = GL_BGRA;									srcType = GL_UNSIGNED_BYTE;
			dstFormat = GL_RGBA8;
			break;

		case tPixelFormat::BC1DXT1A:
			dstFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;			compressed = true;
			break;

		case tPixelFormat::BC1DXT1:
			dstFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;			compressed = true;
			break;

		case tPixelFormat::BC2DXT2DXT3:
			dstFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;			compressed = true;
			break;

		case tPixelFormat::BC3DXT4DXT5:
			dstFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;			compressed = true;
			break;

		case tPixelFormat::BC7:
			dstFormat = GL_COMPRESSED_RGBA_BPTC_UNORM_ARB;			compressed = true;
			break;

		case tPixelFormat::BC6S:
			dstFormat = GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB;	compressed = true;
			break;

		case tPixelFormat::G3B5R5G3:
			// srcType modifies the format to the desired src format of G3B5R5G3. Usually the OpenGL driver gives
			// you a 565 format for dst. Don't know why a the exact internal format doesn't exist.
			srcFormat = GL_RGB;										srcType = GL_UNSIGNED_SHORT_5_6_5;
			dstFormat = GL_RGB5;
			break;

		case tPixelFormat::G3B5A1R5G2:
			// srcType reverses this order to ARGB, just like the PixelFormat. Cuz GL_UNSIGNED_SHORT is to be
			// interpreted as little endian, the type swaps the bytes yielding the proper G3B5A1R5G2. It is a special
			// case and applies to the entire BGRA group (unlike GL_UNSIGNED_BYTE).
			srcFormat = GL_BGRA;									srcType = GL_UNSIGNED_SHORT_1_5_5_5_REV;
			dstFormat = GL_RGB5_A1;
			break;

		case tPixelFormat::G4B4A4R4:
			// Same comment as above but yields the proper G4B4A4R4 format.
			srcFormat = GL_BGRA;									srcType = GL_UNSIGNED_SHORT_4_4_4_4_REV;
			dstFormat = GL_RGBA4;
			break;
	}
}


uint64 Image::BindThumbnail()
{
	if (!ThumbnailRequested)
		return 0;

	if (!ThumbnailThreadFlag.test_and_set())
	{
		ThumbnailThread.join();
		ThumbnailThreadRunning = false;
		ThumbnailNumThreadsRunning--;
	}

	if (ThumbnailThreadRunning)
		return 0;

	// We only ever access ThumbnailPicture once the worker thread is completed,
	// If the worker thread failed, ThumbnailPicture will be invalid and we return 0.
	if (ThumbnailInvalidateRequested)
	{
		ThumbnailRequested = false;
		ThumbnailInvalidateRequested = false;
		ThumbnailPicture.Clear();
		if (TexIDThumbnail != 0)
		{
			glDeleteTextures(1, &TexIDThumbnail);
			TexIDThumbnail = 0;
		}
		return 0;
	}

	if (ThumbnailPicture.IsValid())
	{
		if (TexIDThumbnail != 0)
		{
			glBindTexture(GL_TEXTURE_2D, TexIDThumbnail);
			return TexIDThumbnail;
		}

		glGenTextures(1, &TexIDThumbnail);
		if (TexIDThumbnail == 0)
			return 0;

		Config::ProfileData& profile = Config::GetProfileData();
		tList<tLayer> layers;
		ThumbnailPicture.GenerateLayers(layers, tResampleFilter(profile.MipmapFilter), tResampleEdgeMode::Clamp, profile.MipmapChaining);
		BindLayers(layers, TexIDThumbnail);
		return TexIDThumbnail;
	}

	return 0;
}


void Image::GenerateThumbnailBridge(Image* img)
{
	img->GenerateThumbnail();
}


void Image::GenerateThumbnail()
{
	// This thread (only) is allowed to access ThumbnailPicture. The main thread will leave it alone until GenerateThumbnail is complete.
	if (ThumbnailPicture.IsValid())
		return;

	// Retrieve from cache if possible.
	tuint256 hash = 0;
	int thumbVersion = 3;
	tFileInfo fileInfo;
	tGetFileInfo(fileInfo, Filename);
	hash = tHash::tHashData256((uint8*)&thumbVersion, sizeof(thumbVersion));
	hash = tHash::tHashString256(Filename, hash);
	hash = tHash::tHashData256((uint8*)&fileInfo.FileSize, sizeof(fileInfo.FileSize), hash);
	hash = tHash::tHashData256((uint8*)&fileInfo.CreationTime, sizeof(fileInfo.CreationTime), hash);
	hash = tHash::tHashData256((uint8*)&fileInfo.ModificationTime, sizeof(fileInfo.ModificationTime), hash);
	hash = tHash::tHashData256((uint8*)&ThumbWidth, sizeof(ThumbWidth), hash);
	hash = tHash::tHashData256((uint8*)&ThumbHeight, sizeof(ThumbHeight), hash);
	tString hashFile;
	tsPrintf(hashFile, "%s%032|256X.bin", ThumbCacheDir.Chr(), hash);
	if (tFileExists(hashFile))
	{
		bool loaded = false;
		tChunkReader chunk(hashFile);
		for (tChunk ch = chunk.First(); ch.IsValid(); ch = ch.Next())
		{
			switch (ch.ID())
			{
				case ThumbChunkInfoID:
					ch.GetItem(Cached_PrimaryWidth);
					ch.GetItem(Cached_PrimaryHeight);
					ch.GetItem(Cached_PrimaryArea);
					break;

				case tChunkID::Image_MetaData:
					Cached_MetaData.Clear();
					Cached_MetaData.Load(ch);
					break;

				case tChunkID::Image_Picture:
					ThumbnailPicture.Load(ch);
					loaded = true;
					break;
			}
		}
		if (loaded)
			return;
	}

	Image thumbLoader;
	int maxLoadAttempts = 5;
	for (int attempt = 0; attempt < maxLoadAttempts; attempt++)
	{
		if (thumbLoader.Load(Filename))
			break;
		else
			tSystem::tSleep(250);
	}
	if (!thumbLoader.IsLoaded())
		return;

	// Thumbnails are generated from the primary (first) picture in the picture list.
	tPicture* srcPic = thumbLoader.GetPrimaryPic();
	if (!srcPic)
		return;

	int srcW = srcPic->GetWidth();
	int srcH = srcPic->GetHeight();
	Cached_PrimaryWidth		= srcW;
	Cached_PrimaryHeight	= srcH;
	Cached_PrimaryArea		= srcW * srcH;

	Cached_MetaData			= thumbLoader.Cached_MetaData;

	// We make the thumbnail keep its aspect ratio.
	float scaleX = float(ThumbWidth)  / float(srcW);
	float scaleY = float(ThumbHeight) / float(srcH);
	int iw, ih;
	if (scaleX < scaleY)
	{
		iw = ThumbWidth;
		ih = int(tRound(float(srcH)*scaleX));
	}
	else
	{
		ih = ThumbHeight;
		iw = int(tRound(float(srcW)*scaleY));
	}
	tAssert((iw == ThumbWidth) || (ih == ThumbHeight));

	// Create an image that is big (or small) enough to exactly match either the width or height without ruining the aspect.
	srcPic->Resample(iw, ih, tResampleFilter::Bilinear);

	// Center-crop the image to what we need. Cropping to a bigger size adds transparent pixels.
	srcPic->Crop(ThumbWidth, ThumbHeight);

	ThumbnailPicture.Set(*srcPic);

	// Write to cache file.
	tChunkWriter writer(hashFile);
	writer.Begin(ThumbChunkInfoID);
	writer.Write(Cached_PrimaryWidth);
	writer.Write(Cached_PrimaryHeight);
	writer.Write(Cached_PrimaryArea);
	writer.Write(0x00000000);
	writer.End();

	// Only save meta-data chunk if it's valid.
	if (Cached_MetaData.IsValid())
		Cached_MetaData.Save(writer);

	ThumbnailPicture.Save(writer);
	// std::this_thread::sleep_for(std::chrono::milliseconds(100));
}


void Image::RequestThumbnail()
{
	if (ThumbnailRequested)
		return;

	// Leave one core free unless we are on a two core or lower machine, in which case we always use a min of 2 threads.
	int numThreadsMax = tClampMin((tSystem::tGetNumCores()) - 1, 2);
	if (ThumbnailNumThreadsRunning >= numThreadsMax)
		return;

	ThumbnailRequested = true;
	ThumbnailThreadRunning = true;
	ThumbnailNumThreadsRunning++;
	ThumbnailThreadFlag.test_and_set();
	ThumbnailThread = std::thread
	(
		[this]
		{
			GenerateThumbnailBridge(this);
			ThumbnailThreadFlag.clear();
		}
	);
}


void Image::UnrequestThumbnail()
{
	if (ThumbnailRequested && !ThumbnailThreadRunning && !ThumbnailPicture.IsValid())
		ThumbnailRequested = false;
}


void Image::RequestInvalidateThumbnail()
{
	if (!ThumbnailRequested)
		return;

	ThumbnailInvalidateRequested = true;
}


void Image::Play()
{
	FrameCurrCountdown = FrameDurationPreviewEnabled ? FrameDurationPreview : GetCurrentPic()->Duration;
	FramePlaying = true;
}


void Image::Stop()
{
	FramePlaying = false;
}


void Image::UpdatePlaying(float dt)
{
	if (!FramePlaying)
		return;

	int numFrames = GetNumFrames();
	if (numFrames <= 1)
		return;

	FrameCurrCountdown -= dt;
	if (FrameCurrCountdown <= 0.0f)
	{
		if (!FramePlayRev)
		{
			FrameNum++;
			if (FrameNum >= numFrames)
			{
				if (FramePlayLooping)
					FrameNum = 0;
				else
				{
					FrameNum = numFrames-1;
					FramePlaying = false;
				}
			}
		}
		else
		{
			FrameNum--;
			if (FrameNum <= 0)
			{
				if (FramePlayLooping)
					FrameNum = numFrames-1;
				else
				{
					FrameNum = 0;
					FramePlaying = false;
				}
			}
		}

		// Thanks to GitHub user Frousties for noticing animations were playing too slowly in Tacent View. The issue was
		// we weren't taking into account how much below zero we went when we detected the time was up -- the remainder.
		// The code below subtracts the remainder after clamping to deal with the case that the frameDuration is set
		// too low.
		float remainder = -FrameCurrCountdown;
		float frameDuration = FrameDurationPreviewEnabled ? FrameDurationPreview : GetCurrentPic()->Duration;
		tMath::tiClampMin(frameDuration, remainder);
		FrameCurrCountdown = frameDuration - remainder;
	}
}
