// FileDialog.cpp
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

#include <Math/tVector2.h>
#include <System/tTime.h>
#include "imgui.h"
#include "FileDialog.h"
#include "TacentView.h"
#include "Image.h"
using namespace tStd;
using namespace tMath;
using namespace tSystem;
using namespace tImage;
using namespace tInterface;


TreeNode* TreeNode::Find(const tString& name) const
{
	for (tItList<TreeNode>::Iter child = Children.First(); child; child++)
	{
		#ifdef PLATFORM_WINDOWS
		if (child->Name.IsEqualCI(name))
		#else
		if (child->Name == name)
		#endif
			return child;
	}
	return nullptr;
}


int TreeNode::Depth() const
{
	int depth = 0;
	TreeNode* parent = Parent;
	while (parent)
	{
		depth++;
		parent = parent->Parent;
	}
	return depth;
}


TreeNode::ContentItem* TreeNode::FindSelectedItem() const
{
	for (ContentItem* item = Contents.First(); item; item = item->Next())
		if (item->Selected)
			return item;

	return nullptr;
}


bool TreeNode::IsNetworkLocation() const
{
	bool isNet = false;
	const TreeNode* curr = this;
	while (curr)
	{
		if ((curr->Depth() == 0) && (curr->Name == "Network"))
			isNet = true;
		curr = curr->Parent;
	}
	return isNet;
}


TreeNode::ContentItem::ContentItem(const tSystem::tFileInfo& fileInfo) :
	Selected(false)
{
	Name = tSystem::tGetFileName(fileInfo.FileName);
	IsDir = fileInfo.Directory;

	tsPrintf(FileSizeString, "%'d Bytes", fileInfo.FileSize);
	tsPrintf(ModTimeString, "%s", tSystem::tConvertTimeToString(tSystem::tConvertTimeToLocal(fileInfo.ModificationTime)).Chars());
}


FileDialog::FileDialog(DialogMode mode) :
	Mode(mode)
{
	FavouritesTreeNode = new TreeNode("Favourites", this);
	LocalTreeNode = new TreeNode("Local", this);
	#ifdef PLATFORM_WINDOWS
	NetworkTreeNode = new TreeNode("Network", this);
	#endif

	PopulateFavourites();
	PopulateLocal();
	#ifdef PLATFORM_WINDOWS
	PopulateNetwork();
	#endif
}


void FileDialog::OpenPopup()
{
	ImGui::SetNextWindowSize(tVector2(600.0f, 400.0f), ImGuiCond_FirstUseEver);
	switch (Mode)
	{
		case DialogMode::OpenDir:
			ImGui::OpenPopup("Open Directory");
			break;

		case DialogMode::OpenFile:
			ImGui::OpenPopup("Open File");
			break;

		case DialogMode::SaveFile:
			ImGui::OpenPopup("Save File");
			break;
	}
}


void FileDialog::PopulateFavourites()
{
	TreeNode* favouriteA = new TreeNode("FavouriteA", this, FavouritesTreeNode);
	FavouritesTreeNode->AppendChild(favouriteA);
}


void FileDialog::PopulateLocal()
{
	#if defined(PLATFORM_WINDOWS)
	tList<tStringItem> drives;
	tSystem::tGetDrives(drives);

	for (tStringItem* drive = drives.First(); drive; drive = drive->Next())
	{
		TreeNode* driveNode = new TreeNode(*drive, this, LocalTreeNode);
		LocalTreeNode->AppendChild(driveNode);	
	}
	#endif
}


#ifdef PLATFORM_WINDOWS
void FileDialog::RequestNetworkSharesThread()
{
	tSystem::tGetNetworkShares(NetworkShareResults);
}


void FileDialog::PopulateNetwork()
{
	// Start the request on a different thread.
	std::thread threadGetShares(&FileDialog::RequestNetworkSharesThread, this);
	threadGetShares.detach();
}


void FileDialog::ProcessShareResults()
{
	// Most of time this will be empty since we remove the items when they are available, just like a message queue.
	tStringItem* share = NetworkShareResults.ShareNames.Remove();
	if (share)
	{
		tList<tStringItem> exploded;
		tExplodeShareName(exploded, *share);
		tString machName = *exploded.First();
		if (!NetworkTreeNode->Contains(machName))
		{
			TreeNode* machine = new TreeNode(machName.Text(), this, NetworkTreeNode);
			NetworkTreeNode->AppendChild(machine);
		}

		TreeNode* machNode = NetworkTreeNode->Find(machName);
		tAssert(machNode);
		tStringItem* shareName = (exploded.GetNumItems() == 2) ? exploded.Last() : nullptr;

		if (shareName && !machNode->Contains(*shareName))
		{
			TreeNode* shareNode = new TreeNode(shareName->Text(), this, machNode);
			machNode->AppendChild(shareNode);
		}
		delete share;
	}
}
#endif


