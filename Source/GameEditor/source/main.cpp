#include <tge/editor/GoEditor.h>

#include <DefaultEditorGraphics.h>

int main(const int /*argc*/, const char* /*argc*/[])
{
	GoEditor(TGE_PROJECT_SETTINGS_FILE, DefaultEditorConfiguration, std::make_unique<Tga::DefaultEditorGraphics>());
	return 0;
}
