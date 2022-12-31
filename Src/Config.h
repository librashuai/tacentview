// Config.h
//
// Viewer settings stored as human-readable symbolic expressions.
//
// Copyright (c) 2019-2022 Tristan Grimmer.
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby
// granted, provided that the above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
// AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#pragma once
#include <Foundation/tString.h>
#include <Math/tColour.h>
#include <System/tScript.h>
#include "InputBindings.h"


namespace Viewer { namespace Config {


enum Category
{
	Category_None,

	Category_Unspecified			= 1 << 0,		// Everything that's not in a category below.
	Category_Display				= 1 << 1,		// Includes background and aspect settings.
	Category_Slideshow				= 1 << 2,
	Category_System					= 1 << 3,
	Category_Interface				= 1 << 4,
	Category_Bindings				= 1 << 5,		// Key bindings.

	Category_All					= 0xFFFFFFFF,
	Category_AllNoBindings			= Category_All & ~Category_Bindings
};


// The ProfileSettings struct represents a single set of all configurable parameters. There can be multiple instances for
// different profiles. Currently we only have two profiles: Main and Basic.
struct ProfileSettings
{
	ProfileSettings(Profile profile)				: Name(), InputBindings() { Reset(profile, Category_All); }
	tString Name;									// The name of the profile.

	bool ShowMenuBar;
	bool ShowNavBar;
	bool ShowImageDetails;
	bool ShowImageMetaData;
	bool ShowPixelEditor;
	bool ShowChannelFilter;
	bool ShowFrameScrubber;
	bool ShowContentView;
	bool ShowPropsWindow;
	bool ShowBindingsWindow;
	bool ShowCheatSheet;
	bool ShowAbout;

	float ThumbnailWidth;
	enum class SortKeyEnum
	{
		FileName,
		FileModTime,
		FileSize,
		FileType,
		ImageArea,
		ImageWidth,
		ImageHeight
	};
	int SortKey;								// Matches SortKeyEnum values.
	bool SortAscending;							// Sort direction.

	int OverlayCorner;
	bool Tile;

	enum class BGStyle
	{
		None,
		Checkerboard,
		SolidColour,
		NumStyles
	};
	int BackgroundStyle;
	int BackgroundCheckerboxSize;
	tColouri BackgroundColour;					// Only used if BGStyle is SolidColour.
	bool BackgroundExtend;						// Extend background past image bounds.

	enum class RetMode
	{
		AlwaysHidden,
		AlwaysVisible,
		OnSelect,								// Click outside = hide. Click inside = show. Image change = hide.
		AutoHide,								// Hidden at same time as on-screen buttons.
		NumModes
	};
	int ReticleMode;

	enum class UIMode
	{
		Small,
		Medium,
		Large,
		NumModes
	};
	int UISize;

	int ResampleFilter;							// Matches tImage::tResampleFilter. Used for image resize when saving and multiframe saving.
	int ResampleEdgeMode;						// Matches tImage::tResampleEdgeMode. Used for image resize when saving and multiframe saving.
	int ResampleFilterContactFrame;				// Matches tImage::tResampleFilter. Used for contact sheet frame resizing.
	int ResampleEdgeModeContactFrame;			// Matches tImage::tResampleEdgeMode. Used for contact sheet frame resizing.
	int ResampleFilterContactFinal;				// Matches tImage::tResampleFilter. Used for contact sheet final resizing.
	int ResampleEdgeModeContactFinal;			// Matches tImage::tResampleEdgeMode. Used for contact sheet final resizing.
	int ResampleFilterRotateUp;					// Matches tImage::tResampleFilter. Used for image rotations.
	int ResampleFilterRotateDown;				// Matches tImage::tResampleFilter. Used for image rotations.

	enum class RotMode
	{
		Fill,
		Crop,
		CropResize,
		NumModes
	};
	int RotateMode;

	enum class ZoomMode
	{
		User,
		Fit,
		DownscaleOnly,
		OneToOne,
		NumModes
	};
	int DefaultZoomMode;						// Zoom mode to use when opening a new image. User means don't change zoom.

	bool ConfirmDeletes;
	bool ConfirmFileOverwrites;
	bool SlideshowLooping;
	bool SlideshowProgressArc;
	double SlideshowPeriod;

