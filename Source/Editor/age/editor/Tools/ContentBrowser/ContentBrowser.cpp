#include <age/editor/Tools/ContentBrowser/ContentBrowser.h>

#include <string>
#include <mutex>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cctype>

#include <imgui.h>
#include <age/editor/imgui_widgets/imgui_widgets.h>
#include <age/settings/settings.h>
#include <age/log/Log.h>
#include <age/scene/SceneSerialize.h>

#include <age/editor/Editor.h>
#include <age/editor/AnimationClip/AnimationClipDocument.h>
#include <age/editor/ObjectDefinition/ObjectDefinitionDocument.h>
#include <age/editor/Scene/SceneDocument.h>
#include <age/editor/Material/MaterialDocument.h>
#include <age/editor/Import/FbxConvert.h>
#include <age/editor/Tools/ContentBrowser/AssetFileCommands.h>
#include <age/editor/CommandManager/CommandManager.h>

#include <IconFontHeaders/IconsLucide.h>

using namespace Ag;

static fs::path _current_path;

namespace Ag
{
	struct DirectoryCache
	{
		std::vector<fs::path> directories;
		std::vector<fs::path> files;
	};

	struct FileHierarchyCache
	{
		fs::path root;

		std::unordered_map<fs::path, DirectoryCache> activeCache;
		std::unordered_map<fs::path, DirectoryCache> pendingCache;

		// Main-thread-only copy of activeCache, taken once per Draw() under
		// a brief lock (see Draw()'s opening lines) so the rest of the frame
		// -- ImGui widgets, thumbnail loads, document opens on double-click,
		// all of which can take far longer than a map copy -- reads this
		// instead of holding isAccessingCache for the whole draw and
		// starving the background scan thread's next swap.
		std::unordered_map<fs::path, DirectoryCache> drawSnapshot;

		std::mutex isAccessingCache;
		std::atomic<bool> shutDownUpdate;

		std::thread cacheThread;
	};
}

void UpdateCacheThread(FileHierarchyCache* cache)
{
	while (!cache->shutDownUpdate)
	{

		{
			std::vector<fs::path> pending;

			{
				std::lock_guard guard(cache->isAccessingCache);

				if (!cache->root.empty())
					pending.push_back(cache->root);
			}

			while (!pending.empty())
			{
				fs::path current = pending.back();
				pending.pop_back();

				DirectoryCache& dirCache = cache->pendingCache[current];

				for (fs::directory_entry item : fs::directory_iterator(current))
				{
					if (item.is_directory())
					{
						// DeleteAssetCommand's undo trash, one folder at the asset
						// root mirroring each deleted file's own relative path
						// underneath it (see AssetFileCommands.h) -- not a real
						// asset location, never shown.
						if (item.path().filename() == kAssetTrashFolderName)
						{
							continue;
						}
						if (item.path().extension() == ".leveldata")
						{
							continue;
						}

						dirCache.directories.push_back(item.path());
						pending.push_back(item.path());
					}
					else
					{
						// A material graph's sidecar is pure metadata for the
						// Graph tab (see MaterialDocument/MaterialGraph) --
						// the .tgmat and whatever real _C/_N/_M/_FX.dds it
						// bakes are the actual visible assets. Same idea as
						// hiding .leveldata above: nothing else ever opens or
						// references a .tgmatgraph directly.
						if (item.path().extension() == ".tgmatgraph")
							continue;
						dirCache.files.push_back(item.path());
					}
				}
			}
			for (auto& [path, directory] : cache->pendingCache)
			{
				std::ranges::sort(directory.directories);
				std::ranges::sort(directory.files);
			}

			{
				std::lock_guard guard(cache->isAccessingCache);
				std::swap(cache->pendingCache, cache->activeCache);
			}
			cache->pendingCache.clear();
		}

		{
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(1s);
		}
	}
}

ContentBrowser::ContentBrowser()
{
	myCache = std::make_unique<FileHierarchyCache>();

	myCache->cacheThread = std::thread(UpdateCacheThread, myCache.get());
}

ContentBrowser::~ContentBrowser()
{
	myCache->shutDownUpdate = true;
	myCache->cacheThread.join();
}

void ContentBrowser::SetPath(const std::string_view& aPath) 
{
	_current_path = fs::absolute(aPath);

	std::lock_guard guard(myCache->isAccessingCache);
	myCache->root = fs::absolute(aPath);
}

