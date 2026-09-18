#include <tge/editor/Tools/AssetBrowser/AssetBrowser.h>

#include <string>
#include <mutex>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cctype>

#include <imgui.h>
#include <tge/editor/imgui_widgets/imgui_widgets.h>
#include <tge/settings/settings.h>
#include <tge/log/Log.h>
#include <tge/scene/SceneSerialize.h>

#include <tge/editor/Editor.h>
#include <tge/editor/AnimationClip/AnimationClipDocument.h>
#include <tge/editor/ObjectDefinition/ObjectDefinitionDocument.h>
#include <tge/editor/Scene/SceneDocument.h>
#include <tge/editor/Material/MaterialDocument.h>
#include <tge/editor/Import/FbxConvert.h>
#include <tge/editor/Tools/AssetBrowser/AssetFileCommands.h>
#include <tge/editor/CommandManager/CommandManager.h>

#include <IconFontHeaders/IconsLucide.h>

#define HIDE_LEVELDATA_DIRECTORIES 

using namespace Tga;

static fs::path _current_path;

namespace Tga
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
#ifdef HIDE_LEVELDATA_DIRECTORIES
						if (item.path().extension() == ".leveldata")
						{
							continue;
						}
#endif

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

AssetBrowser::AssetBrowser()
{
	myCache = std::make_unique<FileHierarchyCache>();

	myCache->cacheThread = std::thread(UpdateCacheThread, myCache.get());
}

AssetBrowser::~AssetBrowser()
{
	myCache->shutDownUpdate = true;
	myCache->cacheThread.join();
}

void AssetBrowser::SetPath(const std::string_view& aPath) 
{
	_current_path = fs::absolute(aPath);

	std::lock_guard guard(myCache->isAccessingCache);
	myCache->root = fs::absolute(aPath);
}

StringId AssetBrowser::GetSelectedAsset()
{
	return StringRegistry::RegisterOrGetString(mySelectedPath.string());
}

fs::path AssetBrowser::GetCurrentFolder() const
{
	return _current_path;
}

void AssetBrowser::DrawFileTree(const fs::path& parentPath)
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

		// @todo: if we wanted to hide leveldata-folders HIDE_LEVELDATA_DIRECTORIES shows an example of how to do it..
#ifdef HIDE_LEVELDATA_DIRECTORIES
		if (path.extension() == ".leveldata")
		{
			continue;
		}
