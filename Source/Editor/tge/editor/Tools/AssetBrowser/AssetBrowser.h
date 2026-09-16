#pragma once

#include <tge/editor/Tools/ToolsInterface.h>
#include <tge/stringRegistry/StringRegistry.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace Tga
{
	struct FileHierarchyCache;

	class AssetBrowser : public ToolsInterface 
	{
	public:
		AssetBrowser();
		~AssetBrowser();

		virtual void Draw();
		void SetPath(const std::string_view &);
		StringId GetSelectedAsset();
		fs::path GetCurrentFolder() const;

	private:
		void DrawFileTree(const fs::path& parentPath);
		void ConvertFbxToTgo(const fs::path& absoluteFbxPath);

		float myThumbSize = 32.0f;
		char mySearchBuffer[128]{};
		char myNewFolderBuffer[128]{};
		std::string myAssetOperationError;
		fs::path myPendingDelete;
		int myAssetTypeFilter = 0;

		fs::path mySelectedPath;

		std::unique_ptr<FileHierarchyCache> myCache;
	};
}