StringId ContentBrowser::GetSelectedAsset()
{
	return StringRegistry::RegisterOrGetString(mySelectedPath.string());
}

void ContentBrowser::ListAssets(std::span<const char* const> extensions, std::vector<std::string>& out) const
{
	const fs::path root = fs::absolute(Ag::Settings::GameAssetRoot());
	std::lock_guard guard(myCache->isAccessingCache);
	for (const auto& [directory, cache] : myCache->activeCache)
	{
		for (const fs::path& file : cache.files)
		{
			const std::string extension = file.extension().string();
			for (const char* wanted : extensions)
			{
				if (extension == wanted)
				{
					out.push_back(fs::relative(file, root).string());
					break;
				}
			}
		}
	}
}

fs::path ContentBrowser::GetCurrentFolder() const
{
	return _current_path;
}

void ContentBrowser::DrawFileTree(const fs::path& parentPath)
{
	auto parentIt = myCache->drawSnapshot.find(parentPath);

	if (parentIt == myCache->drawSnapshot.end())
		return;

	const DirectoryCache& parentCache = parentIt->second;

	for (fs::path path : parentCache.directories)
	{
		auto it = myCache->drawSnapshot.find(path);
		if (it == myCache->drawSnapshot.end())
			continue;

		const DirectoryCache& cache = it->second;

		ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Leaf;

		// A scene's .leveldata folder is its own storage, not something to browse.
		if (path.extension() == ".leveldata")
		{
			continue;
		}

		/////////////////////////////////////////////////////////////
		// need to know if there are sub-folders, if not it is a leaf

		if (!cache.directories.empty())
			node_flags ^= ImGuiTreeNodeFlags_Leaf;

		if (path == _current_path) {
			node_flags |= ImGuiTreeNodeFlags_Selected;
		}

		bool node_open = ImGui::TreeNodeEx(path.filename().string().c_str(), node_flags);
		if (ImGui::IsItemClicked()) {
			_current_path = path;
		}
		if (ImGui::BeginDragDropTarget())
		{
			const char* types[] = { ".tgo", ".tgs", ".tgm", ".tgmat", ".tgac", ".tgps", ".dds", ".fbx" };
			for (const char* type : types)
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(type))
				{
					fs::path source = fs::path(Ag::Settings::GameAssetRoot()) / static_cast<const char*>(payload->Data);
					fs::path destination = path / source.filename();
					if (source != destination && !fs::exists(destination))
					{
						auto command = std::make_shared<RenameAssetCommand>(source, destination);
						CommandManager::DoCommand(command);
						if (!command->GetError().empty()) myAssetOperationError = command->GetError();
					}
					break;
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (node_open) 
		{
			DrawFileTree(path);
			
			ImGui::TreePop();
		}
	}
}

void ContentBrowser::Draw()
{
	// Copy, don't hold: the previous version locked isAccessingCache for
	// this entire function, which runs ImGui widgets, thumbnail loads and
	// document-open calls on double-click -- all far slower than a map
	// copy, and all blocking the background scan thread's next cache swap
	// for as long as they took. Everything below reads drawSnapshot, a
	// plain value copy only this (main) thread ever touches, instead.
	{
		std::lock_guard guard(myCache->isAccessingCache);
		myCache->drawSnapshot = myCache->activeCache;
	}

	{
		std::string result;
		if (myConvertRunner.PollResult(result))
		{
			myAssetOperationError = result;
			// The editor keeps every prefab loaded in memory, so a prefab that was
			// already open when the converter rewrote its file (Bistro.tgo with no
			// material list, say) would keep showing the old contents -- and saving
			// it would overwrite the generated one. Safe to attempt even after a
			// failed cook: Reload() keeps the existing copy if the file won't parse.
			if (!myConvertPrefab.empty())
				Editor::GetEditor()->GetSceneObjectDefinitionManager().Reload(myConvertPrefab);
			myConvertPrefab.clear();
		}
	}

	ImGui::SetNextWindowClass(Editor::GetEditor()->GetGlobalWindowClass());
	ImGui::Begin("Content Browser");

	// Top bar: Add, then where you are.
	if (ImGui::Button(ICON_LC_PLUS " Add"))
		ImGui::OpenPopup("AddAssetMenu");
	if (ImGui::BeginPopup("AddAssetMenu"))
	{
		DrawAddMenuItems();
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	DrawBreadcrumbs();
	if (!myAssetOperationError.empty())
		ImGui::TextColored(ImVec4(1.f, .45f, .25f, 1.f), "%s", myAssetOperationError.c_str());
	DrawCreatePopup();
	ImGui::Separator();

	// Folders on the left, what is in the current folder on the right.
	if (ImGui::BeginTable("##ContentBrowserBody", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableSetupColumn("Folders", ImGuiTableColumnFlags_WidthFixed, 220.f);
		ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 6.f));
		ImGui::BeginChild("##Folders", ImVec2(0.f, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_AlwaysUseWindowPadding);
		ImGui::PopStyleVar();
	{
		ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_SpanAvailWidth;
		node_flags |= ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;

		if (ImGui::TreeNodeEx("Game", node_flags))
		{
			fs::path path = fs::absolute(Ag::Settings::GameAssetRoot());
			if (ImGui::IsItemClicked()) {
				_current_path = path;
			}
			DrawFileTree(path.string().c_str());
			ImGui::TreePop();
		}
	}
	ImGui::EndChild();

	ImGui::TableSetColumnIndex(1);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));
	ImGui::BeginChild("##Assets", ImVec2(0.f, ImGui::GetContentRegionAvail().y), ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::PopStyleVar();
	{
		// Set when the pointer is over a tile or folder, so an item's right-click menu and the
		// empty-space menu never open on the same click.
		bool pointerOverAsset = false;
		if (ImGui::Button(myGridView ? ICON_LC_LIST : ICON_LC_LAYOUT_GRID))
			myGridView = !myGridView;
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip(myGridView ? "Switch to list view" : "Switch to grid view");
		ImGui::SameLine();
		if (myGridView)
		{
			ImGui::SetNextItemWidth(110.f);
			ImGui::SliderFloat("##TileSize", &myGridTileSize, 72.f, 200.f, "%.0f");
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip("Thumbnail size");
			ImGui::SameLine();
		}
		ImGui::SetNextItemWidth(-105.f);
		ImGui::InputTextWithHint("##AssetSearch", ICON_LC_SEARCH " Search assets", mySearchBuffer, IM_ARRAYSIZE(mySearchBuffer));
		ImGui::SameLine();
		const char* assetTypes[] = { "All", "Materials", "Textures", "Scenes", "Meshes", "Other" };
		ImGui::SetNextItemWidth(100.f);
		ImGui::Combo("##AssetType", &myAssetTypeFilter, assetTypes, IM_ARRAYSIZE(assetTypes));
		const std::string search = mySearchBuffer;
		const auto matchesFilter = [this](const fs::path& path)
		{
			const std::string extension = path.extension().string();
			switch (myAssetTypeFilter)
			{
			case 1: return extension == ".tgmat";
			case 2: return extension == ".dds";
			case 3: return extension == ".tgs";
			case 4: return extension == ".tgm" || extension == ".fbx";
			case 5: return extension != ".tgmat" && extension != ".dds" && extension != ".tgs" && extension != ".tgm" && extension != ".fbx";
			default: return true;
			}
		};
		const auto matchesSearch = [&search, &matchesFilter](const fs::path& path)
		{
			if (!matchesFilter(path)) return false;
			if (search.empty()) return true;
			std::string name = path.filename().string();
			std::string needle = search;
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return name.find(needle) != std::string::npos;
		};
		ImGui::Separator();
		auto parentIt = myCache->drawSnapshot.find(_current_path);

		if (parentIt != myCache->drawSnapshot.end())
		{

			const DirectoryCache& parentCache = parentIt->second;

			// Sub-folders first, as tiles, like the Unreal Content Browser. Hidden while searching or filtering.
			if (search.empty() && myAssetTypeFilter == 0)
			{
				for (const fs::path& directory : parentCache.directories)
				{
					if (directory.extension() == ".leveldata")
						continue;
					ImGui::PushID(directory.generic_string().c_str());
					const fs::path relativeDirectory = fs::relative(directory, Ag::Settings::GameAssetRoot());
					const Ag::AssetListItemStatus folderStatus = myGridView
						? Ag::AssetGridItem(relativeDirectory, false, ICON_LC_FOLDER, (ImTextureID)0, myGridTileSize)
						: Ag::AssetListItem(relativeDirectory, false, ICON_LC_FOLDER);
					pointerOverAsset |= ImGui::IsItemHovered();
					if (folderStatus.doubleClicked)
						_current_path = directory;
					if (myGridView)
					{
						const float nextTileRight = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + myGridTileSize;
						if (nextTileRight < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
							ImGui::SameLine();
					}
					ImGui::PopID();
				}
				if (myGridView)
					ImGui::NewLine();
			}

			for (fs::path absPath : parentCache.files)
			{
				if (!matchesSearch(absPath))
					continue;
				// Use a stable, per-asset scope for menu and row controls.
				ImGui::PushID(absPath.generic_string().c_str());
				{
					const std::string& root = Ag::Settings::GameAssetRoot();
					fs::path path = fs::relative(absPath, root);

					bool isSelected = mySelectedPath == path;
					Ag::AssetListItemStatus itemStatus{};

					const std::string extension = path.extension().string();
					std::string icon = ICON_LC_FILE;
					if (extension == ".tgmat") icon = ICON_LC_PALETTE;
					else if (extension == ".dds") icon = ICON_LC_IMAGE;
					else if (extension == ".tgs") icon = ICON_LC_MAP;
					else if (extension == ".tgm" || extension == ".fbx") icon = ICON_LC_CUBOID;
					else if (extension == ".tgo" || extension == ".tgac") icon = ICON_LC_FILE_CODE;
					else if (extension == ".tgps") icon = ICON_LC_SPARKLES;

					if (path.extension() == ".dds")
					{
						ImTextureID img = Editor::GetEditor()->GetEditorGraphics().GetTextureID(path.string());
						if (img)
						{
							itemStatus = myGridView
								? Ag::AssetGridItem(path, isSelected, "", img, myGridTileSize)
								: Ag::AssetListItem(path, isSelected, "", img, myThumbSize);
						}
					}
					else
					{
						itemStatus = myGridView
							? Ag::AssetGridItem(path, isSelected, icon, (ImTextureID)0, myGridTileSize)
							: Ag::AssetListItem(path, isSelected, icon);

						if (path.extension() == ".tgs")
						{
							if (itemStatus.doubleClicked)
							{
								// The editor always has one level open; this replaces it.
								Editor::GetEditor()->OpenLevel(path);
							}
						}

						if (absPath.extension() == ".tgo")
						{
							if (itemStatus.doubleClicked)
							{
								// todo: check if already open!
								// move this logic somewhere else?

								std::unique_ptr<ObjectDefinitionDocument> sceneDocument = std::make_unique<ObjectDefinitionDocument>();
								sceneDocument->Init(path.string());

								Editor::GetEditor()->AddDocument(std::move(sceneDocument));
							}
						}

						if (absPath.extension() == ".tgac")
						{
							if (itemStatus.doubleClicked)
							{
								// todo: check if already open!
								// move this logic somewhere else?

								std::unique_ptr<AnimationClipDocument> document = std::make_unique<AnimationClipDocument>();
								document->Init(path.string());

								Editor::GetEditor()->AddDocument(std::move(document));
							}
						}

						if (absPath.extension() == ".tgmat")
						{
							if (itemStatus.doubleClicked)
							{
								// Material preview setup touches graphics resources and can throw
								// (for example when a shader/model asset is unavailable). Do not
								// let an asset-browser double click tear down the entire editor.
								try
								{
									std::unique_ptr<MaterialDocument> document = std::make_unique<MaterialDocument>();
									document->Init(path.string());
									Editor::GetEditor()->AddDocument(std::move(document));
								}
								catch (const std::exception& e)
								{
									ERROR_PRINT("Material editor: could not open '%s': %s", path.string().c_str(), e.what());
								}
								catch (...)
								{
									ERROR_PRINT("Material editor: could not open '%s': unknown exception", path.string().c_str());
								}
							}
						}
						if (absPath.extension() == ".tgm" && itemStatus.doubleClicked)
						{
							fs::path fbx = absPath;
							fbx.replace_extension(".fbx");
							if (fs::exists(fbx))
								OpenFbxConvertDialog(fbx);
						}
					}
					pointerOverAsset |= ImGui::IsMouseHoveringRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()) || itemStatus.contextClicked;
					if (itemStatus.selectedAfter)
						mySelectedPath = path;

					if (itemStatus.contextClicked)
					{
						mySelectedPath = path;
						ImGui::OpenPopup("AssetContext");
					}
					if (ImGui::BeginPopup("AssetContext"))
					{
			ImGui::TextDisabled("%s", path.filename().string().c_str());
						ImGui::Separator();
						if (ImGui::MenuItem("Copy Path"))
							ImGui::SetClipboardText(path.string().c_str());
						if (ImGui::MenuItem("Select"))
							mySelectedPath = path;
						if (absPath.extension() == ".fbx" &&
							ImGui::MenuItem(myConvertRunner.IsRunning() ? "Converting..." : "Convert to TGO...", nullptr, false, !myConvertRunner.IsRunning()))
							OpenFbxConvertDialog(absPath);
						if (ImGui::MenuItem("Delete"))
						{
							myPendingDelete = absPath;
						}
						ImGui::EndPopup();
					}

					// Flow-wrap tiles left to right: keep going on the same
					// row as long as the next tile would still fit, otherwise
					// let the next iteration fall through to a new line.
					if (myGridView)
					{
						const float nextTileRight = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + myGridTileSize;
						if (nextTileRight < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
							ImGui::SameLine();
					}
				}
				ImGui::PopID();
			}
		}

	if (myPendingDelete.empty() && !mySelectedPath.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
		&& !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
		myPendingDelete = fs::absolute(fs::path(Ag::Settings::GameAssetRoot()) / mySelectedPath);

	if (!myPendingDelete.empty()) ImGui::OpenPopup("Confirm Asset Delete");
	if (ImGui::BeginPopupModal("Confirm Asset Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped("Delete '%s'?", myPendingDelete.filename().string().c_str());
		if (ImGui::Button("Delete", ImVec2(120.f, 0.f)))
		{
			auto command = std::make_shared<DeleteAssetCommand>(myPendingDelete, fs::absolute(Ag::Settings::GameAssetRoot()));
			CommandManager::DoCommand(command);
			if (!command->GetError().empty()) myAssetOperationError = command->GetError();
			else if (mySelectedPath == fs::relative(myPendingDelete, Ag::Settings::GameAssetRoot())) mySelectedPath.clear();
			myPendingDelete.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.f, 0.f))) { myPendingDelete.clear(); ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}

	// Right-click on empty space: the same things the Add button makes.
	if (!pointerOverAsset && ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
		ImGui::OpenPopup("##ContentBrowserContext");
	if (ImGui::BeginPopup("##ContentBrowserContext"))
	{
		DrawAddMenuItems();
		ImGui::EndPopup();
	}
	}
	ImGui::EndChild();
	ImGui::EndTable();
	}
	ImGui::End();

	// Outside the window, so the dialog's popup id is the same no
	// matter which one opened it.
	FbxCookRequest requestFromDialog;
	if (myFbxDialog.Draw(requestFromDialog))
		StartFbxConversion(requestFromDialog);
}

void ContentBrowser::DrawBreadcrumbs()
{
	const fs::path root = fs::absolute(Ag::Settings::GameAssetRoot());

	// The folder tree builds its paths from directory listings while the root comes from the
	// settings, so the same folder can differ in slashes or case. Compare the folders themselves.
	std::error_code error;
	std::vector<fs::path> chain;
	bool reachedRoot = false;
	for (fs::path path = _current_path;; path = path.parent_path())
	{
		chain.push_back(path);
		if (fs::equivalent(path, root, error))
		{
			reachedRoot = true;
			break;
		}
		if (path == path.parent_path())
			break;
	}
	if (!reachedRoot)
	{
		// Somewhere outside the game folder: start again at the root.
		_current_path = root;
		chain = { root };
	}
	std::reverse(chain.begin(), chain.end());

	for (size_t i = 0; i < chain.size(); ++i)
	{
		ImGui::PushID((int)i);
		const std::string label = i == 0 ? std::string(ICON_LC_HOUSE " Game") : chain[i].filename().string();
		if (ImGui::SmallButton(label.c_str()))
			_current_path = chain[i];
		ImGui::PopID();
		if (i + 1 < chain.size())
		{
			ImGui::SameLine(0.f, 4.f);
			ImGui::TextDisabled(">");
			ImGui::SameLine(0.f, 4.f);
		}
	}
}

void ContentBrowser::RequestNewLevel()
{
	RequestCreate(CreateKind::Level);
}

void ContentBrowser::RequestCreate(CreateKind kind)
{
	static const char* defaults[] = { "", "NewFolder", "NewTGO", "NewLevel", "NewMaterial", "NewAnimationClip", "NewParticleSystem" };
	myCreateKind = kind;
	myOpenCreatePopup = true;
	myAssetOperationError.clear();
	strncpy_s(myCreateNameBuffer, defaults[(int)kind], _TRUNCATE);
}

void ContentBrowser::DrawAddMenuItems()
{
	ImGui::TextDisabled("Create in %s", _current_path.filename().string().c_str());
	ImGui::Separator();
	if (ImGui::MenuItem(ICON_LC_FOLDER_PLUS "  New Folder")) RequestCreate(CreateKind::Folder);
	ImGui::Separator();
	if (ImGui::MenuItem(ICON_LC_BOX "  TGO (Blueprint)")) RequestCreate(CreateKind::Tgo);
	if (ImGui::MenuItem(ICON_LC_MAP "  Level")) RequestCreate(CreateKind::Level);
	if (ImGui::MenuItem(ICON_LC_PALETTE "  Material")) RequestCreate(CreateKind::Material);
	if (ImGui::MenuItem(ICON_LC_FILE_CODE "  Animation Clip")) RequestCreate(CreateKind::AnimationClip);
	if (ImGui::MenuItem(ICON_LC_SPARKLES "  Particle System")) RequestCreate(CreateKind::ParticleSystem);
}

void ContentBrowser::DrawCreatePopup()
{
	if (myOpenCreatePopup)
	{
		ImGui::OpenPopup("Create Asset");
		myOpenCreatePopup = false;
	}
	if (!ImGui::BeginPopupModal("Create Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		return;

	static const char* titles[] = { "", "Folder", "TGO", "Level", "Material", "Animation Clip", "Particle System" };
	static const char* extensions[] = { "", "", ".tgo", ".tgs", ".tgmat", ".tgac", ".tgps" };
	ImGui::Text("New %s", titles[(int)myCreateKind]);
	ImGui::TextDisabled("in %s", _current_path.string().c_str());
	ImGui::SetNextItemWidth(320.f);
	if (ImGui::IsWindowAppearing())
		ImGui::SetKeyboardFocusHere();
	const bool enter = ImGui::InputText("##createname", myCreateNameBuffer, IM_ARRAYSIZE(myCreateNameBuffer), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);

	const bool validName = myCreateNameBuffer[0] != '\0' && std::string_view(myCreateNameBuffer).find_first_of("\\/:*?\"<>|") == std::string_view::npos;
	ImGui::BeginDisabled(!validName);
	if ((ImGui::Button("Create", ImVec2(120.f, 0.f)) || enter) && validName)
	{
		const fs::path path = _current_path / (std::string(myCreateNameBuffer) + extensions[(int)myCreateKind]);
		std::string error;
		try
		{
			switch (myCreateKind)
			{
			case CreateKind::Folder:
			{
				auto command = std::make_shared<CreateAssetFolderCommand>(path);
				CommandManager::DoCommand(command);
				error = command->GetError();
				break;
			}
			case CreateKind::Tgo: error = Editor::GetEditor()->CreateNewObjectDefinition(path); break;
			case CreateKind::Level: error = Editor::GetEditor()->CreateNewScene(path); break;
			case CreateKind::Material: error = Editor::GetEditor()->CreateNewMaterial(path); break;
			case CreateKind::AnimationClip: error = Editor::GetEditor()->CreateNewAnimationClip(path); break;
			case CreateKind::ParticleSystem: error = Editor::GetEditor()->CreateNewParticleSystem(path); break;
			default: break;
			}
		}
		catch (const std::exception& e)
		{
			error = e.what();
		}
		myAssetOperationError = error;
		if (error.empty())
			ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120.f, 0.f)))
		ImGui::CloseCurrentPopup();
	if (!myAssetOperationError.empty())
		ImGui::TextColored(ImVec4(1.f, .45f, .25f, 1.f), "%s", myAssetOperationError.c_str());
	ImGui::EndPopup();
}

void ContentBrowser::ConvertFbxToTgo(const fs::path& absoluteFbxPath)
{
	if (myConvertRunner.IsRunning())
		return;

	FbxCookRequest request;
	const std::string error = MakeDefaultFbxCookRequest(absoluteFbxPath, request);
	if (!error.empty())
	{
		myAssetOperationError = error;
		return;
	}

	StartFbxConversion(request);
}

void ContentBrowser::OpenFbxConvertDialog(const fs::path& absoluteFbxPath)
{
	myFbxDialog.Open(absoluteFbxPath);
}

void ContentBrowser::StartFbxConversion(const FbxCookRequest& request)
{
	if (myConvertRunner.IsRunning())
		return;
	myConvertPrefab = request.generatedPrefab;
	myAssetOperationError = "Cooking...";
	myConvertRunner.Start(request);
}
