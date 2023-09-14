// Profile.h
//
// The viewer profile enum.
//
// Copyright (c) 2022, 2023 Tristan Grimmer.
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby
// granted, provided that the above copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
// AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#pragma once


namespace Viewer
{
	enum class eProfile
	{
		Main,
		Basic,
		Kiosk,
		NumProfiles,
		Invalid = NumProfiles
	};

	const char* GetProfileName(eProfile);
	const char* GetProfileNameLong(eProfile);

	extern const char* ProfileNames[];
	extern const char* ProfileNamesLong[];
}


// Implementation only below.


inline const char* Viewer::GetProfileName(eProfile profile)
{
	return ProfileNames[int(profile)];
}


inline const char* Viewer::GetProfileNameLong(eProfile profile)
{
	return ProfileNamesLong[int(profile)];
}