	tString	SaveSubFolder;
	tString	SaveFileType;
	tString	SaveFileTypeMultiFrame;
	bool	SaveFileTargaRLE;
	int		SaveFilePngDepthMode;				// E [0, 2]. 0 = Auto. 1 = 24BPP. 2 = 32BPP.
	int		SaveFileQoiDepthMode;				// E [0, 2]. 0 = Auto. 1 = 24BPP. 2 = 32BPP.
	int		SaveFileQoiColourSpace;				// E [0, 1]. 0 = sRGB. 1 = Linear.
	int		SaveFileBmpDepthMode;				// E [0, 2]. 0 = Auto. 1 = 24BPP. 2 = 32BPP.
	int		SaveFileJpegQuality;				// E [1, 100]
	bool	SaveFileWebpLossy;
	float	SaveFileWebpQualComp;				// E [0.0, 100.0]
	bool	SaveFileTiffZLibDeflate;
	int		SaveFileGifBPP;						// E [1, 8]
	int		SaveFileGifQuantMethod;				// E [0, 3]
	int		SaveFileGifLoop;					// E [0, inf]
	int		SaveFileGifAlphaThreshold;			// E [-1, 255]
	float	SaveFileGifDitherLevel;				// E [0.0f, inf]
	int		SaveFileGifFilterSize;				// E [0, 2] Maps to 1, 3, 5.
	int		SaveFileGifSampleFactor;			// E [1, 10]

	int SaveFileWebpDurOverride;				// E [-1, inf]. In ms.
	int SaveFileGifDurOverride;					// E [-1, inf]. In 1/100 seconds.
	int SaveFileApngDurOverride;				// E [-1, 65535]. In ms.
	int SaveFileTiffDurOverride;				// E [-1, inf]. In ms.

	int SaveFileWebpDurMultiFrame;				// E [0, inf]. In ms.
	int SaveFileGifDurMultiFrame;				// E [0, inf]. In 1/100 seconds.
	int SaveFileApngDurMultiFrame;				// E [0, 65536]. In ms.
	int SaveFileTiffDurMultiFrame;				// E [0, inf]. In ms.
	enum class SizeMode
	{
		Percent,
		SetWidthAndHeight,
		SetWidthRetainAspect,
		SetHeightRetainAspect
	};
	int SaveAllSizeMode;
	int CropAnchor;								// E [-1, 9] with 4 being the default (middle), 0 being top-left, and -1 being 'cursor position'.
	tColouri FillColour;
	int ResizeAspectNum;
	int ResizeAspectDen;
	int ResizeAspectMode;						// 0 = Crop Mode. 1 = Letterbox Mode.
	int MaxImageMemMB;							// Max image mem before unloading images.
	int MaxCacheFiles;							// Max number of cache files before removing oldest.
	int MaxUndoSteps;
	bool StrictLoading;							// No attempt to display ill-formed images.
	bool ExifOrientLoading;						// Reorient images on load if Exif meta-data contains camera orientation information.
	bool DetectAPNGInsidePNG;					// Look for APNG data (animated) hidden inside a regular PNG file.
	int MipmapFilter;							// Matches tImage::tResampleFilter. Use None for no mipmaps.
	bool MipmapChaining;						// True for faster mipmap generation. False for a lot slower and slightly better results.
	bool AutoPropertyWindow;					// Auto display property editor window for supported file types.
	bool AutoPlayAnimatedImages;				// Automatically play animated gifs, apngs, and WebPs.
	float MonitorGamma;							// Used when displaying HDR formats to do gamma correction.
	Viewer::Bindings::InputMap InputBindings;	// Each Settings struct (profile) gets its own copy of the InputMap (key bindings).

	bool Save(tExprWriter&) const;				// Writes to the file tExprWriter was constructed with.
	void Load(tExpr);							// Reads from the expression. Pass in a tExprReader if you want to load from a file directly.

	// Yes, this struct only represents a single profile, but the defaults are different
	// depending on which profile is chosen, so we need to pass it in.
	void Reset(Viewer::Profile, uint32 categories);
};


struct GlobalSettings
{
	GlobalSettings()					{ Reset(); }

	void Save(tExprWriter&);
	void Load(tExpr);
	void Reset();

	int ConfigVersion;
	int CurrentProfile;
	int WindowX;
	int WindowY;
	int WindowW;
	int WindowH;
	bool TransparentWorkArea;
	tString LastOpenPath;
	bool FullscreenMode;

private:
	static void GetScreenSize(int& width, int& height);
};


// Loads the global settings and all profile settings from a single file.
void Load(const tString& filename);

// Saves the global settings and all profile settings in a single file.
void Save(const tString& filename);

// These apply to the current profile.
void SetProfile(Profile);
Profile GetProfile();
const char* GetProfileName();

// Current profile reset. If category is 'All' it resets all categories plus stuff not in a category.
void ResetProfile(uint32 categories);

// Resets all profiles, but only the categories specified.
void ResetAllProfiles(uint32 categories);

// Resets all profiles. All categories. Calls ResetAllProfiles with All as the category.
void ResetAllProfiles();

extern GlobalSettings Global;
extern ProfileSettings* Current;
extern ProfileSettings MainProfileSettings;
extern ProfileSettings BasicProfileSettings;


} }
