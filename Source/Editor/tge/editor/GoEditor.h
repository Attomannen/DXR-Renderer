#pragma once

#include <tge/editor/EditorConfiguration.h>
#include <tge/editor/EditorGraphics/EditorGraphicsBase.h>

void GoEditor(const char* aSettingsPath, const EditorConfiguration& aEditorConfiguration = DefaultEditorConfiguration, std::unique_ptr<Tga::EditorGraphicsBase>&& graphics = nullptr);