void FileDialog::TreeNodeRecursive(TreeNode* node)
{
	int flags =
		ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DefaultOpen |
		((node->Children.GetNumItems() == 0) ? ImGuiTreeNodeFlags_Leaf : 0) |
		((SelectedNode == node) ? ImGuiTreeNodeFlags_Selected : 0);

	bool populate = false;

	// This is the magic that's needed if you select from the right-hand panel.
	if (node->NextOpen)
	{
		ImGui::SetNextTreeNodeOpen(true);
		node->NextOpen = false;
		populate = true;
	}

	bool isOpen = ImGui::TreeNodeEx(node->Name, flags);
	bool isClicked = ImGui::IsItemClicked();
	if (isClicked)
	{
		SelectedNode = node;
		populate = true;
	}

	// We only bother populating children if we need to.
	if (populate)
	{
		if (!node->ChildrenPopulated)
		{
			if (!ProcessingNetworkPath || (node->Depth() >= 2))
			{
				// Need to be careful here. tFindDirs would (reasonably?) use the current working dir if we passed in an empty string.
				tString currDir = GetSelectedDir();
				tList<tStringItem> foundDirs;
				if (!currDir.IsEmpty())
					tSystem::tFindDirs(foundDirs, currDir);
				for (tStringItem* dir = foundDirs.First(); dir; dir = dir->Next())
				{
					tString relDir = tSystem::tGetRelativePath(currDir, *dir);
					relDir.ExtractLeft("./");
					relDir.ExtractRight("/");

					TreeNode* child = new TreeNode(relDir, this, node);
					node->AppendChild(child);
				}
				node->ChildrenPopulated = true;
			}
		}
	}

	if (isOpen)
	{
		// Recurse children.
		for (tItList<TreeNode>::Iter child = node->Children.First(); child; child++)
			TreeNodeRecursive(child.GetObject());

		ImGui::TreePop();
	}
}


void FileDialog::DoSelectable(const char* label, TreeNode::ContentItem* item)
{
	if (ImGui::Selectable(label, &item->Selected))
	{
		// This block enforces single selection.
		if (Mode != DialogMode::OpenFiles)
		{
			if (item->Selected)
			{
				// Only allowed one selected max. Clear the others.
				for (TreeNode::ContentItem* i = SelectedNode->Contents.First(); i; i = i->Next())
				{
					if (i == item)
						continue;
					i->Selected = false;
				}
			}

			// Do not allow deselection of the single item.
			item->Selected = true;
		}

		// If a directory is selected in the right panel, we support updating the current selected TreeNode.
		if (item->IsDir)
		{
			SelectedNode->NextOpen = true;
			TreeNode* node = SelectedNode->Find(item->Name);
			if (!node)
			{
				// Need to create a new one and connect it up.
				node = new TreeNode(item->Name, this, SelectedNode);
				SelectedNode->AppendChild(node);
				tAssert(node->ChildrenPopulated == false);
			}
			SelectedNode = node;
		}
	}
}


void FileDialog::FavouritesTreeNodeFlat(TreeNode* node)
{
	int flags = (node->Children.GetNumItems() == 0) ? ImGuiTreeNodeFlags_Leaf : 0;
	if (SelectedNode == node)
		flags |= ImGuiTreeNodeFlags_Selected;
	flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

	bool isOpen = ImGui::TreeNodeEx(node->Name, flags);
	bool isClicked = ImGui::IsItemClicked();

	if (isOpen)
	{
		// Recurse children.
		for (tItList<TreeNode>::Iter child = node->Children.First(); child; child++)
			FavouritesTreeNodeFlat(child.GetObject());

		ImGui::TreePop();
	}
}


