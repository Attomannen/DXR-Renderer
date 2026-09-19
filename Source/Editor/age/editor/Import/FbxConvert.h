#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>

namespace Ag
{
	// Finds the TextureCooker executable beside this GameEditor host exe, preferring
	// the variant matching this build's own configuration and falling back to
	// another one if that exact variant isn't present. Empty if none exist.
	std::filesystem::path FindTextureCookerExe();

	// One TextureCooker invocation. All folders are relative to Settings::GameAssetRoot().
	struct FbxCookRequest
	{
		std::string fbx, sourceFolder, outputFolder, materialFolder, generatedPrefab;
		// Source normal maps are assumed DirectX (green down) unless told otherwise.
		bool srcNormalsGl = false, flipGreen = false, recursive = true;
		// Recook every texture even if its output is newer than its sources. A plain
		// conversion doesn't need it: the cooker notices on its own when the normal
		// convention changed and recooks just the normal maps.
		bool force = false;
		std::vector<std::array<std::string, 2>> materialRemaps;
	};

	// What the Convert dialog edits, and what a model's optional .tgm sidecar stores.
	// Models without a sidecar use the project defaults (EditorSettings::fbx*).
	struct FbxImportSettings
	{
		bool normalsOpenGL = false;
		bool flipGreen = false;
		bool recursive = true;
		// Folders, relative to the asset root.
		std::string sourceFolder, cookedFolder, materialFolder;
		std::vector<std::array<std::string, 2>> materialRemaps;
		bool hasSidecar = false;   // not stored
	};

	// The model's sidecar if it has one, else the project defaults resolved against
	// the model's folder. Never fails: a missing or unreadable file just means defaults.
	FbxImportSettings LoadFbxImportSettings(const std::filesystem::path& absoluteFbxPath);
	// Writes <model>.tgm beside the FBX.
	std::string SaveFbxSidecar(const std::filesystem::path& absoluteFbxPath, const FbxImportSettings& settings);
	// Remembers these choices as the project defaults for models without a sidecar.
	void RememberFbxImportDefaults(const std::filesystem::path& absoluteFbxPath, const FbxImportSettings& settings);
	// Turns settings into a cooker request, creating the folders it writes into.
	// Returns an error message, empty on success.
	std::string MakeFbxCookRequest(const std::filesystem::path& absoluteFbxPath, const FbxImportSettings& settings, FbxCookRequest& outRequest);
	// A conversion with no dialog: the model's sidecar, or the project defaults.
	std::string MakeDefaultFbxCookRequest(const std::filesystem::path& absoluteFbxPath, FbxCookRequest& outRequest);

	// Runs the cooker and returns a message for the user. Blocks -- use TextureCookRunner.
	std::string RunFbxCooker(const FbxCookRequest& request);

	// Runs RunFbxCooker on a detached background thread so the editor stays responsive
	// (a large cook takes minutes). The thread only touches a shared result box, so
	// the runner can be destroyed while a cook is still in flight.
	class TextureCookRunner
	{
	public:
		void Start(const FbxCookRequest& request);
		bool IsRunning() const { return myState && !myState->done.load(); }
		// True exactly once, the frame a cook finishes, with its message.
		bool PollResult(std::string& outResult);
	private:
		struct SharedResult
		{
			std::atomic<bool> done{ false };
			std::mutex mutex;
			std::string result;
		};
		std::shared_ptr<SharedResult> myState;
	};

	// Renders one material's normal map as the current settings would cook it, so a
	// wrong convention shows up before a long cook rather than after it.
	class NormalPreviewRunner
	{
	public:
		struct Params
		{
			std::filesystem::path sourceFolder;
			bool normalsOpenGL = false, flipGreen = false, recursive = true;
			int index = -1;   // which material with a normal map; -1 = let the cooker choose
		};
		struct Result
		{
			bool ok = false;
			std::string error, key;
			int index = 0, count = 0;
			std::string sourceImage, currentImage, oppositeImage;   // absolute .dds paths
		};
		void Start(const Params& params);
		bool IsRunning() const { return myState && !myState->done.load(); }
		bool PollResult(Result& out);
	private:
		struct SharedResult
		{
			std::atomic<bool> done{ false };
			std::mutex mutex;
			Result result;
		};
		std::shared_ptr<SharedResult> myState;
	};

	// The "Convert FBX" dialog: settings for this model, a normal-map preview, and
	// Convert / Cancel. Opened from the Content Browser.
	class FbxConvertDialog
	{
	public:
		void Open(const std::filesystem::path& absoluteFbxPath);
		// Call every frame from the owning panel. Returns true on the frame the user
		// presses Convert, with outRequest ready to run.
		bool Draw(FbxCookRequest& outRequest);
	private:
		void RequestPreview();
		void ApplyPreviewResult(const NormalPreviewRunner::Result& result);

		bool myOpenRequested = false;
		bool myOpen = false;
		std::filesystem::path myFbx;
		FbxImportSettings mySettings;
		bool myRememberForModel = false;
		std::string myError;

		NormalPreviewRunner myPreviewRunner;
		bool myPreviewDirty = false;
		int myPreviewIndex = -1;
		NormalPreviewRunner::Result myPreview;
		bool myHasPreview = false;
		ImTextureID myPreviewTextures[3] = { 0, 0, 0 };
	};
}
