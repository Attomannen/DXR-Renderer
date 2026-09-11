#include <tge/editor/Tools/AssetBrowser/AssetBrowser.h>

#include <string>
#include <mutex>
#include <chrono>
#include <thread>

#include <imgui.h>
#include <tge/editor/imgui_widgets/imgui_widgets.h>
#include <tge/settings/settings.h>
#include <tge/scene/SceneSerialize.h>

#include <tge/editor/Editor.h>
#include <tge/editor/AnimationClip/AnimationClipDocument.h>
#include <tge/editor/ObjectDefinition/ObjectDefinitionDocument.h>
#include <tge/editor/Scene/SceneDocument.h>
#include <tge/editor/Material/MaterialDocument.h>

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
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 5.f));
		ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 2.f);
		ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.1f, 0.1f, .15f, 0.9f));
		if (ImGui::BeginPopupContextWindow("File menu")) {
			//if (ImGui::BeginPopupContextItem("bar")) // <-- use last item id as popup id
			ImGui::Text("Menu");
			ImGui::Separator();
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
			if (ImGui::Selectable("Create folder"))
			{
				ImGui::CloseCurrentPopup();
			}
			if (ImGui::Selectable("Delete folder"))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::PopStyleColor();
			ImGui::EndPopup();
		}
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(2);

		if (ImGui::IsItemClicked()) {
			_current_path = path;
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
		auto parentIt = myCache->activeCache.find(_current_path);

		if (parentIt != myCache->activeCache.end())
		{

			const DirectoryCache& parentCache = parentIt->second;

			for (fs::path absPath : parentCache.files)
			{
				{
					const std::string& root = Tga::Settings::GameAssetRoot();
					fs::path path = fs::relative(absPath, root);

					bool isSelected = mySelectedPath == path;
					Tga::AssetListItemStatus itemStatus{};

					P4::FileInfo fileinfo = P4::GetFileInfo(path.string().c_str());

					std::string icon = ICON_LC_FILE;
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
								std::unique_ptr<MaterialDocument> document = std::make_unique<MaterialDocument>();
								document->Init(path.string());

								Editor::GetEditor()->AddDocument(std::move(document));
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
				}
			}
		}
	}
	ImGui::End();
}