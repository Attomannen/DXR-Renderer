#pragma once

#include <tge/editor/Document/Document.h>
#include <array>
#include <string>
#include <vector>

namespace Tga
{
	// Parameters for a single TextureCooker invocation, shared by the
	// ImportSettingsDocument UI (Reimport) and any other caller that wants to
	// cook an FBX without going through a saved .tgm settings file first (e.g.
	// the AssetBrowser's "Convert to TGO" context menu action).
	struct FbxCookRequest
	{
		// All paths are relative to Settings::GameAssetRoot(), matching the
		// convention used by ImportSettingsDocument's own fields.
		std::string fbx, sourceFolder, outputFolder, generatedPrefab;
		bool srcNormalsGl = true, flipGreen = false, recursive = true;
		std::vector<std::array<std::string, 2>> materialRemaps;
	};

	class ImportSettingsDocument final : public Document
	{
	public:
		void Init(std::string_view path) override;
		void Update(float, InputManager&) override;
		void Save() override;

		// Runs TextureCooker synchronously for the given request and returns a
		// human-readable status message (success or failure detail). Shared so
		// the AssetBrowser's quick "Convert to TGO" action doesn't reimplement
		// the process launch and result verification.
		static std::string RunCooker(const FbxCookRequest& request);
	private:
		void Reimport();
		void PickFolder(std::string& target);
		void PickFbx();
		void ApplyRecommendedPaths();
		std::string myFbx, mySourceFolder, myOutputFolder, myGeneratedPrefab, myStatus;
		float myScale = 1.f;
		// OpenGL/green-up is the safe default for authored texture sets.
		int myAxis = 0, myNormals = 0;
		bool myFlipGreen = false, myRecursive = true;
		std::vector<std::array<std::string, 2>> myRemaps;
	};
}
