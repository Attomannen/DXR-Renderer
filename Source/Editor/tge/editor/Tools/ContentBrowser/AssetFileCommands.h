#pragma once

#include <filesystem>
#include <string>

#include <tge/editor/CommandManager/AbstractCommand.h>

namespace Tga
{
	namespace fs = std::filesystem;

	// Where DeleteAssetCommand moves a file instead of actually erasing it,
	// so Undo can move it back -- keeps the file's relative path structure
	// underneath it (".trash/Textures/Foo.dds", not just "Foo.dds") so two
	// deleted files that happen to share a filename in different folders
	// don't collide. ContentBrowser's background scan thread excludes this
	// folder by name, the same way it already excludes ".leveldata".
	constexpr const char* kAssetTrashFolderName = ".trash";

	// None of these three touch a Scene or Document -- plain filesystem
	// operations, so AbstractCommand directly, same reasoning as
	// ChangeMaterialCommand.

	class CreateAssetFolderCommand : public AbstractCommand
	{
	public:
		explicit CreateAssetFolderCommand(fs::path aPath);
		void Execute() override;
		void Undo() override;
		const char* GetName() const override { return "Create Folder"; }
		const std::string& GetError() const { return myError; }
	private:
		fs::path myPath;
		std::string myError;
	};

	class RenameAssetCommand : public AbstractCommand
	{
	public:
		RenameAssetCommand(fs::path aFrom, fs::path aTo);
		void Execute() override;
		void Undo() override;
		const char* GetName() const override { return "Move / Rename Asset"; }
		const std::string& GetError() const { return myError; }
	private:
		fs::path myFrom;
		fs::path myTo;
		std::string myError;
	};

	// Do moves aPath under a trash root derived from aAssetRoot (mirroring
	// aPath's own position relative to aAssetRoot); Undo moves it back.
	// aAssetRoot must be an ancestor of aPath.
	class DeleteAssetCommand : public AbstractCommand
	{
	public:
		DeleteAssetCommand(fs::path aPath, fs::path aAssetRoot);
		void Execute() override;
		void Undo() override;
		const char* GetName() const override { return "Delete Asset"; }
		const std::string& GetError() const { return myError; }
	private:
		fs::path myOriginalPath;
		fs::path myTrashPath;
		std::string myError;
	};
}
