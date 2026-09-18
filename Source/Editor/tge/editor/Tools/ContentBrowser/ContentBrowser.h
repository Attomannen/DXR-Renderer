#pragma once

#include <tge/editor/Tools/ToolsInterface.h>
#include <tge/editor/Import/FbxConvert.h>
#include <tge/stringRegistry/StringRegistry.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace Tga
{
	struct FileHierarchyCache;

	class ContentBrowser : public ToolsInterface 
	{
	public:
		ContentBrowser();
		~ContentBrowser();

		virtual void Draw();
		void SetPath(const std::string_view &);
		StringId GetSelectedAsset();
		// File > New Level: asks for a name in the Content Browser's current folder.
		void RequestNewLevel();
		fs::path GetCurrentFolder() const;

		// Converts an FBX into its .tgo prefab plus one .tgmat per FBX material, with
		// no dialog: the model's own settings or the project defaults. Used when an
		// FBX is dragged into a scene. Does nothing while a conversion is running.
		void ConvertFbxToTgo(const fs::path& absoluteFbxPath);
		// The same conversion behind the Convert dialog: settings for this model and a
		// normal-map preview before anything is cooked.
		void OpenFbxConvertDialog(const fs::path& absoluteFbxPath);
		bool IsConverting() const { return myConvertRunner.IsRunning(); }

	private:
		void DrawFileTree(const fs::path& parentPath);
		void DrawBreadcrumbs();
		void DrawAddMenuItems();
		void DrawCreatePopup();

		// What the Add menu can make in the current folder. The name is asked for in a small
		// popup, then the asset is created and opened.
		enum class CreateKind { None, Folder, Tgo, Level, Material, AnimationClip };
		CreateKind myCreateKind = CreateKind::None;
		bool myOpenCreatePopup = false;
		char myCreateNameBuffer[128]{};
		void RequestCreate(CreateKind kind);

		float myThumbSize = 32.0f;
		// Content-Browser-style tile view toggle, alongside the original flat
		// list -- see Draw()'s Files panel.
		bool myGridView = false;
		float myGridTileSize = 96.0f;
		char mySearchBuffer[128]{};
		char myNewFolderBuffer[128]{};
		std::string myAssetOperationError;
		fs::path myPendingDelete;
		int myAssetTypeFilter = 0;
		TextureCookRunner myConvertRunner;
		FbxConvertDialog myFbxDialog;
		// Runs a conversion on the background thread; progress and the result show in
		// this panel's status line, and the prefab is reloaded when the cook finishes.
		void StartFbxConversion(const FbxCookRequest& request);
		// Prefab (relative to the asset root) the in-flight conversion is writing,
		// so its in-memory definition can be reloaded once the cook finishes.
		fs::path myConvertPrefab;

		fs::path mySelectedPath;

		std::unique_ptr<FileHierarchyCache> myCache;
	};
}
