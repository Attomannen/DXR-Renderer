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
#include <tge/editor/Import/ImportSettingsDocument.h>

#include <IconFontHeaders/IconsLucide.h>
#include <tge/editor/p4/p4.h>

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
	auto parentIt = myCache->activeCache.find(parentPath);

	if (parentIt == myCache->activeCache.end())
		return;

	const DirectoryCache& parentCache = parentIt->second;

	for (fs::path path : parentCache.directories)
	{
		auto it = myCache->activeCache.find(path);
		if (it == myCache->activeCache.end())
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
					std::error_code ec;
					if (source != destination && !fs::exists(destination)) fs::rename(source, destination, ec);
					if (ec) myAssetOperationError = "Could not move asset: " + ec.message();
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
	std::lock_guard guard(myCache->isAccessingCache);

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
				std::error_code ec;
				fs::create_directory(_current_path / myNewFolderBuffer, ec);
				if (ec) myAssetOperationError = "Could not create folder: " + ec.message();
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
		auto parentIt = myCache->activeCache.find(_current_path);

		if (parentIt != myCache->activeCache.end())
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

					P4::FileInfo fileinfo = P4::GetFileInfo(path.string().c_str());

					const std::string extension = path.extension().string();
					std::string icon = ICON_LC_FILE;
					if (extension == ".tgmat") icon = ICON_LC_PALETTE;
					else if (extension == ".dds") icon = ICON_LC_IMAGE;
					else if (extension == ".tgs") icon = ICON_LC_MAP;
					else if (extension == ".tgm" || extension == ".fbx") icon = ICON_LC_CUBOID;
					else if (extension == ".tgo" || extension == ".tgac") icon = ICON_LC_FILE_CODE;
					if (fileinfo.action != P4::FileAction::None)
					{
						switch (fileinfo.action)
						{
						case(P4::FileAction::Add): { icon = ICON_LC_FILE_PLUS_2; } break;
						case(P4::FileAction::Edit): { icon = ICON_LC_FILE_PEN; } break;
						case(P4::FileAction::Delete): { icon = ICON_LC_FILE_X_2; } break;
						default: { icon = ICON_LC_FILE; } break;
						}
					}

					if (path.extension() == ".dds")
					{
						ImTextureID img = Editor::GetEditor()->GetEditorGraphics().GetTextureID(path.string());
						if (img)
						{
							itemStatus = Tga::AssetListItem(path, isSelected, (fileinfo.action != P4::FileAction::None) ? icon : "", img, myThumbSize);
						}
					}
					else
					{
						itemStatus = Tga::AssetListItem(path, isSelected, icon);

						if (path.extension() == ".tgs")
						{
							if (itemStatus.doubleClicked)
							{
								// todo: check if already open!
								// move this logic somewhere else?

								std::unique_ptr<SceneDocument> sceneDocument = std::make_unique<SceneDocument>();
								sceneDocument->Init(path.string());

								Editor::GetEditor()->AddDocument(std::move(sceneDocument));
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
							try
							{
								std::unique_ptr<ImportSettingsDocument> document = std::make_unique<ImportSettingsDocument>();
								document->Init(path.string());
								Editor::GetEditor()->AddDocument(std::move(document));
							}
							catch (const std::exception& e)
							{
								ERROR_PRINT("FBX import settings: could not open '%s': %s", path.string().c_str(), e.what());
							}
							catch (...)
							{
								ERROR_PRINT("FBX import settings: could not open '%s': unknown exception", path.string().c_str());
							}
						}
					}
					//if (ImGui::IsItemHovered())
					if (itemStatus.hovered)
					{
						if (P4::QueryHasFileInfo(path.string().c_str()))
						{
							ImGui::BeginTooltip();
							{
								ImGui::PushTextWrapPos(ImGui::GetFontSize() * 20);
								ImGui::TextWrapped(
									"At revision %d marked for %s by %s in changelist %s workspace %s",
									fileinfo.revision, P4::FileActionString(fileinfo.action).data(), fileinfo.user, fileinfo.changelist, fileinfo.client
								);
								ImGui::PopTextWrapPos();
							}
							ImGui::EndTooltip();

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
						if (absPath.extension() == ".fbx" && ImGui::MenuItem("Convert to TGO"))
							ConvertFbxToTgo(absPath);
						if (ImGui::MenuItem("Delete"))
						{
							myPendingDelete = absPath;
						}
						ImGui::EndPopup();
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
			std::error_code ec;
			fs::remove(myPendingDelete, ec);
			if (ec) myAssetOperationError = "Could not delete asset: " + ec.message();
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
}

void AssetBrowser::ConvertFbxToTgo(const fs::path& absoluteFbxPath)
{
	const fs::path root = fs::absolute(Settings::GameAssetRoot());
	std::error_code ec;
	const fs::path relFbx = fs::relative(absoluteFbxPath, root, ec);
	if (ec || relFbx.empty() || relFbx.string() == "." || relFbx.is_absolute())
	{
		myAssetOperationError = "The FBX must be inside this project's asset folder.";
		return;
	}

	const fs::path folder = relFbx.parent_path();
	const fs::path texturesFolder = folder / "Textures";
	const fs::path materialsFolder = folder / "Materials";
	fs::create_directories(root / texturesFolder, ec);
	if (ec) { myAssetOperationError = "Could not create Textures folder: " + ec.message(); return; }
	fs::create_directories(root / materialsFolder, ec);
	if (ec) { myAssetOperationError = "Could not create Materials folder: " + ec.message(); return; }

	FbxCookRequest request;
	request.fbx = relFbx.generic_string();
	request.sourceFolder = texturesFolder.generic_string();
	request.outputFolder = materialsFolder.generic_string();
	request.generatedPrefab = (folder / (relFbx.stem().string() + ".tgo")).generic_string();
	request.recursive = false;
	myAssetOperationError = ImportSettingsDocument::RunCooker(request);
}
