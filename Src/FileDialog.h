// FileDialog.h
//
// Dialog that allows selection of a file or directory. May be used for opening a file/directory or saving to a file.
//
// Copyright (c) 2021, 2022 Tristan Grimmer.
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
#include <System/tFile.h>
#include <System/tScript.h>
namespace tFileDialog
{


// Call to save the dialogs settings (bookmarks, current dir/file, etc) to disk. Writes to the file tExprWriter
// was constructed with. All settings are shared between all instances of FileDialog. The expressionName is
// whatever you like. It is the name of the expression you will see in the written file. You should make sure it
// doesn't collide with any other names you may be using. Use the same name when loading. Saving may be performed
// at any time, even if a dialog is open. Returns false if writing fails.
bool Save(tExprWriter&, const tString& expressionName);

// Call to load the dialog settings from disk. By passing in a tExpr you can use your own config file along with
// other expressions. This function reads from the passed-in expression. Pass in a tExprReader directly if you
// want to load from a separate file. All settings are shared between all instances of FileDialog. The
// expressionName is the name of the expression you used when saving. Make sure it doesn't collide with any other
// names you may be using. Use the same name when saving. Returns false if fails to load.
bool Load(tExpr, const tString& expressionName);

enum class DialogMode
{
	OpenFile,
	OpenDir,
	SaveFile
};

struct ContentItem;
class TreeNode;


// You can use multiple instances or repurpose the same one.
class FileDialog : public tLink<FileDialog>
{
public:
	// In OpenDir dialog mode the file-types are ignored. If file-types is empty (default) then all types are used.
	FileDialog(DialogMode, const tSystem::tFileTypes& = tSystem::tFileTypes());
	~FileDialog();

	// Call when you want the modal dialog to open.
	void OpenPopup();

	enum class DialogResult
	{
		Open,								// Modal is currently open.
		Closed,								// Modal is currently not open.
		OK,									// Call GetResult to see what the user selected.
		Cancel
	};
	DialogResult DoPopup();					// Call every frame and observe result.
	tString GetResult();

	// This struct stores shared state between all FileDialog instances. This includes things like the list of
	// bookmarks/favourites and last accessed paths.
	struct State
	{
		tList<tStringItem> Bookmarks;
		tString CurrentPath;
	};
	State SharedState;
	
private:
	tString GetDir(const TreeNode*);
	void GetDir(tList<tStringItem>& destDirItems, const TreeNode*);
	void DoSelectable(ContentItem*);

	#ifdef TACENTVIEW_BOOKMARKS
	void PopulateBookmarks();
	#endif
	void PopulateLocal();
	#ifdef PLATFORM_WINDOWS
	void PopulateNetwork();
	#endif

	// Needed in cases where we need a reload of content. For example, when changing filetype filters.
	void InvalidateAllNodeContent();
	void InvalidateAllNodeContentRecursive(TreeNode*);
	void TreeNodeRecursive(TreeNode*, tStringItem* selectPathItemName = nullptr);
	void TreeNodeFlat(TreeNode*);

	#ifdef PLATFORM_WINDOWS
	void RequestNetworkSharesThread();
	bool ProcessShareResults();
	tSystem::tNetworkShareResult NetworkShareResults;
	bool ProcessingNetworkPath = false;
	#endif

	bool PopupJustOpened;
	DialogMode Mode;
	tSystem::tFileTypes FileTypes;
	tString Result;

	#ifdef TACENTVIEW_BOOKMARKS
	TreeNode* BookmarkTreeNode;
	#endif

	TreeNode* LocalTreeNode;
	#ifdef PLATFORM_WINDOWS
	TreeNode* NetworkTreeNode;
	#endif
	TreeNode* SelectedNode = nullptr;
};


}
