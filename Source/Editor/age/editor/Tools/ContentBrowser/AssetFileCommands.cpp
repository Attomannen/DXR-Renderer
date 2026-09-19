#include <age/editor/Tools/ContentBrowser/AssetFileCommands.h>

using namespace Ag;

// ---- CreateAssetFolderCommand ----------------------------------------------

CreateAssetFolderCommand::CreateAssetFolderCommand(fs::path aPath)
	: myPath(std::move(aPath))
{
}

void CreateAssetFolderCommand::Execute()
{
	myError.clear();
	std::error_code ec;
	fs::create_directory(myPath, ec);
	if (ec) myError = "Could not create folder: " + ec.message();
}

void CreateAssetFolderCommand::Undo()
{
	// Only actually removes it if still empty (the normal case -- undo
	// usually follows right after create, before anything's been put in
	// it). A folder someone has since added files to silently survives an
	// undo rather than deleting their work; std::error_code swallows the
	// "not empty" failure rather than surfacing it as an editor error,
	// since from the user's perspective this isn't really a failure.
	std::error_code ec;
	fs::remove(myPath, ec);
}

// ---- RenameAssetCommand -----------------------------------------------------

RenameAssetCommand::RenameAssetCommand(fs::path aFrom, fs::path aTo)
	: myFrom(std::move(aFrom))
	, myTo(std::move(aTo))
{
}

void RenameAssetCommand::Execute()
{
	myError.clear();
	std::error_code ec;
	fs::rename(myFrom, myTo, ec);
	if (ec) myError = "Could not rename asset: " + ec.message();
}

void RenameAssetCommand::Undo()
{
	std::error_code ec;
	fs::rename(myTo, myFrom, ec);
}

// ---- DeleteAssetCommand ------------------------------------------------------

DeleteAssetCommand::DeleteAssetCommand(fs::path aPath, fs::path aAssetRoot)
	: myOriginalPath(std::move(aPath))
{
	std::error_code ec;
	fs::path relative = fs::relative(myOriginalPath, aAssetRoot, ec);
	myTrashPath = ec ? (aAssetRoot / kAssetTrashFolderName / myOriginalPath.filename())
	                 : (aAssetRoot / kAssetTrashFolderName / relative);
}

void DeleteAssetCommand::Execute()
{
	myError.clear();
	std::error_code ec;
	fs::create_directories(myTrashPath.parent_path(), ec);
	fs::rename(myOriginalPath, myTrashPath, ec);
	if (ec) myError = "Could not delete asset: " + ec.message();
}

void DeleteAssetCommand::Undo()
{
	std::error_code ec;
	fs::create_directories(myOriginalPath.parent_path(), ec);
	fs::rename(myTrashPath, myOriginalPath, ec);
}
