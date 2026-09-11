#include "stdafx.h"

#include <tge/editor/Document/Document.h>

using namespace Tga;

static int locDocumentCount;
Document::Document()
{
	myId = locDocumentCount;
	locDocumentCount++;
}

bool Document::HasUnsavedChanges() const
{
	return (mySaveUndoStackSize != myUndoStackSize);
}

void Document::Init(std::string_view path)
{
	myPath = path;

	myDocumentWindowClass = {};
	myDocumentWindowClass.ClassId = GetId();
	myDocumentWindowClass.DockingAllowUnclassed = false;
}