FileDialog::DialogResult FileDialog::DoPopup()
{
	// The unused isOpen bool is just so we get a close button in ImGui. 
	bool isOpen = true;
	const char* label = nullptr;
	switch (Mode)
	{
		case DialogMode::OpenFile:		label = "Open File";		break;
		case DialogMode::OpenFiles:		label = "Open Files";		break;
		case DialogMode::OpenDir:		label = "Open Directory";	break;
		case DialogMode::SaveFile:		label = "Save File";		break;
	}
	if (!ImGui::BeginPopupModal(label, &isOpen, ImGuiWindowFlags_NoScrollbar /*ImGuiWindowFlags_AlwaysAutoResize*/))
		return DialogResult::Closed;

	DialogResult result = DialogResult::Open;
	Result.Clear();

	// The left and right panels are cells in a 1 row, 2 column table.
	float bottomBarHeight = 20.0f;
	if (ImGui::BeginTable("FileDialogTable", 2, ImGuiTableFlags_Resizable, tVector2(0.0f, -bottomBarHeight)))
	{
		ImGui::TableSetupColumn("LeftTreeColumn", ImGuiTableColumnFlags_WidthFixed, 185.0f);
		ImGui::TableSetupColumn("RightContentColumn", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();

		// Left tree panel.
		ImGui::TableSetColumnIndex(0);
		ImGui::BeginChild("LeftTreePanel", tVector2(0.0f, -bottomBarHeight));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, tVector2(0.0f, 3.0f));

		// This block is the workhorse of the dialog.
		FavouritesTreeNodeFlat(FavouritesTreeNode);
		TreeNodeRecursive(LocalTreeNode);
		#ifdef PLATFORM_WINDOWS
		ProcessShareResults();
		ProcessingNetworkPath = true;
		TreeNodeRecursive(NetworkTreeNode);
		ProcessingNetworkPath = false;
		#endif

		ImGui::PopStyleVar();
		ImGui::EndChild();
		
		// Right content panel.
		ImGui::TableSetColumnIndex(1);
		ImGui::BeginChild("RightContentPanel", tVector2(0.0f, -bottomBarHeight));

		if (SelectedNode)
		{
			if (!SelectedNode->ContentsPopulated)
			{
				tString selDir = GetSelectedDir();

				// Directories.
				tList<tStringItem> foundDirs;
				if (!selDir.IsEmpty())
					tSystem::tFindDirs(foundDirs, selDir);
				for (tStringItem* dir = foundDirs.First(); dir; dir = dir->Next())
				{
					(*dir)[dir->Length()-1] = '\0';				// Remove slash.
					TreeNode::ContentItem* contentItem = new TreeNode::ContentItem(tSystem::tGetFileName(*dir), true);
					SelectedNode->Contents.Append(contentItem);
				}

				// Files.
				tList<tFileInfo> foundFiles;
				if (!selDir.IsEmpty())
					tSystem::tFindFilesFast(foundFiles, selDir);
				for (tFileInfo* fileInfo = foundFiles.First(); fileInfo; fileInfo = fileInfo->Next())
				{
					TreeNode::ContentItem* contentItem = new TreeNode::ContentItem(*fileInfo);
					SelectedNode->Contents.Append(contentItem);
				}

				SelectedNode->ContentsPopulated = true;
			}

			if (ImGui::BeginTable("ContentItems", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings))
			{
				for (TreeNode::ContentItem* item = SelectedNode->Contents.First(); item; item = item->Next())
				{
					ImGui::TableNextRow();

					ImGui::TableNextColumn();
					DoSelectable(item->Name.Chars(), item);

					ImGui::TableNextColumn();
					tString modTime("<DIR>");
					if (!item->IsDir)
						tsPrintf(modTime, "%s##%s", item->ModTimeString.Text(), item->Name.Text());
					DoSelectable(modTime, item);

					ImGui::TableNextColumn();
					tString numBytes;
					if (!item->IsDir)
						tsPrintf(numBytes, "%s##%s", item->FileSizeString.Text(), item->Name.Text());
					DoSelectable(numBytes, item);
				}
				ImGui::EndTable();
			}
		}

		ImGui::EndChild();
		ImGui::EndTable();
	}

	if (ImGui::Button("Cancel", tVector2(100.0f, 0.0f)))
		result = DialogResult::Cancel;

	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - 100.0f);

	TreeNode::ContentItem* selItem = SelectedNode ? SelectedNode->FindSelectedItem() : nullptr;
	bool showOK = false;
	if (selItem)
	{
		showOK =
		(
			((Mode == DialogMode::OpenFile) && !selItem->IsDir) ||
			((Mode == DialogMode::SaveFile) && !selItem->IsDir) ||
			((Mode == DialogMode::OpenDir) && selItem->IsDir)
			// @todo multiple files.
		);
	}

	if (!showOK && (Mode == DialogMode::OpenDir) && SelectedNode)
	{
		// OK to select dir in left-hand pane.
		int depth = SelectedNode->Depth();
		showOK =
		(
			(depth > 1) ||
			(!SelectedNode->IsNetworkLocation() && (depth > 0))
		);		
	}

	if (showOK)
	{
		if (ImGui::Button("OK", tVector2(100.0f, 0.0f)))
		{
			Result = GetSelectedDir();
			if ((Mode == DialogMode::OpenFile) && selItem)
				Result += selItem->Name;
			result = DialogResult::OK;
		}
	}

	if (result != DialogResult::Open)
		ImGui::CloseCurrentPopup();

	ImGui::EndPopup();
	return result;
}


tString FileDialog::GetResult()
{
	return Result;
}


tString FileDialog::GetSelectedDir()
{
	if (!SelectedNode)
		return tString();
	tString dir;
//	TreeNode* curr = SelectedNode;
//	while (curr)
//	{
//		if ((curr->Depth() == 0) && (curr->Name == "Network"))
//			isNetworkLoc = true;
//		curr = curr->Parent;
//	}
	bool isNetworkLoc = SelectedNode->IsNetworkLocation();
	TreeNode* curr = SelectedNode;
	while (curr)
	{
		if (!curr->Name.IsEmpty() && (curr->Depth() > 0))
		{
			if (isNetworkLoc && (curr->Depth() == 1))
				dir = curr->Name + "\\" + dir;
			else
				dir = curr->Name + "/" + dir;
		}
		curr = curr->Parent;
	}
	if (isNetworkLoc)
		dir = tString("\\\\") + dir;

	return dir;
}
