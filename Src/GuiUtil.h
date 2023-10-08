// GuiUtil.h
//
// Various GUI utility functions used by many of the dialogs.
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

#pragma once
#include <Foundation/tAssert.h>
#include <Math/tVector2.h>
#include <Math/tVector4.h>
#include "Config.h"
namespace Viewer { extern Config::ProfileData::UISizeEnum CurrentUISize; }


namespace Gutil
{
	void SetWindowTitle();

	// Some window managers in Linux show an app icon (like KDE) while some don't by default (Gnome).
	// For windows, the icon is set as an exe resource, so this function does nothing on Windows.
	void SetWindowIcon(const tString& icoFile);

	// Helper to display a little (?) mark which shows a tooltip when hovered.
	void ShowHelpMark(const char* desc, bool autoWrap = true);
	void ShowToolTip(const char* desc, bool autoWrap = true);

	// This is a wrapper for ImGui::Button that also returns true if enter pressed.
	bool Button(const char* label, const tMath::tVector2& size = tMath::tVector2::zero);
	void ProgressArc(float radius, float percent, const tMath::tVector4& colour, const tMath::tVector4& colourbg, float thickness = 4.0f, int segments = 32);

	enum class DialogID
	{
		ThumbnailView,
		Properties,
		PixelEditor,
		Preferences,
		Bindings,
		MetaData,
		CheatSheet,
		About,
		ChannelFilter,
		LogOutput,
		NumIDs
	};
	tMath::tVector2 GetDialogOrigin(DialogID);

	// Returns the UI parameter based on the current UI size. You enter the base param value. For the first function,
	// GetUIParamScaled, you also enter the full-sized scale. For the second variant, GetUIParamExtent, you also enter
	// the full-sized extent. This latter function allows non-uniform scaling of multi-dimensional types.
	template<typename T> T GetUIParamScaled(const T& param, float fullScale);
	template<typename T> T GetUIParamExtent(const T& param, const T& fullParam);
}


// Implementation below this line.


template<typename T> T Gutil::GetUIParamScaled(const T& param, float fullScale)
{
	tAssert(Viewer::CurrentUISize != Viewer::Config::ProfileData::UISizeEnum::Auto);
	tAssert(Viewer::CurrentUISize != Viewer::Config::ProfileData::UISizeEnum::Invalid);
	T scaled = tMath::tLinearLookup
	(
		float(Viewer::CurrentUISize), float(Viewer::Config::ProfileData::UISizeEnum::Smallest), float(Viewer::Config::ProfileData::UISizeEnum::Largest),
		param, T(param*fullScale)
	);

	return scaled;
}


template<typename T> T Gutil::GetUIParamExtent(const T& param, const T& fullParam)
{
	tAssert(Viewer::CurrentUISize != Viewer::Config::ProfileData::UISizeEnum::Auto);
	tAssert(Viewer::CurrentUISize != Viewer::Config::ProfileData::UISizeEnum::Invalid);
	T scaled = tMath::tLinearLookup
	(
		float(Viewer::CurrentUISize), float(Viewer::Config::ProfileData::UISizeEnum::Smallest), float(Viewer::Config::ProfileData::UISizeEnum::Largest),
		param, fullParam
	);

	return scaled;
}