#endif

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
			const char* types[] = { ".tgo", ".tgs", ".tgm", ".tgmat", ".tgac", ".dds", ".fbx" };
			for (const char* type : types)
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(type))
				{
					fs::path source = fs::path(Tga::Settings::GameAssetRoot()) / static_cast<const char*>(payload->Data);
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

void AssetBrowser::Draw()
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
	ImGui::Begin("Asset Browser - Directories");
	{
		if (ImGui::Button(ICON_LC_FOLDER_PLUS " Create Folder"))
		{
			myNewFolderBuffer[0] = '\0';
			myAssetOperationError.clear();
			ImGui::OpenPopup("Create Folder");
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Create a folder inside the selected asset directory.");
		if (!myAssetOperationError.empty())
		{
			ImGui::TextColored(ImVec4(1.f, .45f, .25f, 1.f), "%s", myAssetOperationError.c_str());
		}

		if (ImGui::BeginPopupModal("Create Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextDisabled("Location: %s", _current_path.string().c_str());
			ImGui::SetNextItemWidth(300.f);
			ImGui::InputTextWithHint("Name", "New Folder", myNewFolderBuffer, IM_ARRAYSIZE(myNewFolderBuffer), ImGuiInputTextFlags_AutoSelectAll);
			const bool validName = myNewFolderBuffer[0] != '\0' && std::string_view(myNewFolderBuffer).find_first_of("\\/:*?\"<>|") == std::string_view::npos;
			ImGui::BeginDisabled(!validName);
			if (ImGui::Button("Create", ImVec2(120.f, 0.f)))
			{
				auto command = std::make_shared<CreateAssetFolderCommand>(_current_path / myNewFolderBuffer);
				CommandManager::DoCommand(command);
				if (!command->GetError().empty()) myAssetOperationError = command->GetError();
				else ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120.f, 0.f))) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::Separator();
		ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_SpanAvailWidth;
		node_flags |= ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow;

		if (ImGui::TreeNodeEx("Game", node_flags))
		{
			fs::path path = fs::absolute(Tga::Settings::GameAssetRoot());
			if (ImGui::IsItemClicked()) {
				_current_path = path;
			}
			DrawFileTree(path.string().c_str());
			ImGui::TreePop();
		}
	}
	ImGui::End();

	ImGui::SetNextWindowClass(Editor::GetEditor()->GetGlobalWindowClass());
	ImGui::Begin("Asset Browser - Files");
	{
		if (ImGui::Button(myGridView ? ICON_LC_LIST : ICON_LC_LAYOUT_GRID))
			myGridView = !myGridView;
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip(myGridView ? "Switch to list view" : "Switch to grid view");
		ImGui::SameLine();
		if (myGridView)
		{
			ImGui::SetNextItemWidth(90.f);
			ImGui::SliderFloat("##TileSize", &myGridTileSize, 64.f, 192.f, "%.0fpx");
			ImGui::SameLine();
		}
		ImGui::SetNextItemWidth(-105.f);
		ImGui::InputTextWithHint("##AssetSearch", "Search assets…", mySearchBuffer, IM_ARRAYSIZE(mySearchBuffer));
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

			for (fs::path absPath : parentCache.files)
			{
				if (!matchesSearch(absPath))
					continue;
				// Use a stable, per-asset scope for menu and row controls.
				ImGui::PushID(absPath.generic_string().c_str());
				{
					const std::string& root = Tga::Settings::GameAssetRoot();
					fs::path path = fs::relative(absPath, root);

					bool isSelected = mySelectedPath == path;
					Tga::AssetListItemStatus itemStatus{};

					const std::string extension = path.extension().string();
					std::string icon = ICON_LC_FILE;
					if (extension == ".tgmat") icon = ICON_LC_PALETTE;
					else if (extension == ".dds") icon = ICON_LC_IMAGE;
					else if (extension == ".tgs") icon = ICON_LC_MAP;
					else if (extension == ".tgm" || extension == ".fbx") icon = ICON_LC_CUBOID;
					else if (extension == ".tgo" || extension == ".tgac") icon = ICON_LC_FILE_CODE;

					if (path.extension() == ".dds")
					{
						ImTextureID img = Editor::GetEditor()->GetEditorGraphics().GetTextureID(path.string());
						if (img)
						{
							itemStatus = myGridView
								? Tga::AssetGridItem(path, isSelected, "", img, myGridTileSize)
								: Tga::AssetListItem(path, isSelected, "", img, myThumbSize);
						}
					}
					else
					{
						itemStatus = myGridView
							? Tga::AssetGridItem(path, isSelected, icon, (ImTextureID)0, myGridTileSize)
							: Tga::AssetListItem(path, isSelected, icon);

						if (path.extension() == ".tgs")
						{
							if (itemStatus.doubleClicked)
							{
								// todo: check if already open!
								// move this logic somewhere else?

								// SceneDocument::Init() throws if the .tgs no longer exists on
								// disk (deleted/moved since this listing was scanned) -- same
								// guard as the .tgmat case below, so a stale double-click
								// reports a status message instead of crashing the editor.
								try
								{
									std::unique_ptr<SceneDocument> sceneDocument = std::make_unique<SceneDocument>();
									sceneDocument->Init(path.string());
									Editor::GetEditor()->AddDocument(std::move(sceneDocument));
								}
								catch (const std::exception& e)
								{
									myAssetOperationError = std::string("Could not open scene '") + path.string() + "': " + e.what();
								}
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

	if (!myPendingDelete.empty()) ImGui::OpenPopup("Confirm Asset Delete");
	if (ImGui::BeginPopupModal("Confirm Asset Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped("Delete '%s'?", myPendingDelete.filename().string().c_str());
		if (ImGui::Button("Delete", ImVec2(120.f, 0.f)))
		{
			auto command = std::make_shared<DeleteAssetCommand>(myPendingDelete, fs::absolute(Tga::Settings::GameAssetRoot()));
			CommandManager::DoCommand(command);
			if (!command->GetError().empty()) myAssetOperationError = command->GetError();
			else if (mySelectedPath == fs::relative(myPendingDelete, Tga::Settings::GameAssetRoot())) mySelectedPath.clear();
			myPendingDelete.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.f, 0.f))) { myPendingDelete.clear(); ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	}
	ImGui::End();

	// Outside both panels' windows, so the dialog's popup id is the same no
	// matter which one opened it.
	FbxCookRequest requestFromDialog;
	if (myFbxDialog.Draw(requestFromDialog))
		StartFbxConversion(requestFromDialog);
}

void AssetBrowser::ConvertFbxToTgo(const fs::path& absoluteFbxPath)
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

void AssetBrowser::OpenFbxConvertDialog(const fs::path& absoluteFbxPath)
{
	myFbxDialog.Open(absoluteFbxPath);
}

void AssetBrowser::StartFbxConversion(const FbxCookRequest& request)
{
	if (myConvertRunner.IsRunning())
		return;
	myConvertPrefab = request.generatedPrefab;
	myAssetOperationError = "Cooking...";
	myConvertRunner.Start(request);
}
