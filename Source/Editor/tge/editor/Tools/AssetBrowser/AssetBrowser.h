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

	private:
		void DrawFileTree(const fs::path& parentPath);

		float myThumbSize = 32.0f;

		fs::path mySelectedPath;

		std::unique_ptr<FileHierarchyCache> myCache;
	};
}