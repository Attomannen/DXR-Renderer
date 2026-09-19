#pragma once

#include <age/editor/EditorConfiguration.h>
#include <age/editor/EditorGraphics/EditorGraphicsBase.h>

void GoEditor(const char* aSettingsPath, const EditorConfiguration& aEditorConfiguration = DefaultEditorConfiguration, std::unique_ptr<Ag::EditorGraphicsBase>&& graphics = nullptr);
